# Versioning and releases

The root `VERSION` file is the single version source. It uses
`X.Y.Z-alpha|beta|rc|release`, with an optional `.N` after alpha, beta, or rc
(for example `1.4.0-beta.2`). The core version can stay at `1.4.0` across any
number of commits; increase the channel iteration only when you want to identify
a new prerelease. Iterations run from 1 to 999. Existing unnumbered versions
remain supported; `release` does not take an iteration.

| VERSION | Runtime display | Release tag | GitHub release type |
|---|---|---|---|
| `1.4.0-beta.1` | `Beta.1-1.4.0` | `v1.4.0-beta.1` | Prerelease |
| `1.4.0-beta.2` | `Beta.2-1.4.0` | `v1.4.0-beta.2` | Prerelease |
| `1.4.0-rc.1` | `RC.1-1.4.0` | `v1.4.0-rc.1` | Prerelease |
| `1.4.0-release` | `Release-1.4.0` | `v1.4.0-release` | Release |

CMake, Android/iOS/macOS metadata, runtime output, package names, and release CI
all derive from this file. Branch pushes, pull requests, and manual workflow runs
build and test; desktop CI continues to run CTest. A `v*` tag push publishes all
platform packages only if the tag exactly matches `v<VERSION>` and every platform
job passes. Manual runs do not publish, including runs on a tag. Package filenames include the full version
(for example `MinecraftC-1.4.0-beta.2-linux-x86_64.tar.gz`).

Android versionCode and Apple bundle build numbers use
`(major * 10000 + minor * 100 + patch) * 10000 + channel * 1000 + iteration`,
where alpha/beta/rc/release are 1/2/3/4 and an omitted iteration is 0. This keeps
iterations and subsequent stages increasing and upgrades over the former codes.
Major is limited to 20, minor/patch to 99, and the final code must be at most
Android's 2100000000 limit. The Apple short version remains `X.Y.Z`.

Source of truth: [VERSION](../VERSION), [shared parser](../tools/project_version.cmake),
[CI workflow](../.github/workflows/cross-platform.yml), and
[Android version mapping](../android/app/build.gradle).

Platform signing and distribution: [Android](../android/README.md),
[iOS](../ios/README.md), [package instructions](../packaging/README-release.md).
