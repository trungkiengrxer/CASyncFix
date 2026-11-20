# CASyncFix

> [!IMPORTANT]
> **Built for macOS 26.6 Tahoe, build 25G72, Intel (x86_64) only.**
> The SkyLight patches use fixed addresses of the 25G72 dyld shared cache. On any other build (26.5, 26.7, 27, a
> different 26.6 build) they find no match and stay off, so the kext does nothing harmful, but it also brings little
> benefit. Check your build with `sw_vers -buildVersion` before installing, and keep a working EFI before updating macOS.

A [Lilu](https://github.com/acidanthera/Lilu) plugin that removes animation stutter in WindowServer on Intel
hackintoshes with an Intel iGPU (developed on a Dell Precision 5540, UHD 630, 60 Hz panel). Mission Control,
App Exposé, Genie/Scale minimize, Show Desktop, space swipes and live resize run at a steady frame rate in apps that
use Liquid Glass and vibrancy (Safari, Mail, Music, Weather...), which stock macOS drops to about 30 fps on these GPUs.

The kext patches code pages of the dyld shared cache in memory as they are paged in (a `_cs_validate_page` route,
the same mechanism as RestrictEvents and FeatureUnlock). Nothing on disk is modified, and removing the kext restores
stock behaviour on the next boot.

## What it patches

| Patch | Where | Matching | Default |
|---|---|---|---|
| Periodic CPU wait | QuartzCore `CA::OGL::new_metal_context` | byte pattern, any build that has it | on |
| Mid-frame flushes | QuartzCore `CA::OGL::Context::Context` | byte pattern, any build that has it | on (256) |
| Forced flatten during whole-window animations | SkyLight `ca_window_prep_ingestion_phase` + code cave | fixed 25G72 offsets | on |
| Window freeze under overload | SkyLight, same cave | fixed 25G72 offsets | off |
| Present ahead | SkyLight `CDDisplay::present_update` | fixed 25G72 offsets | off |

### 1. Periodic CPU wait (`finish_every`)

On GPUs without `MTLGPUFamilyApple3` (every Intel, AMD and NVIDIA Mac), QuartzCore sets
`MetalContext::finish_every = 128`, and `MetalContext::set_surface` calls `finish()` (`waitUntilCompleted`) every 128
render-target switches. AppKit vibrancy and Liquid Glass switch targets about 1300 times per second during Genie or
Exposé, so WindowServer's main thread blocks about 10 times per second (18.7 ms each on average) and drops to 30 fps.
The patch sets the immediate to 0, which disables the periodic wait:

```
41 F6 87 35 0E 00 00 04 75 0B 41 C7 87 38 0E 00 00 80 00 00 00
                                               ^^^^^^^^^^^ -> 00 00 00 00
```

### 2. Mid-frame flushes (`flush_every`)

`CA::OGL::Context::Context` sets `flush_every = 64`: the command buffer is committed every 64 render-target switches,
and on Intel each commit goes through the host GPU scheduler in the kernel (about 7% of WindowServer's main thread in
Mission Control). The default patch raises it to 256. `casyncflush=64` keeps the stock value, `0` commits only at the
end of the frame.

### 3. Forced flatten (SkyLight)

SkyLight never flattens a window that has WindowServer-aware backdrops (the frontmost window of any app with active
glass), and unflattens a window whose content changes. During Mission Control, Exposé, Genie, space swipes, window
drags, Show Desktop and live resize it therefore re-renders whole layer trees every frame, while windows without glass
are drawn from a cached flattened image. At the point where every flatten decision converges, a small blob copied into
free space after SkyLight's `__TEXT` forces normal-level windows that are being animated, moved or resized as a whole
to draw from their flattened image. Content changes are held back and rendered once when the window settles.

- Live resize keeps the old image until the resize pauses or ends (like the "show window contents while dragging"
  option off on Windows). `casyncresizehz=N` (1-60) refreshes at N Hz instead, `0` keeps stock resize.
- `casyncflathz=N` (0-60) refreshes window content N times per second during animations. Default 0 (frozen).

### 4. Window freeze and present ahead (off)

Experimental features kept for testing. The freeze measures window preparation time per frame and freezes the costliest
windows for 2 updates when the frame exceeds its budget (`casyncfreeze=2`, budget `casyncfreezems=N`, default 6 ms).
Present ahead presents each frame two refreshes after its CoreAnimation time (`casyncahead=1`, needs the freeze). On
the Intel path CoreDisplay ignores the present time, so it has no effect there.

## Requirements

- **macOS 26.6 (25G72)**, Intel CPU, Intel iGPU driving the display.
- Lilu 1.7.2 or newer.
- Recommended: [SharedCacheWire](https://github.com/trungkiengrxer/SharedCacheWire), loaded **last**.
  The shared cache is an APFS-compressed file: once a patched page is resident, an evicted neighbour in the same
  compression chunk can fail its hash check on page-in and the faulting process gets SIGBUS. SharedCacheWire wires
  those chunks. This applies to every shared-cache patcher (WhateverGreen, RestrictEvents...), not only this one.

## Install (OpenCore)

1. Download `CASyncFix-<version>-RELEASE.zip` from [Releases](../../releases) and copy `CASyncFix.kext` to `EFI/OC/Kexts`.
2. Add it to `config.plist > Kernel > Add` **after Lilu and before SharedCacheWire** (which must stay last).
3. Reboot.

To disable temporarily, add the boot-arg `-casyncoff`. To remove, set `Enabled = false` or delete the entry.

## Boot-args

| Boot-arg | Values | Default |
|---|---|---|
| `-casyncoff` | disable the plugin | |
| `casyncflush=N` | `flush_every`: 64 = stock (not patched), 0 = end-of-frame only | 256 |
| `casyncglass=N` | forced flatten: 1 on, 0 off | 1 |
| `casyncflathz=N` | content refresh during animations, 0-60 Hz, 0 = frozen | 0 |
| `casyncresizehz=N` | live resize: 0 = stock, 1-60 = refresh rate, 61 = old image until the resize pauses | 61 |
| `casyncfreeze=N` | window freeze: 0 off, 1 measure only, 2 on | 0 |
| `casyncfreezems=N` | freeze budget in ms (1-100) | 6 |
| `casyncahead=N` | present ahead: 1 on (needs `casyncfreeze=2`) | 0 |

## Verify

```bash
kextstat | grep -i casync
```

Check the patched bytes in memory (from a clone of this repository):

```bash
clang -O2 -framework QuartzCore -o tools/check_patch tools/check_patch.c && ./tools/check_patch
```

Expected: `finish_every immediate = 0 -> PATCHED`, `flush_every immediate = 256 -> PATCHED`, `freeze cave: blob present`
and `forced flatten: ... PATCHED`. The freeze and present-ahead lines say `stock` while those features are off. If
`forced flatten` says `stock` on 25G72, the kext did not load.

## Known costs

- While a window is forced flat, its content may lag behind until the animation ends (or refreshes at
  `casyncflathz`). Live resize shows the old image until you pause.
- Tools that read other processes' memory (ReportCrash, Instruments) can be killed when they touch a patched shared-cache
  page. This is common to all shared-cache patchers.

## Build

Needs Xcode, the Lilu SDK (the `Contents/Resources` folder of a DEBUG `Lilu.kext`) and
[MacKernelSDK](https://github.com/acidanthera/MacKernelSDK):

```bash
LILU_SDK=/path/to/Lilu.kext/Contents/Resources KERNEL_SDK=/path/to/MacKernelSDK ./build.sh
```

Output: `build/CASyncFix.kext` (and `build/EETOff.kext`, see below).

## Tests

```bash
./Tests/run_all.sh
```

Runs every host test on an Intel Mac. Tests that read the real shared cache or call the live SkyLight run only on
25G72; `freeze_sites_test` also needs the stock cache (boot once with `-casyncoff`), because file reads see the pages
a loaded CASyncFix has already patched.

## CI and releases

`.github/workflows/build.yml` runs the tests and builds on every push and pull request (artifacts attached to the run).
On a push to `main`, if the version in `CASyncFix/Info.plist` (and `build.sh`) has no release yet, it creates the tag
`vX.Y.Z` and publishes a GitHub Release with `CASyncFix-X.Y.Z-RELEASE.zip` and its dSYM. To release, bump the version
and push.

## EETOff (experimental, not released)

`EETOff/` is a separate plain IOKit kext that sets the Energy Efficient Turbo disable bit (bit 19 of `MSR_POWER_CTL`)
on Intel CPUs with CPUID family 6, model 0x8E, 0x9E, 0xA5 or 0xA6 (Kaby Lake to Comet Lake). It reads the MCHBAR MMIO
copy of the package power limits, and with `-eetmmiosync` raises it to match the MSR limits (`-eetnommio` skips MMIO). Version 0.3.0 caused a kernel panic at boot; 0.3.1 fixes the cause but has not been boot-tested. It
is built by CI only to keep it compiling and is not part of the releases. Use at your own risk.

## Layout

```
CASyncFix/           kext source; freeze_blob/ is the position-independent code copied into SkyLight
EETOff/              experimental EET kext
Tests/               host tests (run_all.sh)
tools/               measurement and inspection tools (check_patch, frame pacing scripts...)
build.sh             builds both kexts
```

## Disclaimer

This patches Apple system code in memory. It is provided as is, without warranty. Keep a bootable fallback EFI.
