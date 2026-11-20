// Sends WindowServer debug options through SkyLight's own path (SLSSetDebugOptions -> set_debug_options), the one
// Quartz Debug uses. State lives in WindowServer's memory only: the "*-default" modes restore it, and so does
// logging out or rebooting.
//
// WindowServer accepts this only from an unsandboxed client with the com.apple.private.CoreGraphics.debugging
// entitlement (works here because AMFI is off). Run it from a normal Terminal, not a sandboxed shell.
//
// Build:
//   clang -O2 -o tools/wsdebug tools/wsdebug.c -F/System/Library/PrivateFrameworks -framework SkyLight
//   codesign -f -s - --entitlements tools/wsdebug.entitlements tools/wsdebug
// Use: ./tools/wsdebug <mode>   (no argument lists the modes)
// Check: log show --last 1m --predicate 'eventMessage CONTAINS "[ Tooling ]"'
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int SLSSetDebugOptions(uint32_t options);

static const struct { const char *name; uint32_t code; const char *what; } modes[] = {
    // WSFlatteningPolicySetDebugOptions
    {"flatten-force", 0x80000083, "window flattening always on"},
    {"flatten-default", 0x800000a1, "stock flattening policy"},
    {"flatten-off", 0x800000a0, "window flattening off"},
    // WSSchedulerSetDebugOptions
    {"backoff-toggle", 0x8000035d, "flip the updater backoff (stock: on, max 2 ms)"},
    {"backoff-default", 0x8000035c, "updater backoff on with stock values"},
    // sWSEnablePresentAtTime: present_update+479 takes the timed CoreDisplay present only when it is 0; when it is 1
    // every frame goes through the untimed present (shown at the next vblank, the computed present time is ignored).
    {"pat-0", 0x80000160, "sWSEnablePresentAtTime = 0 (timed present path)"},
    {"pat-1", 0x80000161, "sWSEnablePresentAtTime = 1 (untimed present path)"},
    {"greedy-force", 0x80000180, "force_greedy_displays = 1"},
    {"greedy-noforce", 0x80000181, "force_greedy_displays = 0 (stock)"},
    {"greedy-mode1", 0x80000183, "greedy_scheduler_mode = 1 (stock)"},
    {"greedy-mode2", 0x80000182, "greedy_scheduler_mode = 2"},
    {"greedy-mode0", 0x80000184, "greedy_scheduler_mode = 0"},
    // set_debug_options jump table (index = code - 0x80000001): read-only state dumps to /tmp/WindowServer.*.out
    {"dump-flatten", 0x80000105, "WSDumpFlatteningInfo -> /tmp/WindowServer.finfo.out (per-window flatten state + reason)"},
};

extern int SLSSetDebugOptionsData(uint32_t options, const void *data, int size);

int main(int argc, char **argv) {
    // CoreDisplayManager::set_debug_options 0x80000104 + float: cgdebug::updater_wakeup_delay (s, stock 0.0005).
    // next_wakeup_time = next VBL + delay; present_update target = next VBL - delay. Negative = render ahead.
    if (argc == 3 && strcmp(argv[1], "wakeup-delay") == 0) {
        float d = 0;
        if (sscanf(argv[2], "%f", &d) != 1 || d < -0.034f || d > 0.017f) { fprintf(stderr, "delay out of range\n"); return 2; }
        int err = SLSSetDebugOptionsData(0x80000104, &d, sizeof d);
        printf("SLSSetDebugOptionsData(0x80000104, updater_wakeup_delay=%g s) -> %d\n", d, err);
        return err != 0;
    }
    // WSSetConsistentEventDelivery (set_debug_options reads a float from the data pointer, so data is mandatory):
    //   0x80000230 + f: gEventDeliveryType = consistent (vsync-resampled) delivery, gEventBufferTime = clamp(f, 0, 1)
    //   0x80000231: gEventDeliveryType stock value on this machine;  0x80000233: other delivery type
    //   0x80000234 + f: gEventLatency = f;  0x80000236 + f: event_interpolation_always_on_pointer = (f != 0)
    //   0x80000232 + f: event info logging (f >= 1)
    if (argc == 4 && strcmp(argv[1], "event") == 0) {
        unsigned code = 0; float f = 0;
        if (sscanf(argv[2], "%x", &code) != 1 || code < 0x80000230 || code > 0x80000236 || sscanf(argv[3], "%f", &f) != 1) {
            fprintf(stderr, "event <0x80000230..0x80000236> <float>\n"); return 2;
        }
        int err = SLSSetDebugOptionsData(code, &f, sizeof f);
        printf("SLSSetDebugOptionsData(0x%08x, %g) -> %d\n", code, f, err);
        return err != 0;
    }
    // dump-flatten <u32>: same dump with a 4-byte argument (set_debug_options passes it to WSDumpFlatteningInfo).
    if (argc == 3 && strcmp(argv[1], "dump-flatten") == 0) {
        uint32_t v = (uint32_t)strtoul(argv[2], 0, 0);
        int err = SLSSetDebugOptionsData(0x80000105, &v, sizeof v);
        printf("SLSSetDebugOptionsData(0x80000105, %u) -> %d\n", v, err);
        return err != 0;
    }
    for (unsigned i = 0; argc == 2 && i < sizeof modes / sizeof *modes; i++) {
        if (strcmp(argv[1], modes[i].name) != 0) continue;
        int err = SLSSetDebugOptions(modes[i].code);
        printf("SLSSetDebugOptions(0x%08x) [%s: %s] -> %d\n", modes[i].code, modes[i].name, modes[i].what, err);
        return err != 0;
    }
    fprintf(stderr, "usage: %s <mode> | wakeup-delay <seconds, stock 0.0005>\n", argv[0]);
    for (unsigned i = 0; i < sizeof modes / sizeof *modes; i++)
        fprintf(stderr, "  %-16s 0x%08x  %s\n", modes[i].name, modes[i].code, modes[i].what);
    return 2;
}
