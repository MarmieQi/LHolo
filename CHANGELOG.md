# Changelog

## [26.20.8] - 2026-09-01

### Added

- 新增“材料清单”，可查看当前投影显示范围内的方块总数、材料种类、物品标识符和需求数量。
- 新增“缺失材料”HUD。
- 新增未放置标记和错误标记的独立穿透显示开关。
- 新增“多余方块”纠错提示，并可在 HUD 中独立显示多余方块数量。
- 新增加载投影和关闭投影快捷键。
- 快捷键绑定支持鼠标中键、侧键 1 和侧键 2。
- `.litematic` 遇到 Chunker 映射表中不存在、但 Java 与基岩版名称相同的方块时，会尝试使用基岩版同名方块显示，减少方块完全缺失的情况。

### Changed

- 材料统计改为后台计算并缓存结果，渲染线程只读取已完成的快照。
- 同一投影重复打开材料清单时复用方块到物品的解析缓存，避免大型投影重复计算。
- 材料按实际放置物品归类，相同物品对应的不同方块状态会合并统计。
- 材料 HUD 设置统一归入“HUD 信息显示”页面。
- 投影纠错统一处理未放置、方块类型错误、方块状态错误和多余方块，减少重复扫描与重复网格。
- 切换维度时不再直接关闭投影：进入其他维度后暂停投影并隐藏相关 HUD，返回投影所在维度后自动恢复。
- 退出单人世界或服务器时完整关闭投影并清理 HUD 状态。
- 配置文件版本升级到 10；旧配置缺少新增字段时继续使用默认值。

### Fixed

- 修复打开 LHolo 菜单时，鼠标点击可能穿透到 Minecraft 原生页面的问题。
- 修复打开过 LHolo 菜单后调整游戏窗口大小可能导致客户端闪退的问题。
- 修复 Overlay 可能错误处理其他模组交换链的问题；现在只处理已绑定的 Minecraft 主交换链。
- 修复图形后端初始化失败时可能遗留部分 COM 资源并反复初始化的问题。
- 修复退出世界后投影 HUD、材料 HUD 或状态信息仍残留在屏幕上的问题。
- 修复切换维度后原维度投影 HUD 仍继续显示的问题。
- 修复材料 HUD 异步更新时在旧结果、统计中和库存未刷新状态之间闪烁的问题。
- 修复大型投影反复打开材料清单时可能出现明显卡顿或闪退的问题。
- 修复手动放置缺少材料时被误提示为“未瞄准投影”的问题。
- 修复部分运行态方块、连接类方块、门、半砖、中继器和比较器的放置或状态判断不正确的问题。

## [26.20.7] - 2026-08-25

### Added

- HUD 的准心名称提示扩展为所有投影蓝图方块，并可在辅助放置关闭时独立使用；配置读取兼容旧的方块实体名称开关。
- `.litematic` 会读取并转换普通/悬挂告示牌的正反面文字、发光和蜡封数据，复用原版 Bedrock 方块实体渲染链显示文字。
- 轻松放置和范围放置（自动放置）会在投影位置的实体方块被破坏后跳过该位置 10 秒，手动放置不受影响。

### Changed

- HUD 的准心投影方块名称改用普通文字颜色显示。
- 辅助放置的背包交换失败重试间隔由 50 ms 调整为 200 ms；成功交换后的下一 tick 放置行为不变。

### Fixed

- 方块状态旋转和镜像改用当前 Bedrock 的通用状态变换 API，修复铁轨方向未随投影旋转的问题。
- 手动放置会捕获指向空气的右键操作，修复无法放置浮空投影方块的问题。

## [26.20.6] - 2026-08-22

### Changed

- 手动放置、轻松放置和范围放置改为仅当前游戏会话有效，不再从配置恢复或写入配置；辅助放置页会明确提示当前临时开启的模式。
- 水和岩浆投影现在会根据 `liquid_depth` 和相邻液体列生成带高度差的顶面与侧面，可直观显示液体流向；液源保持原版源液面高度，浸没格保持满高。
- 投影纠错状态在首次有界扫描后改为响应方块变化和子区块加载事件，稳定世界不再持续轮询整份结构。
- dirty section 的 `BlockTessellator` 与 CPU 几何生成移入单 Worker 后台线程，渲染主线程仅准备只读快照并上传已完成的 `MeshData`；大型投影初次生成和方块更新时的渲染卡顿显著降低。
- 后台构建优先处理玩家附近 section 和增量方块更新，普通更新会保留旧 Mesh 直到新 Mesh 就绪；Worker 初始化或连续构建失败时会自动回退到同步构建。

