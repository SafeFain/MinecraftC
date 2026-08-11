# MinecraftC

MinecraftC 是一款使用 C++17、SDL3、OpenGL 和 Vulkan 构建的体素沙盒游戏。
桌面 OpenGL 使用 3.3 Core，Android OpenGL 使用 ES 3.0；Linux、Windows、
macOS 和 Android 构建同时包含 Vulkan 与 OpenGL 完整游戏渲染后端，iOS
通过 MoltenVK 提供 Vulkan-only 客户端。游戏提供可无限加载的确定性
世界、创造/生存/旁观模式、完整昼夜与天气系统，以及中英文界面。

当前版本由仓库根目录的 `VERSION` 文件定义；CMake、Android Gradle 和发布 CI
均从该文件读取。

## 功能概览

- Y=-64..319 的无限世界；同一种子稳定生成地形、生物群系、洞穴、矿物、植被与树木。
- 20 种生物群系、花卉森林、向日葵平原、七种植被/树形和移动的 3D 方块云。
- 异步区域生成、距离优先区块流送、贪心网格、环境遮蔽、动态光照与透明材质。
- 创造、生存和旁观模式；支持采集、合成、熔炼、容器、种植、战斗和死亡掉落。
- 水与岩浆流动、流体固化、点火、连锁 TNT 爆炸、晴雨雷暴和雪地降雪。
- 可持久化的世界、玩家、实体、天气、容器、客户端设置和出生点区块缓存。
- JSON 驱动的方块材质、134 个物品图标，以及确定性的 16×16 像素资产管线。
- 键鼠、标准手柄和原生多点触控输入；完整英语与简体中文界面。

## 快速开始

### Linux

需要支持 C++17 的编译器、CMake 3.16+、OpenGL 3.3、Vulkan 和图形系统开发头文件。
以 Debian/Ubuntu 为例：

```bash
sudo apt install build-essential cmake git libglm-dev libgl1-mesa-dev libvulkan-dev xorg-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols extra-cmake-modules pkg-config

cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local -j2
./build-local/minecraftc
```

Vulkan 后端使用 VMA 管理 GPU 内存，支持完整游戏世界、UI、天空、
云、粒子、透明 Chunk、选择框和带蒙皮动画的 glTF 实体模型。通用
`IRenderDevice` 纹理网格与生产 Chunk 的隔离回归场景也可分别运行：

```bash
./build-local/minecraftc --renderer=opengl-demo
./build-local/minecraftc --renderer=vulkan-demo
./build-local/minecraftc --renderer=vulkan-textured-demo
```

Linux 和 Windows 构建必须安装 Vulkan 开发环境；OpenGL 后端仍会同时构建，
并在 Vulkan 初始化失败时作为回退。所有受支持平台现在都默认使用 Vulkan，
也可用 `--renderer=opengl` 明确选择 OpenGL。

未指定 renderer 时所有平台都启动 Vulkan；除 Vulkan-only 的 iOS 外，Vulkan
初始化失败会回退相应平台的 OpenGL 后端。完整游戏代码依赖
后端无关的 `IGameRenderer`，OpenGL 与 Vulkan 均声明完整 gameplay 能力。

Vulkan GLSL 与预编译 SPIR-V 位于 `assets/shaders/vulkan/`。普通构建不要求
安装 `glslc`；修改着色器后可使用以下命令重新生成并检查文件：

```bash
python3 tools/vulkan_shaders.py --root . --generate
python3 tools/vulkan_shaders.py --root . --check
```

安装到独立目录：

```bash
cmake --install build-local --prefix ./install-local
./install-local/bin/minecraftc
```

Fedora 对应软件包包括 `gcc-c++ cmake git glm-devel mesa-libGL-devel
vulkan-loader-devel` 及 X11/Wayland 开发包；Arch Linux 对应 `base-devel cmake
git glm mesa vulkan-headers vulkan-icd-loader` 及 X11/Wayland 开发包。

### Windows

安装 Visual Studio 2022 或更高版本并勾选“使用 C++ 的桌面开发”，同时安装
CMake、Git 和 LunarG Vulkan SDK 1.4.350.0 或更新版本。在 PowerShell 中运行：

```powershell
cmake -S . -B build-local -DMINECRAFTC_FETCH_DEPENDENCIES=ON
cmake --build build-local --config Release --parallel 2
cmake --install build-local --config Release --prefix install-local
.\install-local\bin\minecraftc.exe
.\install-local\bin\minecraftc.exe --renderer=vulkan
```

