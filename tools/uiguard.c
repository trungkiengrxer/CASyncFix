// uiguard: keeps the UI responsive while builds, tests and agents run (docs/superpowers/specs/2026-11-16-uiguard-design.md).
// All cores of this CPU run at one shared clock under a 35 W budget, and the UI lags below about 3.0 GHz. While the user
// interacts (pointer: move, click, drag, scroll, trackpad gesture; or a modifier key: Cmd-Tab, Ctrl-arrow) and the cores
// run below the target clock (Intel Power Gadget library, no root), heavy command-line work is paused for part of every
// 100 ms slice (SIGSTOP / SIGCONT): 10 ms more per 200 ms step while below target, 10 ms less while 200 MHz above it,
// up to 80 ms. Above the target, or one second after the last interaction, the work runs at full speed. Without the
// Power Gadget library it does nothing. User level only: no kext, no root, it only signals this user's own processes.
//
// Targets: known compilers by name, or any command-line process using >= 25% of a core (kept for 5 s once chosen).
// Never: anything inside *.app/Contents/MacOS/ (apps with a UI and their helpers), other users' processes (WindowServer,
// system), Dock, uiguard itself.
//
// Build: clang -O2 -framework ApplicationServices -F/Library/Frameworks -weak_framework IntelPowerGadget \
//              -o tools/uiguard tools/uiguard.c
// Run:   ./tools/uiguard [-t targetMHz=3000] [-s maxStopMs=80] [-h holdSec=1.0] [-c cpuShare=0.25] [-n extraName]... [-f] [-v]
//          -f  act as if the user were always interacting (scripted tests)
//        ./tools/uiguard --selftest | --release      (--release: SIGCONT every target, after a crash)
// Auto start: tools/com.trungkien.uiguard.plist (LaunchAgent). Stop: every paused process is resumed first.
#include <ApplicationServices/ApplicationServices.h>
#include <IntelPowerGadget/PowerGadgetLib.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc.h>
#include <unistd.h>

#define SLICE_MS 100
#define MAX_PROCS 4096
#define MAX_NAMES 64
#define STICKY_SEC 5.0

static double cpuThreshold = 0.25;

// ms of the next slice to keep the targets paused: stopMs while the user interacted within holdSec, else 0.
static int guardStopMs(double idleSec, double holdSec, int stopMs) {
    if (stopMs < 0) stopMs = 0;
    if (stopMs > 95) stopMs = 95;           // work always keeps at least 5% so nothing times out
    return idleSec >= 0 && idleSec < holdSec ? stopMs : 0;
}

// Pause for the next slices: while interacting, +10 ms per step below targetMHz, -10 ms per step 200 MHz above it,
// kept in [0, maxMs]; not interacting or frequency unknown (freqMHz < 0) -> 0.
static int guardAdaptStop(int curMs, double freqMHz, double targetMHz, int interacting, int maxMs) {
    if (!interacting || freqMHz < 0) return 0;
    int next = curMs;
    if (freqMHz < targetMHz) next += 10;
    else if (freqMHz > targetMHz + 200) next -= 10;
    if (next > maxMs) next = maxMs;
    if (next > 95) next = 95;
    return next < 0 ? 0 : next;
}

// The clock to act on: a fresh read, else the last good read while it is under 1 s old, else -1 (unknown).
static double guardHeldMHz(double fresh, double lastGood, double lastGoodAgeSec) {
    if (fresh >= 0) return fresh;
    return lastGood >= 0 && lastGoodAgeSec <= 1.0 ? lastGood : -1;
}

// Highest core clock since the previous call (Intel Power Gadget library, weak-linked), or -1 when unavailable.
// The highest, not the mean: at light load idle cores drag the mean down to 2 GHz although the busy ones run at 3.6+;
// only when every core is held down by the power budget is the highest one below the target too.
static double coreMHz(void) {
    static int state = 0;               // 0 untried, 1 ready, -1 unavailable
    static PGSampleID prev;
    if (state == 0) {
        state = (&PG_Initialize != NULL && PG_Initialize() && PG_ReadSample(0, &prev)) ? 1 : -1;
        return -1;
    }
    if (state < 0) return -1;
    PGSampleID cur;
    double mean, mn, mx = -1;
    if (!PG_ReadSample(0, &cur)) return -1;
    if (!PGSample_GetIAFrequency(prev, cur, &mean, &mn, &mx)) mx = -1;
    PGSample_Release(prev);
    prev = cur;
    return mx;
}

static const char *names[MAX_NAMES] = {"clang", "swift-frontend", "swiftc", "swift-driver", "ld", "cc1", "gcc", "g++",
                                       "rustc", "cargo", "javac", "kotlinc", "xcodebuild", "XCBBuildService", "ninja",
                                       "make", "esbuild", "go", "dsymutil", "ibtool", "actool", NULL};
