// Reads CPU temperature keys from the SMC (VirtualSMC / SMCProcessor on this machine), no root needed.
// Build: clang -O2 -framework IOKit -o tools/smctemp tools/smctemp.c
// Usage: ./tools/smctemp [key...]        default keys: TC0P TCXC TC0C TC1C TG0P
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <string.h>

typedef struct { char major, minor, build, reserved[1]; uint16_t release; } SMCVers;
typedef struct { uint16_t version, length; uint32_t cpuPLimit, gpuPLimit, memPLimit; } SMCPLimit;
typedef struct { uint32_t dataSize, dataType; char dataAttributes; } SMCKeyInfo;
typedef struct {
    uint32_t key; SMCVers vers; SMCPLimit pLimitData; SMCKeyInfo keyInfo;
    char result, status, data8; uint32_t data32; unsigned char bytes[32];
} SMCParam;

static io_connect_t conn;
static uint32_t k4(const char *s) { return (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | (uint32_t)s[3]; }
static int call(SMCParam *in, SMCParam *out) {
    size_t sz = sizeof *out;
    return IOConnectCallStructMethod(conn, 2, in, sizeof *in, out, &sz);
}
static int readKey(const char *key, double *v) {
    SMCParam in = {0}, out = {0};
    in.key = k4(key); in.data8 = 9;                         // get key info
    if (call(&in, &out) || out.result) return -1;
    uint32_t type = out.keyInfo.dataType, size = out.keyInfo.dataSize;
    in.keyInfo.dataSize = size; in.data8 = 5;               // read bytes
    memset(&out, 0, sizeof out);
    if (call(&in, &out) || out.result) return -1;
    if (type == k4("sp78")) *v = (int16_t)(out.bytes[0] << 8 | out.bytes[1]) / 256.0;
    else if (type == k4("flt ")) { float f; memcpy(&f, out.bytes, 4); *v = f; }
    else if (type == k4("ui8 ")) *v = out.bytes[0];
    else if (type == k4("ui16")) *v = out.bytes[0] << 8 | out.bytes[1];
    else return -2;
    return 0;
}
int main(int argc, char **argv) {
    io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!s || IOServiceOpen(s, mach_task_self(), 0, &conn)) { fprintf(stderr, "cannot open AppleSMC\n"); return 1; }
    const char *def[] = {"TC0P", "TCXC", "TC0C", "TC1C", "TG0P"};
    int n = argc > 1 ? argc - 1 : 5;
    for (int i = 0; i < n; i++) {
        const char *k = argc > 1 ? argv[i + 1] : def[i];
        double v;
        if (readKey(k, &v) == 0) printf("%s %.1f  ", k, v);
    }
    printf("\n");
    return 0;
}