Windows 构建始终同时包含 Vulkan 和 OpenGL，不能关闭 Vulkan。运行 Vulkan
需要显卡驱动提供系统 Vulkan loader；未指定 renderer 时默认使用 Vulkan，
初始化失败则自动回退 OpenGL。

### macOS

要求 macOS 11 或更新系统。安装 Xcode Command Line Tools、CMake、Git 和
GLM，并下载官方 MoltenVK 1.4.1 `MoltenVK-macos.tar`：

```bash
xcode-select --install
brew install cmake git glm

cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release \
  -DMINECRAFTC_MOLTENVK_ROOT=/path/to/extracted/MoltenVK
cmake --build build-local -j2
./build-local/minecraftc
```

macOS 构建始终同时包含 Vulkan 和 OpenGL，不能关闭 Vulkan。新设置默认
Vulkan，初始化失败时自动回退 OpenGL。便携发布包已包含 MoltenVK，不要求最终
用户安装 Vulkan SDK。构建 Intel/Apple Silicon 通用包时添加
`-DMINECRAFTC_FETCH_DEPENDENCIES=ON -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`。

### iOS

iOS 14 或更新系统支持 iPhone 与 iPad，应用固定横屏并且只包含 Vulkan 完整
游戏后端。Vulkan 通过静态链接的 MoltenVK 1.4.1 转换为 Metal；没有 OpenGL
代码、后端切换或启动回退。构建需要完整 Xcode、CMake 3.28+，以及官方
`MoltenVK-all.tar`：

```bash
cmake -S . -B build-ios-simulator -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DMINECRAFTC_FETCH_DEPENDENCIES=ON \
  -DMINECRAFTC_ENABLE_OPENGL=OFF \
  -DMINECRAFTC_ENABLE_VULKAN=ON \
  -DMINECRAFTC_MOLTENVK_ROOT=/path/to/extracted/MoltenVK
cmake --build build-ios-simulator --config Release --parallel 2
```

真机使用 `iphoneos` 与 `arm64` 重新配置。生成的工程默认关闭代码签名；安装到
真机或提交 App Store 前需要在 Xcode 中设置开发团队、签名证书和描述文件。
完整命令、模拟器安装方式与产物说明见 [ios/README.md](ios/README.md)。

### Android

要求 Android 10（API 29）或更新系统、arm64 和 OpenGL ES 3.0。APK 始终包含
Vulkan 1.0 与 OpenGL ES 3.0 后端：新安装默认尝试 Vulkan，设备不支持或初始化
失败时自动回退 OpenGL ES，用户也可在视频设置中切换后端并重启生效。
Vulkan 不是安装必需能力。构建工具版本：

- Android SDK Platform 35、Build Tools 35.0.0
- NDK 28.2.13676358、CMake 3.22.1
- JDK 17、Gradle 8.9 或兼容的新版本

