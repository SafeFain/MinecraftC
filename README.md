# MinecraftC

MinecraftC 是一个使用 C++17 与 OpenGL 3.3 编写的体素沙盒游戏。项目提供可无限加载的确定性世界、18 种生物群系、洞穴与矿物、昼夜和光照系统，并支持创造、生存与旁观模式。

生存模式包含采集、挖掘、合成、熔炼、箱子、工具与护甲耐久、生命与饱食度、种植、树苗生长、床与重生、水中移动、动物、敌对生物、弓箭和死亡掉落。创造模式提供可滚动方块背包与飞行能力。世界、玩家、实体及容器状态均可持久保存。

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

## 运行测试

```bash
ctest --test-dir build-local --output-on-failure
```

测试覆盖世界生成确定性、区块边界、渲染逻辑、玩家移动、生存规则、存档、实体、光照、客户端输入和完整生存进程。

## 项目说明

项目中的 GLAD、FastNoiseLite 与 stb_image 位于 `external/`。纹理和程序化图形的来源及许可证记录在 [`assets/textures/LICENSE.md`](assets/textures/LICENSE.md)。