static int nameCount(void) { int n = 0; while (names[n]) n++; return n; }

// prefix match for compiler families ("clang" also matches "clang++"), exact for short generic names
static int isTarget(const char *comm) {
    for (int i = 0; names[i]; i++) {
        size_t l = strlen(names[i]);
        if (l <= 4 ? strcmp(comm, names[i]) == 0 : strncmp(comm, names[i], l) == 0) return 1;
    }
    return 0;
}

// An app with a UI, or one of its helpers (Edge Helper, Code Helper, Claude Helper...).
static int guardIsGuiPath(const char *path) { return strstr(path, ".app/Contents/MacOS/") != NULL; }

// Should this process of the user be paused while the user interacts? cpuShare: fraction of one core it used in the
// last slice; lastTargetAt: when it last qualified (a paused process uses no CPU, so it stays a target for a while).
static int guardWantsTarget(const char *comm, const char *path, double cpuShare, double now, double lastTargetAt) {
    if (!path[0] || guardIsGuiPath(path) || !strcmp(comm, "Dock") || !strcmp(comm, "uiguard")) return 0;
    return isTarget(comm) || cpuShare >= cpuThreshold || now - lastTargetAt < STICKY_SEC;
}

#define SCAN_EVERY 5                // full process scan every 5 slices (500 ms); the target list is reused in between
typedef struct { pid_t pid; uint64_t cpuNs; double seenAt, targetAt; int8_t never; } Proc;   // never: GUI / no path
static Proc procs[MAX_PROCS];
static pid_t stopped[MAX_PROCS];
static int nStopped;

static double nowSec(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}
static Proc *slot(pid_t pid) {      // open addressing; a slot not seen for 30 s is reused
    double now = nowSec();
    for (unsigned i = 0, h = (unsigned)pid * 2654435761u; i < MAX_PROCS; i++) {
        Proc *p = &procs[(h + i) % MAX_PROCS];
        if (p->pid == pid) return p;
        if (p->pid == 0 || now - p->seenAt > 30) { *p = (Proc){pid, 0, 0, -1e9, -1}; return p; }
    }
    return NULL;
}

// Returns the targets for the next slice. full: also sample CPU time (wallSec since the previous full scan) to find
// busy command-line tools; otherwise a cheap pass that only adds named compilers and keeps recent targets.
static int listTargets(pid_t *out, int max, double wallSec, int full) {
    static pid_t pids[16384];
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    int n = proc_listallpids(pids, sizeof pids), k = 0;
    uid_t me = getuid(); pid_t self = getpid(); double now = nowSec();
    for (int i = 0; i < n && k < max; i++) {
        struct proc_bsdshortinfo bsd;
        if (pids[i] == self || proc_pidinfo(pids[i], PROC_PIDT_SHORTBSDINFO, 0, &bsd, sizeof bsd) != sizeof bsd) continue;
        if (bsd.pbsi_uid != me) continue;
        Proc *p = slot(pids[i]);
        if (!p) continue;
        double share = 0;
        if (full) {
            struct proc_taskinfo ti;
            if (proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &ti, sizeof ti) != sizeof ti) continue;
            uint64_t cpuNs = (ti.pti_total_user + ti.pti_total_system) * tb.numer / tb.denom;
            share = p->seenAt > 0 && wallSec > 0 && cpuNs >= p->cpuNs ? (cpuNs - p->cpuNs) / 1e9 / wallSec : 0;
            p->cpuNs = cpuNs; p->seenAt = now;
        } else if (p->seenAt == 0) {
            p->seenAt = now;        // first seen in a cheap pass: no CPU baseline yet
        }
        if (p->never == 1) continue;
        // the path is read once per process, and only when it could be a target (proc_pidpath is the costly call)
        if (!isTarget(bsd.pbsi_comm) && share < cpuThreshold && now - p->targetAt >= STICKY_SEC) continue;
        char path[PROC_PIDPATHINFO_MAXSIZE] = "x";      // known non-GUI from an earlier read
        if (p->never == -1) {
            if (proc_pidpath(pids[i], path, sizeof path) <= 0) path[0] = 0;
            p->never = !path[0] || guardIsGuiPath(path);
        }
        if (guardWantsTarget(bsd.pbsi_comm, path, share, now, p->targetAt)) {
            if (share >= cpuThreshold || isTarget(bsd.pbsi_comm)) p->targetAt = now;
            out[k++] = pids[i];
        }
    }
    return k;
}
static void resumeAll(void) {
    for (int i = 0; i < nStopped; i++) kill(stopped[i], SIGCONT);
    nStopped = 0;
}
static void onSignal(int sig) { resumeAll(); _exit(128 + sig); }
// `kill -USR1 <pid>` prints counters since start to stdout (/tmp/uiguard.log under the LaunchAgent).
static volatile sig_atomic_t wantStats;
static void onStats(int sig) { (void)sig; wantStats = 1; }
// Resumes every stopped process of the user that is not an app (left over by a crash of a previous run).
static int releaseAll(void) {
    static pid_t pids[16384];
    int n = proc_listallpids(pids, sizeof pids), k = 0;
    for (int i = 0; i < n; i++) {
        struct proc_bsdinfo b;
        char path[PROC_PIDPATHINFO_MAXSIZE] = "";
        if (proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &b, sizeof b) != sizeof b || b.pbi_uid != getuid() || b.pbi_status != SSTOP) continue;
        if (proc_pidpath(pids[i], path, sizeof path) > 0 && guardIsGuiPath(path)) continue;
        kill(pids[i], SIGCONT); k++;
    }
    return k;
}

