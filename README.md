# MinecraftC

MinecraftC 是一个使用 C++17 与 OpenGL 3.3 编写的体素沙盒游戏。项目提供 Y=-64..319 的可无限加载确定性世界、18 种生物群系、崎岖地形、洞穴与矿物、昼夜、动态光照和晴雨雷暴天气，并支持创造、生存与旁观模式。新建世界会预生成并缓存出生点附近区块，同时显示加载进度。

生存模式包含采集、挖掘、合成、熔炼、箱子、工具与护甲耐久、生命与饱食度、种植、树苗生长、床与重生、水中移动、动物、敌对生物、弓箭和死亡掉落。创造模式物品栏可访问全部注册物品并支持飞行；旁观模式提供无碰撞探索。天气使用雨、雪、雷电粒子，破坏方块会产生材质碎屑；行走和受伤具有独立的相机反馈。世界、玩家、实体、天气及容器状态均可持久保存。

## 主要特性

- 确定性种子控制地形、生物群系、洞穴、矿物、植被和树木，并保证区块边界一致。
- 异步区域生成、距离优先区块流送、贪心网格、环境遮蔽、透明材质和动态昼夜照明。
- 生存、创造和旁观模式，以及可持久化的多世界选择、游戏规则和客户端设置。
- 晴天、下雨和雷暴循环；雪原中降水表现为雪，雷暴可产生闪电。
- 生物战斗、碰撞击退、受击反馈、日照燃烧及昼夜相关敌对行为。
- JSON 驱动的方块/物品材质映射，以及确定性程序化 16×16 像素纹理资产管线。

## 构建要求

- 支持 C++17 的编译器
- CMake 3.16 或更高版本
- GLFW 3
- OpenGL 3.3 或更高版本
- GLM

Debian/Ubuntu 可安装以下依赖：

```bash
sudo apt install build-essential cmake libglfw3-dev libglm-dev libgl1-mesa-dev
```

## 构建与运行

在项目根目录执行：

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local -j2
./build-local/minecraftc
```

如果更新源码后遇到旧构建产物导致的链接或 ODR 错误，可执行干净重建：

```bash
cmake --build build-local --clean-first -j2
```

程序需要从项目根目录启动，因为着色器和纹理使用相对路径。

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

键盘、鼠标和滚轮操作均可在 Settings → Controls 中重新绑定。客户端设置保存在 `saves/options.txt`，世界存档位于 `saves/`。

允许作弊的世界支持以下命令：

```text
/gamemode 0|1|3
/tp x y z
/time set day|night
/weather clear|rain|thunder
```

## 纹理资产管线

方块和物品通过 `assets/textures/definitions/` 下的 JSON 文件引用逻辑材质名，C++ 与着色器不硬编码具体 atlas 坐标。正式生成纹理位于 `assets/textures/generated/`；运行时仍保留旧资源和程序化 atlas 作为兼容回退。

当前正式纹理种子为 `213785369`。从项目根目录重新生成、校验并打包：

```bash
python3 tools/texture_generator.py --generate --validate --build-atlas \
  --seed 213785369 --output assets/textures/generated

# 使用当前默认种子的等价 CMake 目标
cmake --build build-local --target texture_generator
```

生成器采用有限调色板、二值透明度、最近邻像素输出和环面坐标结构生成，并检测长直线、中心十字、大型矩形色块、周期性、频率分布及矿簇连通性。生成8组开发候选总览：

```bash
python3 tools/texture_generator.py --generate --validate --build-atlas \
  --seed 213785369 --candidate-count 8 --contact-sheet \
  --output assets/textures/generated
```

可重复使用 `--local-seed MATERIAL=SEED` 为单一材质选择局部种子，而不改变其他材质。候选总览不进入正式 atlas。完整的目录约定、新材质添加步骤和校验说明参见 [`ASSET_PIPELINE.md`](ASSET_PIPELINE.md)。

## 运行测试

```bash
ctest --test-dir build-local --output-on-failure
```

测试覆盖世界生成确定性、区块边界、渲染逻辑、玩家移动、生存规则、存档、实体、光照、客户端输入和完整生存进程。

## 项目说明

项目中的 GLAD、FastNoiseLite 与 stb_image 位于 `external/`。资产来源记录在 [`ASSET_SOURCES.md`](ASSET_SOURCES.md)，纹理许可说明位于 [`assets/textures/LICENSE.md`](assets/textures/LICENSE.md)，第三方资产许可模板位于 [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md)。
