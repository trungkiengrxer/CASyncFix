// Raw report cadence of the I2C trackpad (SYNA2393, VID 1739 / PID 31251), read only, no seize.
// Build: cc -O2 -o tools/tpcadence tools/tpcadence.c -framework IOKit -framework CoreFoundation
// Run:   tools/tpcadence [seconds=10]   (swipe while it runs; the calling app needs Input Monitoring permission)
// Prints the gaps between consecutive reports within one touch (a pause >= 100 ms separates touches),
// using each report's kernel timestamp (when the driver finished the I2C read), not when userspace received it.
#include <IOKit/hid/IOHIDManager.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXN 200000
static double gaps[MAXN];
static int ngaps, nreports;
static uint64_t last;
static double ms_per_tick;

static void on_report(void *ctx, IOReturn r, void *sender, IOHIDReportType type, uint32_t id,
                      uint8_t *report, CFIndex len, uint64_t ts) {
    nreports++;
    if (last) {
        double d = (ts - last) * ms_per_tick;
        if (d < 100 && ngaps < MAXN) gaps[ngaps++] = d;
    }
    last = ts;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    double secs = argc > 1 ? atof(argv[1]) : 10;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    ms_per_tick = (double)tb.numer / tb.denom / 1e6;

    IOHIDManagerRef m = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    int vid = 1739, pid = 31251;
    CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vid), p = CFNumberCreate(NULL, kCFNumberIntType, &pid);
    const void *keys[] = {CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey)}, *vals[] = {v, p};
    CFDictionaryRef match = CFDictionaryCreate(NULL, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(m, match);
    IOHIDManagerRegisterInputReportWithTimeStampCallback(m, on_report, NULL);
    IOHIDManagerScheduleWithRunLoop(m, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOReturn r = IOHIDManagerOpen(m, kIOHIDOptionsTypeNone);
    if (r != kIOReturnSuccess) {
        fprintf(stderr, "IOHIDManagerOpen failed 0x%x (missing Input Monitoring permission?)\n", r);
        return 1;
    }
    fprintf(stderr, "Recording %.0f s, swipe now...\n", secs);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, secs, false);

    printf("reports: %d, gaps within a touch: %d\n", nreports, ngaps);
    if (!ngaps) return 0;
    qsort(gaps, ngaps, sizeof *gaps, cmp);
    int over = 0;
    for (int i = 0; i < ngaps; i++) over += gaps[i] > 16.7;
    printf("ms p10/p50/p90/p99/max: %.1f / %.1f / %.1f / %.1f / %.1f\n", gaps[ngaps / 10], gaps[ngaps / 2],
           gaps[ngaps * 9 / 10], gaps[ngaps * 99 / 100], gaps[ngaps - 1]);
    printf("gaps > 16.7 ms (a display refresh with no report): %d (%.1f%%)\n", over, 100.0 * over / ngaps);
    return 0;
}