// seconds since the last pointer event or modifier key (Cmd-Tab, Ctrl-arrow), not plain typing
static double interactionIdle(void) {
    static const int types[] = {kCGEventMouseMoved, kCGEventLeftMouseDown, kCGEventLeftMouseDragged, kCGEventRightMouseDown,
                                kCGEventOtherMouseDown, kCGEventScrollWheel, kCGEventFlagsChanged, 18, 29, 30, 31};
    double best = 1e9;
    for (unsigned i = 0; i < sizeof types / sizeof *types; i++) {
        double t = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateCombinedSessionState, (CGEventType)types[i]);
        if (t < best) best = t;
    }
    return best;
}

static int selftest(void) {
    int ok = guardStopMs(0.2, 1.0, 70) == 70 && guardStopMs(0.99, 1.0, 70) == 70 && guardStopMs(1.0, 1.0, 70) == 0 &&
             guardStopMs(30, 1.0, 70) == 0 && guardStopMs(0.1, 1.0, 200) == 95 && guardStopMs(0.1, 1.0, -5) == 0 &&
             guardStopMs(-1, 1.0, 70) == 0 &&
             isTarget("clang") && isTarget("clang++") && isTarget("swift-frontend") && isTarget("ld") && isTarget("go") &&
             !isTarget("ldapsearch") && !isTarget("gopls") && !isTarget("WindowServer") && !isTarget("Dock") &&
             !isTarget("node") && !isTarget("Claude Helper") && !isTarget("makewhatis");
    const char *node = "/usr/local/bin/node";
    int ok2 = guardIsGuiPath("/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge") &&
              !guardIsGuiPath("/Applications/Xcode.app/Contents/Developer/Toolchains/X/usr/bin/clang") &&
              guardWantsTarget("node", node, 0.9, 10, -100) &&              // busy CLI tool
              !guardWantsTarget("node", node, 0.1, 10, -100) &&             // idle CLI tool
              guardWantsTarget("node", node, 0.0, 10, 7) &&                 // was a target 3 s ago (paused = no CPU)
              !guardWantsTarget("node", node, 0.0, 10, 4) &&                // 6 s ago: forgotten
              !guardWantsTarget("node", "/X/Code Helper.app/Contents/MacOS/node", 0.9, 10, -100) &&   // GUI helper
              !guardWantsTarget("Dock", "/System/Library/CoreServices/Dock.app/Contents/MacOS/Dock", 1, 10, 7) &&
              !guardWantsTarget("uiguard", "/Users/x/tools/uiguard", 1, 10, -100) &&
              guardWantsTarget("clang", "/usr/bin/clang", 0, 10, -100) &&   // known compiler, even before it uses CPU
              !guardWantsTarget("x", "", 0.9, 10, -100);                    // unreadable path
    // Frequency-driven pause (0.4): only while interacting, only as much as keeps the cores at >= target MHz.
    int ok3 = guardAdaptStop(0, 2500, 3000, 1, 80) == 10 &&        // below target: pause 10 ms more per step
              guardAdaptStop(40, 2500, 3000, 1, 80) == 50 &&
              guardAdaptStop(80, 2500, 3000, 1, 80) == 80 &&       // capped (work keeps at least 20%)
              guardAdaptStop(40, 3100, 3000, 1, 80) == 40 &&       // just above target: hold (hysteresis)
              guardAdaptStop(40, 3300, 3000, 1, 80) == 30 &&       // well above target: pause less
              guardAdaptStop(0, 3300, 3000, 1, 80) == 0 &&
              guardAdaptStop(60, 2500, 3000, 0, 80) == 0 &&        // not interacting: release at once
              guardAdaptStop(60, -1, 3000, 1, 80) == 0;            // frequency unknown: never pause
    // A failed read (the Power Gadget driver returns 0xe00002bc now and then, about 1 read in 5 under launchd) keeps the
    // last good clock for up to 1 s instead of dropping the pause to 0; only longer gaps count as "unknown".
    int ok4 = guardHeldMHz(2900, 2500, 0.4) == 2900 &&     // fresh read wins
              guardHeldMHz(-1, 2500, 0.4) == 2500 &&       // failed read: last good one, 0.4 s old
              guardHeldMHz(-1, 2500, 1.2) == -1 &&         // failed for over a second: unknown
              guardHeldMHz(-1, -1, 0.2) == -1;
    ok = ok && ok2 && ok3 && ok4;
    printf(ok ? "ALL PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    int maxStopMs = 80, force = 0, verbose = 0;
    double hold = 1.0, targetMHz = 3000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) return selftest();
        if (!strcmp(argv[i], "--release")) { printf("resumed %d\n", releaseAll()); return 0; }
        if (!strcmp(argv[i], "-f")) force = 1;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) maxStopMs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) targetMHz = atof(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i + 1 < argc) hold = atof(argv[++i]);
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) cpuThreshold = atof(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc && nameCount() < MAX_NAMES - 1) names[nameCount()] = argv[++i];
        else { fprintf(stderr, "usage: uiguard [-t targetMHz] [-s maxStopMs] [-h holdSec] [-c cpuShare] [-n name]... [-f] [-v] | --selftest | --release\n"); return 2; }
    }
    signal(SIGINT, onSignal); signal(SIGTERM, onSignal); signal(SIGHUP, onSignal); signal(SIGUSR1, onStats);
    atexit(resumeAll);
    int leftover = releaseAll();
    if (verbose || leftover) { printf("uiguard started, resumed %d leftover stopped processes\n", leftover); fflush(stdout); }
    int wasActive = 0, nTargets = 0, ms = 0;
    unsigned long readsOk = 0, readsFailed = 0, slicesInteracting = 0, slicesPaused = 0, pausedMs = 0, slicesBelow = 0;
    double startedAt = nowSec();
    static pid_t targets[MAX_PROCS];
    double last = nowSec(), mhz = -1, goodMHz = -1, goodAt = 0;
    coreMHz();
    for (unsigned slice = 0;; slice++) {
        int interacting = guardStopMs(force ? 0 : interactionIdle(), hold, 1) > 0;
        if (slice % 2 == 0) {           // one 200 ms step
            double fresh = coreMHz(), now = nowSec();
            if (fresh >= 0) { goodMHz = fresh; goodAt = now; readsOk++; } else readsFailed++;
            mhz = guardHeldMHz(fresh, goodMHz, now - goodAt);
            ms = guardAdaptStop(ms, mhz, targetMHz, interacting, maxStopMs);
        } else if (!interacting) {
            ms = 0;
        }
        if (verbose && (ms > 0) != wasActive) { printf(ms ? "throttling (%.0f MHz)\n" : "released (%.0f MHz)\n", mhz); fflush(stdout); }
        if (ms > 0 && !wasActive) slice = 0;         // interaction just started: scan now
        wasActive = ms > 0;
        if (slice % SCAN_EVERY == 0) {
            // While paused a target uses no CPU: shares are measured over wall time, so a paused target looks idle;
            // the 5 s sticky window keeps it a target.
            double now = nowSec();
            nTargets = listTargets(targets, MAX_PROCS, now - last, 1);
            last = now;
        } else if (ms > 0) {
            nTargets = listTargets(targets, MAX_PROCS, 0, 0);   // pick up compilers started since the last full scan
        }
        slicesInteracting += interacting;
        slicesBelow += interacting && mhz >= 0 && mhz < targetMHz;
        if (ms > 0) { slicesPaused++; pausedMs += (unsigned long)ms; }
        if (wantStats) {
            wantStats = 0;
            printf("stats %.0f s: reads ok %lu failed %lu | interacting %.0f s, below %.0f MHz %.0f s, pausing %.0f s"
                   " (avg %lu ms per slice) | last clock %.0f MHz, pause now %d ms, targets %d\n",
                   nowSec() - startedAt, readsOk, readsFailed, slicesInteracting * SLICE_MS / 1000.0, targetMHz,
                   slicesBelow * SLICE_MS / 1000.0, slicesPaused * SLICE_MS / 1000.0,
                   slicesPaused ? pausedMs / slicesPaused : 0, mhz, ms, nTargets);
            fflush(stdout);
        }
        if (ms == 0) { usleep(SLICE_MS * 1000); continue; }
        nStopped = nTargets;
        memcpy(stopped, targets, sizeof(pid_t) * nTargets);
        for (int i = 0; i < nStopped; i++) kill(stopped[i], SIGSTOP);
        usleep(ms * 1000);
        resumeAll();
        usleep((SLICE_MS - ms) * 1000);
    }
}
