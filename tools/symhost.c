// Loads the frameworks WindowServer runs (SkyLight, QuartzCore, CoreDisplay, Metal...) and waits, so that
// `atos -p <this pid>` can symbolize WindowServer addresses: shared-cache addresses are identical in every process
// of the same boot. Used because atos cannot attach to WindowServer itself.
// Build: clang -O1 -o tools/symhost tools/symhost.c ; run: ./tools/symhost & atos -p $! <addr>...
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    const char *libs[] = {
        "/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight",
        "/System/Library/Frameworks/QuartzCore.framework/QuartzCore",
        "/System/Library/Frameworks/CoreDisplay.framework/CoreDisplay",
        "/System/Library/Frameworks/Metal.framework/Metal",
        "/System/Library/Frameworks/IOSurface.framework/IOSurface",
        "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics",
        "/System/Library/Frameworks/CoreImage.framework/CoreImage",
        "/System/Library/Frameworks/IOKit.framework/IOKit",
        "/System/Library/Extensions/AppleIntelKBLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelKBLGraphicsMTLDriver",
    };
    for (unsigned i = 0; i < sizeof libs / sizeof *libs; i++)
        if (!dlopen(libs[i], RTLD_LAZY)) fprintf(stderr, "skip %s\n", libs[i]);
    sleep(600);
    return 0;
}
