// Host test for the present-time code in the freeze blob (CASyncFix/freeze_blob/blob.c, FZ_HOST_TEST mode):
// fz_present_target returns S + 2 refreshes - 0.5 ms and, in the probe build, logs one os_log record per present with
// the exact buffer layout SkyLight uses for its "*** Solver: Testing proposed tile addition ..." message.
//
// Run from the repository root:
//   clang -std=c11 -DFZ_HOST_TEST -DFZ_TEST_PRESENT -DFZ_PROBE=1 -DFZ_TABLE_ADDR=0x580000000000ULL Tests/probe_test.c -o /tmp/fz_probe && /tmp/fz_probe

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static double fakeNow = 100.25, fakeVBL[8];
const void *fz_test_default_log = (const void *)0x1234;
static int nextCalls, lastDisplay, logCalls;
static uint8_t logBuf[64];
static unsigned logSize;
static const char *logFmt;
static int logType;

double fz_test_now(void) { return fakeNow; }
double fz_test_nextvbl(int display) { nextCalls++; lastDisplay = display; return fakeVBL[display & 7]; }
void fz_test_log(const void *dso, const void *log, int type, const char *fmt, const uint8_t *buf, unsigned size) {
    (void)dso; (void)log;
    logCalls++; logType = type; logFmt = fmt; logSize = size;
    memcpy(logBuf, buf, size < sizeof logBuf ? size : sizeof logBuf);
}

#include "../CASyncFix/freeze_blob/blob.c"

static int failures;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); failures += !ok; }
static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static uint64_t rdq(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

// A fake CDDisplay: vtable slot 0x30 (display_number) returns 3.
static int fakeDisplayNumber(void *self) { (void)self; return 3; }
static void *fakeVtable[16];

int main(void) {
    const double ahead = 2.0 / 60.0 - 0.0005;

    // present_target_is_S_plus_two_refreshes
    double t = fz_present_target(374, 200.0);
    check(t > 200.0 + ahead - 1e-12 && t < 200.0 + ahead + 1e-12, "present_target: S + 2/60 - 0.0005");

    // probe_logs_one_record_with_solver_layout: 00 05, then 5 x {00 08 <8 bytes>}, 52 bytes, default type
    check(logCalls == 1 && logSize == 0x34 && logType == 0, "probe: one 52-byte default-level record");
    check(logBuf[0] == 0x00 && logBuf[1] == 0x05, "probe: header flags 0, 5 arguments");
    int itemsOk = 1;
    for (int i = 0; i < 5; i++) itemsOk &= logBuf[2 + 10 * i] == 0x00 && logBuf[3 + 10 * i] == 0x08;
    check(itemsOk, "probe: every argument is a public 8-byte scalar");
    check(rdq(logBuf + 4) != 0, "probe: arg0 (llu) = mach_absolute_time");
    check(rdd(logBuf + 14) == 374.0, "probe: arg1 = site");
    check(rdd(logBuf + 24) == fakeNow, "probe: arg2 = now");
    check(rdd(logBuf + 34) == 200.0, "probe: arg3 = S");
    check(rdd(logBuf + 44) == t, "probe: arg4 = present target");
    check(logFmt == FZ_PROBE_FORMAT, "probe: borrowed SkyLight format string");

    // present_388_reads_S_of_this_display: r14 = CDDisplay; display_number via vtable slot 0x30 -> GetNextVBL(display)
    fakeVtable[0x30 / 8] = (void *)fakeDisplayNumber;
    struct { void **vt; } obj = {fakeVtable};
    fakeVBL[3] = 300.5;
    double t388 = fz_present388(&obj);
    check(nextCalls == 1 && lastDisplay == 3, "present388: WSDisplayScheduleGetNextVBL(display_number())");
    check(t388 > 300.5 + ahead - 1e-12 && t388 < 300.5 + ahead + 1e-12, "present388: S + 2/60 - 0.0005");
    check(logCalls == 2 && rdd(logBuf + 14) == 388.0 && rdd(logBuf + 34) == 300.5, "present388: logged as site 388");

    // null_default_log_skips_logging: SkyLight's default_log not set up yet -> no os_log call, target unchanged
    fz_test_default_log = 0;
    double tn = fz_present_target(374, 10.0);
    check(logCalls == 2 && tn > 10.0 + ahead - 1e-12 && tn < 10.0 + ahead + 1e-12, "probe: null default_log -> no log");

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures != 0;
}
