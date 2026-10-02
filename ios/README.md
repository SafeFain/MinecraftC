# MinecraftC for iOS

The iOS client supports iOS 14 or newer on arm64 iPhone and iPad devices. It is
landscape-only and builds the Vulkan renderer, statically linked to MoltenVK
1.4.1, consistently with every other supported platform.

## Versioning and releases

The root `VERSION` drives both CMake and CI. Alpha/beta/rc can carry `.N`
iterations from 1 to 999; `release` remains unnumbered. For example,
`1.4.0-beta.1` keeps `CFBundleShortVersionString` at `1.4.0` and sets
`CFBundleVersion` to `104002001`, using the same increasing numeric mapping as
Android. The full version remains in the runtime display and package filenames.
See the root [version rules](../README.md#versioning-and-releases) for the formula
and supported limits.

Only a pushed tag exactly matching `v<VERSION>` publishes the unsigned device
IPA and Simulator ZIP after all platform jobs pass. For `v1.4.0-beta.1`, the
files are `MinecraftC-1.4.0-beta.1-ios-arm64-unsigned.ipa` and
`MinecraftC-1.4.0-beta.1-ios-simulator-arm64.zip`. Alpha/beta/rc tags create GitHub
prereleases; `release` creates a normal release. Branch pushes and manual runs
build without publishing.

## Dependencies

- Full Xcode installation
- CMake 3.28 or newer
- MoltenVK 1.4.1 `MoltenVK-all.tar`, extracted without changing its layout

The path passed as `MINECRAFTC_MOLTENVK_ROOT` is the extracted top-level
`MoltenVK` directory containing `MoltenVK/`, `LICENSE`, and `Docs/`.

## Simulator build

```bash
cmake -S . -B build-ios-simulator -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DMINECRAFTC_FETCH_DEPENDENCIES=ON \
  -DMINECRAFTC_MOLTENVK_ROOT=/path/to/MoltenVK
cmake --build build-ios-simulator --config Release --parallel 2
```

The application is written to
`build-ios-simulator/Release-iphonesimulator/MinecraftC.app`. Install and launch
it with Xcode or `xcrun simctl install` and `xcrun simctl launch`.

## Device build and signing

Configure a separate directory with `-DCMAKE_OSX_SYSROOT=iphoneos` and
`-DCMAKE_OSX_ARCHITECTURES=arm64`. The generated project disables signing so CI
can create an unsigned IPA containing `Payload/MinecraftC.app`. The IPA is a
packaging container, not an installable signed application. For a local device
build, open the generated Xcode project, select the `minecraftc` target, choose
a development team, and enable automatic or manual signing. App Store/TestFlight
export is intentionally outside the unsigned release workflow.

The device IPA and Simulator ZIP are separate CI artifacts. An arm64 Simulator
binary is still marked `iOS-simulator` in its Mach-O load commands and cannot run
on a physical iPhone or iPad. Use only the `ios-arm64-unsigned.ipa` artifact for
device signing. CI inspects the Mach-O platform both before and after IPA
packaging to prevent a Simulator application from being published as a device
IPA.

The release uses statically linked MoltenVK. Install the signed IPA as a normal
iOS application so SDL can discover the exported `vkGetInstanceProcAddr` symbol
in the process image. Container launchers that copy an app into their own
Documents directory and `dlopen` its executable with local symbol visibility do
not provide the same process model and may fail while creating the Vulkan
window.

Release archives may have their nlist symbol table stripped by Xcode. CI checks
the dyld export information used by `dlsym`, rather than treating `nm` output as
the runtime export contract. The app link still forces
`_vkGetInstanceProcAddr` out of the static MoltenVK archive and marks it as an
exported symbol. The final Xcode application target owns both requirements via
`OTHER_LDFLAGS` and `ios/MinecraftC.exports`; they are not attached to an
intermediate renderer library. It also enables executable exports explicitly
through CMake's `ENABLE_EXPORTS` and Xcode's `LD_EXPORT_SYMBOLS`, preventing
Xcode from applying `-no_exported_symbols` to the final app. Release archives
use the `non-global` strip style: local symbols are removed, while the global
MoltenVK entry required by SDL remains available to `dlsym`.

Worlds, settings, and logs use the application preference directory. Assets and
the GPL/MoltenVK license texts are read-only Bundle resources.