```bash
gradle -p android assembleDebug
gradle -p android assembleRelease
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

Release APK 默认未签名，发布前必须使用发行者密钥签名。详细说明见
[android/README.md](android/README.md)。推送 `v*` 标签后，CI 会将 Android
APK、未签名 iOS 构建与 Linux、Windows、macOS 构建包一起发布。

SDL 3.4.10 默认由 CMake 按固定版本获取并静态构建。系统已安装兼容 SDL3 时可添加
`-DMINECRAFTC_USE_SYSTEM_SDL3=ON`；需要同时获取固定版本 GLM 时使用
`-DMINECRAFTC_FETCH_DEPENDENCIES=ON`。

## 操作方式

### 键盘与鼠标

| 操作 | 默认输入 |
| --- | --- |
| 移动 / 观察 | `WASD` / 鼠标 |
| 跳跃 / 创造模式飞行 | `Space` / 双击 `Space` |
| 潜行、下降或下潜 | `Shift` |
| 疾跑 | `Ctrl` |
| 攻击或破坏 / 使用或放置 | 鼠标左键 / 右键 |
| 切换快捷栏 | `1`–`9` 或滚轮 |
| 背包 / 命令 | `E` / `T` |
| 暂停、关闭或返回 | `Esc` |

键鼠绑定可在 Settings → Controls 中修改。

### 手柄

默认采用 Xbox 风格布局：左/右摇杆控制移动和视角，A 跳跃，B
潜行或返回，L3 疾跑，Y 打开背包，方向键上打开命令，RT 攻击，LT 使用，
LB/RB 切换快捷栏。可在独立手柄设置页调整绑定、死区、视角灵敏度、Y 轴反转
和震动强度。

### 触控

Linux、Windows 和 Android 可使用原生多点触控。左下摇杆控制移动，右侧区域
拖动视角，屏幕按钮提供跳跃、下潜、攻击、使用、背包、命令和暂停；摇杆推至
外圈会自动疾跑。Settings → Touch Controls 可调整输入模式、灵敏度、控件大小、
透明度和左右手布局。

Android 文本字段会调用系统软键盘；其他桌面平台的世界名、种子和命令输入依赖
物理键盘或系统输入法。

## 存档与命令

默认存档目录：

| 平台 | 路径 |
| --- | --- |
| Windows | `%APPDATA%\MinecraftC\saves` |
| macOS | `~/Library/Application Support/MinecraftC/saves` |
| Linux | `$XDG_DATA_HOME/minecraftc/saves`，未设置时为 `~/.local/share/minecraftc/saves` |
| Android | 应用私有数据目录 |

如果启动目录已存在 `saves/`，桌面版会优先使用该目录。`options.txt` 位于
存档目录，`minecraftc.log` 位于其上一级数据目录。存档格式为 v8，并兼容读取
v2–v7 桌面存档；当前世界生成版本为 v5。

允许作弊的世界支持：

```text
/gamemode 0|1|3
/tp x y z
/time set day|night
/weather clear|rain|thunder
```

无需打开窗口即可检查版本：

```bash
./build-local/minecraftc --version
```

## 开发与验证

完整回归测试：

```bash
ctest --test-dir build-local --output-on-failure
git diff --check
```

测试覆盖世界生成确定性和边界、渲染逻辑、玩家移动、生存规则、存档、实体、
光照、本地化、输入及完整生存进程。

### 纹理资产管线

方块和物品通过 `assets/textures/definitions/` 中的 JSON 引用逻辑材质名，
C++ 和着色器不硬编码 atlas 坐标。当前正式纹理种子为 `213785369`：

```bash
python3 tools/texture_generator.py --generate --validate --build-atlas \
  --build-items-atlas --build-entity-atlas --build-entity-skins \
  --seed 213785369 --output assets/textures/generated

cmake --build build-local --target texture_generator
```

生成器输出方块、物品和实体 atlas，并检查确定性、平铺边界、调色板、透明度和
物品注册表覆盖。完整目录约定和新材质流程见
[ASSET_PIPELINE.md](ASSET_PIPELINE.md)。

### glTF 实体模型

八种生物使用可复用的 glTF 2.0 GLB 模型、分层动画和 GPU 蒙皮。版本化 JSON
动作图支持覆盖/叠加层、遮罩、队列、过渡和游戏事件；敌对生物攻击伤害与动画
事件对齐。资源和再生成说明见
[assets/models/entities/README.md](assets/models/entities/README.md)，实现记录见
[docs/gltf-entity-model-engine.md](docs/gltf-entity-model-engine.md) 和
[docs/entity-animation-engine.md](docs/entity-animation-engine.md)。

## 许可证与第三方材料

除明确标注其他许可证的第三方组件与资产外，MinecraftC 采用
[GNU GPL v3.0 only](LICENSE) 发布。

第三方组件和资产继续适用各自许可证：

| 组件或资产 | 作者/维护者 | 许可证 | 位置 |
| --- | --- | --- | --- |
| SDL 3.4.10 | SDL contributors | Zlib | CMake FetchContent 构建目录 |
| MoltenVK 1.4.1 | KhronosGroup/MoltenVK contributors | Apache-2.0 | macOS 发布包 `libMoltenVK.dylib` 与 `licenses/` |
| Vulkan Memory Allocator 3.3.0 | AMD/GPUOpen contributors | MIT | `external/VulkanMemoryAllocator/` |
| cgltf 1.15 | jkuhlmann | MIT | `external/cgltf/` |
| nlohmann/json 3.12.0 | Niels Lohmann | MIT | `external/nlohmann/` |
| stb_truetype 1.26 | Sean Barrett 等贡献者 | MIT | `external/stb/stb_truetype.h` |
| Noto Sans CJK SC Regular | Google、Adobe 等贡献者 | SIL OFL 1.1 | `assets/fonts/noto/` |

依赖上游来源与校验值记录在各目录的 `UPSTREAM.md`。资产来源见
[ASSET_SOURCES.md](ASSET_SOURCES.md)，纹理许可见
[assets/textures/LICENSE.md](assets/textures/LICENSE.md)。导入第三方文件时应在
对应目录保留其许可证和来源说明。
