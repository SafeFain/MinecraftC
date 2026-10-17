# 天境插件化剩余工作调查

调查日期：2026-10-17；源码基线：main / 468a4eb。
范围：调查与现有 Windows 构建的定向验证，不实施运行时代码迁移。
本文更新 10/11 原始工程量评估和 10/12 复评；历史记录保留原结论。

## 当前结论

通用插件基础已经较完整，但当前不存在实际的 official_heaven 提供者。
天境仍由宿主无条件提供，不能把“支持第三方浮岛示例”当作“天境已插件化”。
下一步应闭合出生、结构和玩法语义，再迁出内容；无需重做整个插件框架。
默认目标为官方内置插件、保持 Heaven v9 / 全局 v20 输出，沿用旧格式拒载策略。
旧世界自动迁移、独立桌面动态库交付不属于这一默认范围。

## 已完成，勿重复建设

- 维度注册、命名空间维度键、维度策略、keyed save/LAN 与旅行接口。
- slab/stair/door/button 状态族、碰撞/选择/模型盒，基础燃烧、支撑与使用采收。
- 结构构建/定位/战利品，宿主授权动作、传送、冷却/耐久与客户端预览。
- 日夜/环境、粒子、手持 glTF 模型、维度音乐播放列表。
- 10/14 的 worldgen V2：区块/48×48 区域批量体素、多层 terrain spans、装饰 LOD、
  缓存身份与纯并发约束。见 sdk/minecraftc/extensions.h:134–183。

已有接口仍有下述适配缺口。tests/WorldgenV2Integration.h 的 heavenComparison
明确是把内置结果送入通用路径的测试适配器，没有迁出运行时天境，也不是迁移性能基准。

## 必须完成的工作

| 优先级 | 工作 | 当前证据及具体交付 |
|---|---|---|
| P0 | 种子相关出生策略 | extensions.h 的 MC_DimensionSpawn 只有 fixed/safe_surface；GameSession.cpp:1552 使用固定点/表面及邻近搜索，不能表达 WorldGenerator::findHeavenSpawnBlock 的主岛层选择。补纯出生采样，接入本地、LAN 和重新进入维度路径。 |
| P0 | 结构位置、跨度、元数据等价 | PluginGeneration.cpp:147 的 candidate 固定 key/hash/cell、最高表面和角点接受；PluginCapabilities.cpp:57 限制 height≤128 且相对 Y 非负。原 Skyway 跨 -88..+88，起源神殿须靠近出生点。补自定义候选/接受及垂直范围，或提供完整的生成结构输出路线，并统一定位、variant、loot seed、覆盖顺序。 |
| P0 | 方块/物品语义 | capabilities.h 的 MC_BlockBehavior 没有光衰减、燃料、破坏掉落规则和基底/木板/叶片标签。Block.cpp:1088 的叶片衰减、SurvivalRules.cpp:604 的矿石/浆果掉落、:764 的燃料、Item.cpp:1094 的木板判断和 BiomeBlockLogic.h 的基底白名单仍使用内置 ID。扩展通用属性并接入实际消费者。使用采收不能代替破坏掉落。 |
| P0 | Skyway 与权杖上下文 | MC_ActionView 只有 actor 位姿/item/loaded-block reader，缺潜行、使用方式及宿主命中块/面；Player.cpp:1165 仍以 !m_sneakInput 选 Skyway 上下方向。扩充只读上下文及必要的 seed/分层采样服务，LAN 执行时重验。权杖已有通用传送框架，主要剩具体算法/模型迁移及等价验证。 |
| P0 | 可选官方提供者与保存门禁 | Extensions.h:40 无条件注册 Heaven；BuiltinPlugins.cpp 只有两个示例，PluginManager.cpp:497/526 写死对应选择，且未设置内置包资源根。SaveStore.cpp:748 仍强制保存维度包含 minecraftc:heaven。应由官方提供者控制维度/内容可用性，处理保留槽位、资源根、默认启用和依赖拒载；禁用后无入口/配方/内容，拒载零写入。 |
| P1 | UI 与语言资源 | SleepMenu 固定 TravelToHeaven，创建世界的生成器按钮有双维度文案；Localization.cpp:28/83 只读宿主 lang 和 item.数字。需要注册式旅行入口、按注册维度显示配置、命名空间语言键及英文回退。 |
| P1 | 表现精确等价 | MC_ParticleDefinition 只有固定颜色/速度/重力，ParticleSystem.cpp:383–430 的天境漂移/旋转和无碰撞移动无法原样注册。补通用参数后迁出发射/群系配色策略；已有环境、手持模型和播放列表接口可复用。若要求专用天空代码也迁出，使用既有 sky render pass 或参数化；若保留通用天空预设，可延后这一项。 |
| P1 | 实际内容与算法迁出 | 抽出 WorldGenerator 的 v9 五层岛、生态、结构和出生算法；连接 V2 batch/LOD。迁出 Block/Item/SurvivalRules 的定义、配方、食物和天境专用逻辑，结构 loot、Skyway、权杖、语言/纹理/模型/音乐随包交付。GameSession/Player/World/Presenter/ParticleSystem/音频适配器仍有天境分支，须逐项归属。 |
| P1 | 集成验收与打包 | 建立启停/安全模式/缺包/版本错配零写入、保存重载、LAN、同种子/负坐标/region-singleton/跨界结构、多层 LOD 与性能、Vulkan 表现和五平台打包回归。iOS/Android 优先使用内置同接口插件。 |