### Fixed

- 修复无旋转、无镜像时仍重映射方块状态，导致部分方向或组合状态与源结构不一致的问题。
- 修复投影中相邻箱子未正确配对为双箱的问题。
- 辅助放置在发送前会重新确认目标格仍为空气，并避免将同类半砖作为点击支撑而意外合并，减少无效重试和重复放置。
- 修复 Worker 中群系着色权重未按方块刷新，导致草方块偶发显示为白色的问题。
- 修复点击“关闭投影”时，渲染 Hook 在 Worker 退出窗口中重新加载旧结构的竞态问题。

## [26.20.5] - 2026-08-20

### Added

- 新增客户端“创建结构”页，支持两点选区、红色整体线框、实体开关和原版 `.mcstructure` 导出。
- HUD 新增总体进度，建造进度改为显示当前分层可见范围的进度。

### Changed

- 完善 `.litematic` 的 Java 至 Bedrock 方块映射和状态转换，映射生成工具仅保留在开发流程中。
- 调整设置导航顺序和“创建结构”页文案。

### Fixed

- 修复关闭投影后网格资源未完整释放、网格重建后未重新预检查，以及空结构反复尝试渲染的问题。
- 投影中隐藏活塞臂碰撞方块，避免错误渲染。
- 修复树叶投影缺少原版群系着色而显示为白色的问题。

## [26.20.4] - 2026-08-19

### Changed

- `.litematic` 的 Java 方块名称和状态改为使用由 Chunker 生成、集中维护的 Bedrock 1.26.20 完整映射表，并按源文件 `MinecraftDataVersion` 选择记录。
- 支持将 Java 含水状态拆分到基岩结构的第二液体层。

### Fixed

- 菜单打开及关闭输入过渡期间，在客户端阻止本地玩家开始或持续破坏方块；本地存档和远程服务器均有效，不影响其他玩家或正常移动。
- 修复退出世界后重新进入并加载或恢复 `.litematic` 投影时，复用已经失效的方块映射缓存导致客户端崩溃。
- 未进入服务器或单人存档时加载 `.litematic`，现在会提示尚未进入世界，不再导致客户端崩溃。
- 修复 Litematic 区域 `Size` 为负时错误倒序读取方块数组，导致整体结构被镜像或旋转、方向方块与 Java 源文件不一致的问题。

## [26.20.3] - 2026-08-16

### Added

- 范围放置：自动放置玩家周围半径内的投影缺块

## [26.20.2] - 2026-08-16

### Added

- 轻松放置：准心对准投影的缺块位置，自动放置对应方块
- 箱子、告示牌等方块在投影中显示为贴图占位外壳
- HUD 可显示准心指向的方块实体名称

### Changed

- 菜单界面调整（轻松放置开关、投影样式入口位置）
- 配置版本升级到 5，新增两项设置

### Fixed

- 修复打开/关闭菜单后光标消失的问题

## [26.20.1] - 2026-08-15

### Fixed

- Fix water and lava projections not rendering.

## [26.20.0] - 2026-08-15

### Added

- Structure projection for `.mcstructure` and `.litematic` files using vanilla block models.
- Correction overlays for missing blocks (blue), wrong block types (red), and wrong states (directions, yellow), with configurable fill/outline opacity.
- Build progress HUD with placed/total counts and separate type/state error counters.
- Rotation, mirroring, XYZ offsets, and Y/X layer slicing with four display ranges.
- Textured water and lava projections drawn from the vanilla terrain atlas, purely client-side.
- Alt+M or `lholo` chat command opens the injected Dear ImGui menu; chat command stays local.
- Projection state persistence (last file, anchor, transform) and hotkey configuration.

### Fixed

- Structure loading on remote servers now resolves through the client-side multiplayer level instead of the integrated-server level.
- The OS cursor display counter is snapshotted while the menu is open and restored after closing, preventing an invisible cursor.
