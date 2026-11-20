// Prints the static (unslid) shared-cache address of exported symbols: dlsym() minus the owning image's slide.
// Build: clang -O1 -o tools/staticaddr tools/staticaddr.c ; run: ./tools/staticaddr <symbol>...
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore", RTLD_LAZY);
    dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
    for (int i = 1; i < argc; i++) {
        void *p = dlsym(RTLD_DEFAULT, argv[i]);
        Dl_info info;
        if (!p || !dladdr(p, &info)) { printf("%-36s not found\n", argv[i]); continue; }
        intptr_t slide = 0;
        for (uint32_t k = 0; k < _dyld_image_count(); k++)
            if (_dyld_get_image_header(k) == info.dli_fbase) slide = _dyld_get_image_vmaddr_slide(k);
        printf("%-36s 0x%lx  (%s)\n", argv[i], (unsigned long)((intptr_t)p - slide), strrchr(info.dli_fname, '/') + 1);
    }
}