V2 batch 的签名仅输出 uint16_t 方块，没有结构/loot 元数据输出缓冲；
当前结构元数据仍由宿主 pluginStructureBlocks 产生。
因此不能靠把结构方块塞进 batch 就认定结构迁移完成；/locate 和宝箱权杖必须一致。
也不要求同时实现“自定义结构策略”和“批量元数据”两套方案，应选择最小完整路径。

保存键/历史数字的保留与内容可用性要分开：保留 BlockId/ItemId 数值不代表无条件注册内容。
新增官方插件会改变精确玩法插件集合；现有世界如何显示不兼容必须明确，不能静默降低检查。
不默认增加旧世界升级要求；若需要兼容，应单独设计全文件映射/备份/验证。
仅移动算法且输出一致可以保留版本；任意输出变化必须明确生成版本决策。

## 推荐顺序与剩余估算

1. 固定 v9 输出/出生/结构与玩法对照，先处理本次暴露的测试基线问题。
2. 完成出生与结构整条路径、方块物品语义、动作上下文；并确定官方包身份与启停/存档策略。
3. 建立可选官方提供者，迁入生成/LOD/结构和内容、动作，分阶段对照旧实现。
4. 迁入 UI/语言/表现资源，完成禁用测试、宿主专用分支清理和五平台集成。

估算：熟悉项目的一名开发者，官方内置插件、保持输出、新世界为主：
宿主剩余补强约 5–8 人日，实际内容/算法迁出 7–12，集成回归 4–6，
合计约 16–26 人日（约 3–6 个工作周）。这是源码调查后的规划区间，非原型承诺；
跨平台设备/CI 等待、未知回归修复、旧档升级或独立动态库产品化另计。
不能直接沿用 10/11 的 25–40 人日，也不能因 V2 已做就忽略真正的内容搬迁。

## 本次验证与边界

未修改运行时代码，未重新构建；使用 cmake-build-windows 的现有 Release 程序。
默认 python/python3 为不可执行 WindowsApps 别名，任务工具使用 bundled Python。
首轮定向 CTest 1/7，通过天境，其他遇到原子替换 Access denied / 缓存未写入；
改用工作区内 TEMP/TMP 后重跑 7 项，4 项通过：天境、LOD、加载器、V2 batch/LOD。
另 3 项仍失败：gameplay/presentation 为 SegFault，block shapes 和 dimensions 为 LAN registry handshake。
未确定剩余失败的根因，不能断言是沙箱或代码回归；本次未越范围修复。
既有测试刻意拒绝坏插件的 WARN 属预期负面测试输出。

未运行完整 CTest、全新构建、Vulkan smoke、其他平台、迁移原型或天境迁移性能对比。
本次 4/7 不等于插件平台已通过完整验收。日志保留在本地任务 evidence。
