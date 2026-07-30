# MinecraftC

MinecraftC 是一个使用 C++17 与 OpenGL 3.3 编写的体素沙盒游戏。项目提供 Y=-64..319 的可无限加载确定性世界、20 种生物群系、崎岖地形、洞穴与矿物、昼夜、动态光照和晴雨雷暴天气，并支持创造、生存与旁观模式。新建世界会预生成并缓存出生点附近区块，同时显示加载进度。

生存模式包含采集、挖掘、合成、熔炼、箱子、种植、战斗和死亡掉落，并支持流动的水与岩浆、玻璃、打火石及可连锁爆炸的 TNT。世界包含多种花卉、繁花森林、向日葵草原和移动的 3D 方块云。创造模式物品栏可访问全部注册物品并支持飞行；旁观模式提供无碰撞探索。世界、玩家、实体、天气及容器状态均可持久保存。

## 主要特性

- 确定性种子控制地形、生物群系、洞穴、矿物、植被和树木，并保证区块边界一致。
- 异步区域生成、距离优先区块流送、贪心网格、环境遮蔽、透明材质和动态昼夜照明。
- 生存、创造和旁观模式，以及可持久化的多世界选择、游戏规则和客户端设置。
- 晴天、下雨和雷暴循环；雪原中降水表现为雪，雷暴可产生闪电。
- 七级水与岩浆流动、流体固化，以及包含伤害、击退、掉落、粒子和音效的 TNT 爆炸。
- 繁花森林、向日葵草原、多种花卉，以及确定性移动的无碰撞 3D 方块云。
- 生物战斗、碰撞击退、受击反馈、日照燃烧及昼夜相关敌对行为。
- JSON 驱动的方块材质与全部 126 个注册物品图标，以及确定性程序化 16×16 像素资产管线。

## 安装与构建

需要支持 C++17 的编译器、CMake 3.16+ 和 OpenGL 3.3+。以下命令均在项目
根目录执行。

### Linux 发行版

安装依赖：

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake libglfw3-dev libglm-dev libgl1-mesa-dev

# Fedora
sudo dnf install gcc-c++ cmake glfw-devel glm-devel mesa-libGL-devel

# Arch Linux
sudo pacman -S --needed base-devel cmake glfw-x11 glm mesa
```

构建、安装并运行：

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local -j2
cmake --install build-local --prefix ./install-local
./install-local/bin/minecraftc
```

若正在开发程序，亦可：

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local -j2
./build-local/minecraftc
```

### macOS

先安装 Xcode Command Line Tools 与 Homebrew 依赖：

```bash
xcode-select --install
brew install cmake glfw glm
```

构建当前 Mac 的原生版本：

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local -j2
cmake --install build-local --prefix ./install-local
./install-local/bin/minecraftc
```

