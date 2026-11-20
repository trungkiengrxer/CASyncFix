#!/bin/zsh
# Builds build/CASyncFix.kext (Lilu plugin) and build/EETOff.kext (plain IOKit) with the same compiler
# flags Xcode uses for the SharedCacheWire kext.
# Reuses the Lilu SDK and MacKernelSDK of ~/Desktop/SharedCacheWire (override with LILU_SDK / KERNEL_SDK).
set -e
cd "${0:A:h}"
LILU_SDK=${LILU_SDK:-$HOME/Desktop/SharedCacheWire/Lilu.kext/Contents/Resources}
KERNEL_SDK=${KERNEL_SDK:-$HOME/Desktop/SharedCacheWire/MacKernelSDK}
SDK=$(xcrun --sdk macosx --show-sdk-path)
rm -rf build

# build_kext <Name> <Version> <uses Lilu: 0|1> <c++ sources...>
build_kext() {
  local name=$1 version=$2 lilu=$3; shift 3
  local obj=build/obj/$name kext=build/$name.kext
  mkdir -p $obj $kext/Contents/MacOS
  local common=(-target x86_64-apple-macos10.14 -nostdinc -fno-builtin -fno-common -mkernel -Os -g
                -DMODULE_VERSION=$version -DPRODUCT_NAME=$name -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
                -I"$LILU_SDK" -I"$SDK/System/Library/Frameworks/Kernel.framework/PrivateHeaders" -I"$KERNEL_SDK/Headers"
                -Wall -Werror)
  local cxx=(-std=gnu++14 -fno-exceptions -fno-rtti -fno-sanitize=vptr -fapple-kext)
  local objs=($obj/kmod_info.o)

  clang "${common[@]}" -std=gnu11 -c $name/kmod_info.c -o $obj/kmod_info.o
  for src in "$@"; do
    clang++ "${common[@]}" "${cxx[@]}" -c $src -o $obj/${src:t:r}.o
    objs+=($obj/${src:t:r}.o)
  done
  if [ $lilu = 1 ]; then
    clang++ "${common[@]}" "${cxx[@]}" -Wno-error -c "$LILU_SDK/Library/plugin_start.cpp" -o $obj/plugin_start.o
    objs+=($obj/plugin_start.o)
  fi

  clang++ -target x86_64-apple-macos10.14 -nostdlib -Xlinker -kext -Xlinker -no_adhoc_codesign \
          -L"$KERNEL_SDK/Library/x86_64" "${objs[@]}" -lkmod -lcc_kext -o $kext/Contents/MacOS/$name
  cp $name/Info.plist $kext/Contents/Info.plist
  dsymutil $kext/Contents/MacOS/$name -o build/$name.kext.dSYM 2>/dev/null || true
  strip -x -T $kext/Contents/MacOS/$name
  plutil -lint $kext/Contents/Info.plist >/dev/null
  echo "built $kext"
}

./CASyncFix/freeze_blob/build_blob.sh
build_kext CASyncFix 0.8.2 1 CASyncFix/kern_start.cpp
build_kext EETOff 0.3.1 0 EETOff/kern_eetoff.cpp