构建 Intel 与 Apple Silicon 通用版本时，改用源码依赖并添加
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`。

### Windows

安装 Visual Studio 2022 或更高版本并勾选“使用 C++ 的桌面开发”，同时安装
CMake 和 Git。然后在 PowerShell 中使用项目自带的固定版本源码依赖：

```powershell
cmake -S . -B build-local -DMINECRAFTC_FETCH_DEPENDENCIES=ON
cmake --build build-local --config Release --parallel 2
cmake --install build-local --config Release --prefix install-local
.\install-local\bin\minecraftc.exe
```

### 自动构建与发行

GitHub Actions 会在 Linux、Windows 和 macOS（arm64/x86-64 通用版）上构建、
测试、检查安装资源并生成带顶层 `MinecraftC-1.1.1` 目录的便携压缩包。包内程序、
`assets/`、启动脚本、使用说明和第三方许可证位于同一级目录；Windows 便携包
使用静态 MSVC 运行库。推送 `v*` 标签会自动发布三个平台的安装包及
`SHA256SUMS`。

无需打开图形窗口即可检查版本：

```bash
minecraftc --version
```

## 默认操作

- `WASD`：移动
- 鼠标：观察方向
- `Space`：跳跃；创造模式双击切换飞行
- `Shift`：潜行、下降或水中下潜
- `Ctrl`：疾跑
- 鼠标左键：攻击或破坏方块
- 鼠标右键：使用或放置方块
- `1`–`9` / 鼠标滚轮：切换快捷栏
- `E`：打开或关闭背包
- `T`：打开命令输入
- `Esc`：暂停、关闭界面或返回

键盘、鼠标和滚轮操作均可在 Settings → Controls 中重新绑定。存档默认位于：

- Windows：`%APPDATA%\MinecraftC\saves`
- macOS：`~/Library/Application Support/MinecraftC/saves`
- Linux：`$XDG_DATA_HOME/minecraftc/saves`，未设置时为
  `~/.local/share/minecraftc/saves`

如果启动目录已经存在 `saves/`，程序会优先使用该目录。`options.txt` 位于存档
目录，`minecraftc.log` 位于其上一级数据目录。新存档使用固定 little-endian 的
格式 v8，并兼容读取
现有桌面平台生成的 v2–v7 存档；新建世界使用世界生成版本 v5。默认世界渲染
距离为 8 区块；云渲染距离可在设置中选择 64–512 方块，默认为 192 方块。
旧版 v5 基础区块缓存会按缓存修订号失效并重生成，玩家方块覆盖仍会保留。

允许作弊的世界支持以下命令：

```text
/gamemode 0|1|3
/tp x y z
/time set day|night
/weather clear|rain|thunder
```

## 纹理资产管线

方块和物品通过 `assets/textures/definitions/` 下的 JSON 文件引用逻辑材质名，C++ 与着色器不硬编码具体 atlas 坐标。`block_texture`、`item_sprite` 和 `block_item_icon` 三种生成器分别负责可平铺方块纹理、透明背景物品精灵，以及使用方块顶面和侧面自动合成的立体背包图标。具体物品 ID 不硬编码在 Python 中。

方块继续使用原有 `atlas.png` / `atlas.json`；全部 126 个当前注册的非空物品使用独立的 `items_atlas.png` / `items_atlas.json`。物品 atlas 采用最近邻采样，图标保持 16×16、二值 alpha、清晰像素轮廓和左上方受光。资源选择顺序为人工覆盖、自动生成、旧图标、缺失资源图标。运行时仍保留旧资源和程序化绘制作为兼容回退。

当前正式纹理种子为 `213785369`。从项目根目录重新生成、校验并打包：

```bash
python3 tools/texture_generator.py --generate --validate --build-atlas \
  --build-items-atlas --build-entity-atlas --build-entity-skins \
  --seed 213785369 --output assets/textures/generated

# 使用当前默认种子的等价 CMake 目标
cmake --build build-local --target texture_generator
```

方块生成器采用有限调色板、二值透明度、最近邻像素输出和环面坐标结构，并检测长直线、中心十字、大型矩形色块、周期性、频率分布及矿簇连通性。物品图标不做无缝平铺校验；工具由轮廓、手柄、工作部件和高光层构成，其他模板覆盖材料、食物、弓盾、护甲、箭矢、作物和树苗等全部现有物品。生成 8 组方块纹理开发候选总览：

```bash
python3 tools/texture_generator.py --generate --validate --build-atlas \
  --build-items-atlas \
  --seed 213785369 --candidate-count 8 --contact-sheet \
  --output assets/textures/generated
```

可重复使用 `--local-seed MATERIAL=SEED` 为单一材质选择局部种子，而不改变其他材质。候选总览不进入正式 atlas。完整的目录约定、新材质添加步骤和校验说明参见 [`ASSET_PIPELINE.md`](ASSET_PIPELINE.md)。

## 运行测试

```bash
ctest --test-dir build-local --output-on-failure
```

测试覆盖世界生成确定性、区块边界、渲染逻辑、玩家移动、生存规则、存档、实体、光照、客户端输入和完整生存进程；资产测试还检查图标确定性、二值透明度、调色板、模板边界、atlas 完整性、人工覆盖优先级，以及实时物品注册表的全覆盖。

## 项目说明

项目中的 GLAD、FastNoiseLite 与 stb_image 位于 `external/`。资产来源记录在 [`ASSET_SOURCES.md`](ASSET_SOURCES.md)，纹理许可说明位于 [`assets/textures/LICENSE.md`](assets/textures/LICENSE.md)，第三方资产许可模板位于 [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md)。
# glTF entity models

MinecraftC renders its eight mobs from reusable glTF 2.0 GLB assets with
hierarchical animation and GPU skinning. Versioned JSON action graphs provide
arbitrary override/additive layers, masks, queues, transitions, and gameplay
events. Every mob has a species-specific seamless walk cycle, while hostile
attacks use visible windups and event-timed impact checks. Each mob embeds an
original 64x64 semantic skin with independent head and body faces, continuous
pixel shading, and a dedicated recognizable face. Dropped items,
arrows, and primed TNT
retain the lightweight compatibility renderer. The original deterministic
assets and regeneration instructions are documented in
`assets/models/entities/README.md`.
The implementation history and validation record are in
`docs/gltf-entity-model-engine.md` and `docs/entity-animation-engine.md`.
