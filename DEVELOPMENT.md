# LHolo 开发与版本适配手册

本文档描述 LHolo Windows 客户端模组的正式版架构、关键实现、性能约束、故障历史和新 Minecraft/LeviLamina 版本适配流程。维护者在修改渲染、输入、结构解析或配置前，应先阅读对应章节，并在发布前执行完整回归矩阵。

当前基线：

- Minecraft Bedrock Windows：`1.26.20.04`
- LeviLamina：`26.20.7`，目标类型 `client`
- 架构：Windows x64
- 图形接口：Minecraft D3D12 + LHolo D3D11On12 + Dear ImGui DX11 后端
- 模组名称、DLL、目录和内部命名空间：`LHolo` / `LHolo.dll` / `mods/LHolo` / `lholo`
- 支持格式：Bedrock `.mcstructure`、Java Litematica `.litematic`

---

## 1. 产品行为与边界

LHolo 只在客户端绘制虚拟结构，不向世界写入方块，不产生碰撞，不修改服务器数据。玩家可以穿过投影，服务器无需安装配套插件。

正式版功能：

- 从任意用户选择的路径加载 `.mcstructure` 或 `.litematic`，导入目录不写死。
- 以加载时玩家脚下的整数方块坐标作为投影锚点；恢复记录时使用保存的锚点。
- 支持 X/Y/Z 结构偏移、0°/90°/180°/270°旋转、X/Z/X+Z 镜像。
- 支持 Y 轴水平分层和 X 轴纵向切片。
- 显示范围支持“完整结构”“单层”“当前层及以下”“当前层及以上”。
- 投影透明度 0～100%，默认 100%。
- 纠错状态：未放置为蓝色、方块类型错误为红色、方向或方块状态错误为黄色、完全正确时隐藏。
- 纠错提示透明度默认 15%，描边透明度默认 100%；均为 0～100 整数输入、即时生效、持久化保存，可一键恢复默认值。
- 可选整体结构边框。
- HUD 可显示文件名、显示层、建造进度、放置错误数和朝向错误数；支持四角定位和单项关闭，两类错误可分别配置。
- GUI 使用外部注入 Dear ImGui，不使用游戏表单。
- 默认 `Alt + M` 打开菜单；聊天栏输入 `LHolo`（ASCII 大小写不敏感）也可打开，消息在客户端拦截，不发往服务器。
- 默认结构移动：`Ctrl + 方向键` 调整 X/Z，`Shift + ↑/↓` 调整 Y。
- 默认显示层：`Alt + ↑/↓`；“完整结构”模式下按键无效。
- 支持保存和恢复上次投影文件、锚点及当时的变换/分层参数。
- 退出世界、切换存档或失去有效客户端上下文时清理投影，禁止跨世界复用世界对象。

不在当前范围内：

- 不真正放置或自动建造方块。
- 不修改服务器世界或同步投影给其他玩家。
- 不支持旧测试命令 `sp`、`sp <block>` 或 `spgui`。
- 不保留早期单方块测试渲染、单方块射线选中或首次预热链路。

---

## 2. 项目目录与模块职责

```text
LHolo/Windows/26.20/LHolo/
├─ src/
│  ├─ plugin/
│  │  ├─ LHolo.cpp              模组启停、事件注册、Hook 总入口
│  │  ├─ LHolo.h
│  │  └─ MemoryOperators.cpp    Windows 客户端内存运算符适配
│  ├─ overlay/
│  │  ├─ ImGuiOverlay.cpp       DXGI/D3D11On12、WndProc、GUI/HUD 帧提交
│  │  └─ ImGuiOverlay.h
│  ├─ structure/
│  │  ├─ StructureLoader.cpp    两种格式解析、GUI、HUD、快捷键、配置
│  │  └─ StructureLoader.h      LoadedStructure 统一数据模型
│  ├─ projection/
│  │  ├─ Projection.cpp         投影网格、纠错、分区缓存、渲染 Hook
│  │  └─ Projection.h           GUI/HUD 使用的投影控制接口
│  └─ place/
│     ├─ PlaceHelper.cpp        轻松放置：准心定位、投影查表、快捷栏取物、useItemOn 放置
│     └─ PlaceHelper.h          配置开关与 Hook 生命周期接口
├─ manifest.json                Mod Packer 模板
├─ xmake.lua                    依赖、编译选项和发布规则
├─ DEVELOPMENT.md               本文档
├─ build/                       xmake 中间产物，不发布
└─ bin/LHolo/                   唯一发布目录
   ├─ LHolo.dll
   └─ manifest.json
```

模块边界必须保持清晰：

- `structure` 负责“文件和用户意图”，不直接提交 Minecraft 网格。
- `projection` 负责“结构如何出现在世界中”，不弹文件选择框、不直接操作 ImGui。
- `overlay` 负责“外部 GUI 如何安全进入游戏图形链”，不解析结构或扫描世界方块。
- `place` 负责“轻松放置”：只读准心结果、调用 projection 导出接口、取快捷栏物品并走 `GameMode::useItemOn`，不碰渲染与配置。
- `plugin` 只组织生命周期，不承载业务逻辑。

---

## 3. 启动、关闭与世界生命周期

### 3.1 模组启用

`LHolo::enable()` 的顺序：

1. 安装投影相关 LeviLamina Hook。
2. 安装轻松放置 `LocalPlayer::tickWorld` Hook（失败仅告警，不阻断）。
3. 尝试安装 ImGui/DXGI Hook；图形环境尚未可用时允许后续 `Present` 重试。

配置由 `LHolo::load()` 在 enable 之前从 `mods/LHolo/config/config.json` 读取。当前没有单独依赖世界退出事件；投影渲染入口通过 `client/level/dimension` 身份变化检测世界切换，并在上下文失效时调用 `projection::disable()` 等价的状态清理和 `structure::clear()`。

投影启用入口只有 `enableStructureProjection()`。它要求：

- 本地玩家存在。
- `LoadedStructure` 非空且至少有一个可渲染方块。
- Minecraft level atlas 已可用。
- 创建基于当前 `DimensionBlockSource` 的 `BlockTessellator`。

### 3.2 模组关闭

关闭顺序应保持对称：

1. 保存配置。
2. 清理投影状态和 GPU 网格。
3. 卸载轻松放置 Hook。
4. 卸载投影 Hook。
5. 关闭 ImGui 图形后端、恢复原 WndProc、移除 MinHook。

### 3.3 世界切换

`ProjectionState` 保存 `IClientInstance*`、`Level*`、`Dimension*`，仅用于验证当前上下文是否仍为创建投影时的世界。每次渲染先调用 `contextIsValid()`；不一致时立即清空投影和已加载结构。

绝对禁止：

- 跨世界保留 `BlockSource`、ECS Storage、组件指针或实体裸指针。
- 仅依据玩家是否为空判断世界相同。
- 在旧维度的 `BlockTessellator` 上继续查询新维度。

---

## 4. 结构文件解析

### 4.1 统一内存模型

两种格式最终转换为 `LoadedStructure`：

- `sourcePath`：原始文件路径。
- `sizeX/sizeY/sizeZ`：归一化后的正尺寸。
- `volume`：结构包围盒体积。
- `paletteEntries`：调色板项数量。
- `generation`：每次成功加载递增，用于通知投影替换结构。
- `renderBlocks`：仅包含至少一个可解析实体方块或液体的坐标。
- `RenderBlock{x,y,z,block,liquid}`：归一化局部坐标、可空实体方块指针和可空液体指针。同一坐标可同时具有实体与液体，用于含水方块。

### 4.2 `.mcstructure`

加载流程：

1. 读取文件，单文件上限 512 MiB。
2. 使用 Bedrock little-endian binary NBT 解析。
3. 校验 `size`、`structure.block_indices`、`palette.default.block_palette`。
4. 校验两个 block index layer 的长度等于结构体积。
5. 把完整根 NBT 交给原版 `StructureTemplate::create()` / `load()`。不要逐项调用 `Block::tryGetFromRegistry()`：那条捷径会绕开格式版本升级、世界方块调色板和 unknown-block registry，旧状态可能被错误解析成未知方块。
6. 从加载后的 `StructureTemplateData` 取得原版已升级的主/副索引数组和 `StructureBlockPalette`，用 `StructureBlockPalette::tryGetBlock()` 解析方块。不要用 `StructureTemplate::tryGetBlockAtPos()` 遍历文件：26.20 客户端该接口的坐标访问约定与 `.mcstructure` 的线性索引布局不一致，曾导致门上下半块错位和水取成错误方块。
7. 依照格式文档的 ZYX 顺序还原线性索引：`index = x * (sizeY * sizeZ) + y * sizeZ + z`。主副层分别解析后，同一坐标的非液体写入实体层、液体写入液体层。
8. 门的上下半块本来就是两个坐标、两个完整 palette state，不做合并；格式升级后的上下半块、铰链、朝向和开关状态由原版加载器保留。
9. 原版加载失败、原版尺寸与文件尺寸不一致时直接拒绝加载，不再带着未知方块继续渲染。

适配新版本时重点检查：`StructureTemplate::create/load`、`StructureTemplateData` 索引访问、`StructureBlockPalette::tryGetBlock` 的符号及语义、NBT 标签路径和两个 block index 的格式。固定回归门的上下相邻坐标与水坐标；不要退化回手工注册表解析，也不要未经验证改用 `tryGetBlockAtPos()` 遍历。

渲染与纠错约束：

- 实体模型走原版 `tessellateInWorld()`，并按原版 render layer 分桶。相邻实体方块只在 LHolo 生成网格的线程局部作用域内通过 `BlockSource::getBlock()` 暴露，供门、栅栏等邻居相关模型正确生成；作用域外始终调用原版函数，不改变世界。
- 水和岩浆使用贴图 proxy 单元壳，完全由 LHolo 自绘，不与原版世界或区块管线交互：仅 Missing（未放置）状态的液体格绘制半透明截顶外壳，最上层液体格顶面固定为原版源液体高度 8/9（`getHeightFromDepth()` 在 1.26 上对源液体的返回值不可靠，不再使用；逐格流动深度不参与视觉，只参与纠错比较），上方有同液体时侧壁满格；相邻同种液体剔除共享面；UV 取自 `BlockGraphics::getForBlock(liquid)->getTexture(0, 0)` 的 terrain atlas 水/岩浆贴图；水顶点色为原版蓝 #3F76E4（atlas 水贴图无色），岩浆白色顶点色保留贴图原色；alpha 跟随投影透明度；经 `liquidProxySectionMeshes` 独立网格在 alpha pass 用 `mMatBlendBlock` + terrain atlas 提交（与玻璃同路径），按 section 距离排序。静态贴图无波浪动画是已知限制。纯液体格的 Missing 不再叠加蓝色纠错面/描边（proxy 本身即提示），WrongType/WrongState 仍保留红/黄纠错面。`.litematic` 加载时液体路由到 `RenderBlock::liquid` 字段，与 `.mcstructure` 语义一致。
- `.litematic` 加载时把 `getMaterial().isLiquid()` 的方块路由到 `RenderBlock::liquid` 字段，与 `.mcstructure` 语义一致。
- 纠错分别比较 `BlockSource::getBlock()` 与 `getLiquidBlock()`。缺少液体判为“未放置”，液体类型错误判为“类型错误”，液体深度等状态不同判为“状态错误”。
- 投影进度仍以结构坐标计数，而不是把同一坐标的实体层和液体层重复计数。

不要重新引入“把水交给 `tessellateLiquidInWorld()` 后自行提交”的方案——该路线已两次证伪：盲提交表现为黑块/过曝，受控版本（Blend 桶 + `mMatBlendBlock` + 顶点色覆写）表现为未知方块纹理，说明其顶点格式/UV 语义与普通方块材质根本不兼容。当前正式方案是世界注入（见 4.2 节）：Hook `BlockSource` 读取路径 + `fireBlockChanged` 触发原版区块重建，让原版 terrain water pass（dragon framebuilder 的 `DeferredWater`/`water::WaterParameters` 组件）自己渲染投影液体。注入语义是“真实空气才填充”，因此不产生碰撞、不覆盖真实方块、放对方块后自动让位；服务器无感知。

### 4.3 `.litematic`

加载流程：

1. 读取文件；检测 gzip 头 `1F 8B`，使用 zlib 解压，解压上限 1 GiB。
2. 使用本项目只读 Java big-endian NBT 解析器读取根 Compound。
3. 遍历 `Regions`，读取每个区域的 `Position`、有符号 `Size`、`BlockStatePalette` 和 `BlockStates`。
4. 调色板名称经 `BlockTypeRegistry::lookupByName()` 映射到 Bedrock 方块。
5. 每项位宽为 `max(2, bit_width(paletteSize - 1))`，从 LongArray 解包 palette index。
6. 正确处理负 Size：负轴从区域 Position 向负方向展开。
7. 计算所有区域的全局最小/最大坐标，将多区域合并到从 `(0,0,0)` 开始的正坐标包围盒。
8. 重叠坐标使用后处理区域覆盖先处理区域，最终按 `(x,y,z)` 排序。

限制：当前 Java 方块解析以方块 `Name` 为主。Java/Bedrock 状态名称或枚举值不完全一致时，方向状态可能无法一一映射；新版本适配应为需要的方块增加明确映射，而不是默默猜测。

### 4.4 文件安全约束

- 所有数组长度在分配前检查负值和上限。
- gzip 解压必须限制总输出，防止压缩炸弹。
- 体积乘法使用 64 位整数。
- 坐标范围在转换为 `int` 前检查溢出。
- 解析异常转换为用户可见错误，不允许越界继续。

---

## 5. 坐标、锚点、旋转与镜像

### 5.1 世界坐标

最终世界坐标：

```text
world = anchor + userOffset + transform(local, mirror, rotation)
```

- 新加载时 `anchor = floor(player.position)`。
- 恢复投影时 anchor 使用配置中保存的绝对世界坐标。
- X/Y/Z 输入与快捷键只修改 `userOffset`，不改原始结构数据。
- GPU 顶点保持相对投影原点，提交时矩阵平移到 `renderOrigin - camera`，避免玩家位于数万格时大浮点坐标造成抖动或破面。

### 5.2 变换顺序

当前顺序固定为“镜像后旋转”。局部位置与方块状态必须使用同一旋转/镜像：

- 坐标：`transformStructurePosition()`。
- 方块朝向/状态：`VanillaBlockStateTransformUtils::transformBlock()`。

只改坐标而不改方块状态，会导致楼梯、栅栏门、活塞、观察者等方向判定错误。

### 5.3 分层

- 轴 0：Y 轴水平层，层号取局部 `entry.y`。
- 轴 1：X 轴纵向切片，层号取局部 `entry.x`。
- 模式 0：完整结构。
- 模式 1：仅等于当前层。
- 模式 2：当前层及以下。
- 模式 3：当前层及以上。

隐藏层不生成投影/纠错网格，但建造总进度仍统计完整结构。

---

## 6. 投影网格与渲染路径

### 6.1 原版方块模型复用

LHolo 不自制草方块、楼梯等材质模型。它使用：

- `BlockTessellator::tessellateInWorld()` 生成原版方块几何。
- Minecraft level atlas 提供纹理。
- `BlockGraphics::getRenderLayer()` 取得实际渲染层。
- `VanillaBlockStateTransformUtils` 取得旋转/镜像后的方块状态。

这样可保留草色、生物群系着色、方块模型和原版纹理。若新版本出现草方块白顶、随机材质或黑块，应先检查 atlas、BlockGraphics、Tessellator 缓存和材质，不要重新引入手写 UV。

本节只描述实体方块模型。水和岩浆使用 4.2 节所述的液体 proxy 单元壳（`liquidProxySectionMeshes`），不进入这四种持久 GPU 网格桶，也不调用 `tessellateInWorld()`。

### 6.2 渲染桶

方块按原版 `BlockRenderLayer` 映射到四类持久 GPU 网格：

- `Opaque`
- `Alpha`
- `AlphaOneSided`
- `Blend`

投影透明度为 100% 时，尽量使用匹配的原版 opaque/alpha/one-sided 材质；透明度低于 100% 时，所有桶在透明 pass 中合并排序并使用 blend material，降低跨分区透明排序闪烁。

### 6.3 相机相对坐标

网格生成后，顶点减去投影 `renderOrigin`。每帧只在 world matrix 中应用相机相对平移。不要把几万格绝对坐标直接上传为 float 顶点，否则会复现远坐标渲染错误。

### 6.4 灵动视效 / Deferred 路径

通过 `ItemInHandRenderer::mIsDeferredEnabled` 判断灵动视效路径。

- 普通路径：纠错面使用选择覆盖材质，并暂时借用标准 alpha blend 状态。
- Deferred 路径：使用已验证可见的 outline material，提交纠错 QuadList 前临时替换 primitive、blend、depth bias 和 slope bias。
- 每次临时改动材质状态后必须立即恢复，不能污染 Minecraft 后续绘制。

新版本若灵动视效下投影全白、全黑或纠错颜色消失，优先检查这些材质字段是否仍存在、语义是否改变，以及 deferred 标志是否仍可靠。不要简单提高颜色亮度掩盖材质错误。

---

## 7. 纠错状态机与视觉提示

### 7.1 状态定义

每个结构方块保存一个字节状态：

- `Unknown`：尚未扫描。
- `Missing`：世界方块为空气，蓝色提示。
- `Correct`：类型和完整方块状态相同，不显示投影或纠错。
- `WrongType`：类型名不同，红色提示。
- `WrongState`：类型相同但完整状态不同，黄色提示。

判定顺序：空气 → 类型名 → 完整 `Block` 相等 → 状态错误。这个顺序不能交换，否则空气可能被算成普通类型错误，方向错误也可能被吞掉。

### 7.2 为什么错误位置不再绘制投影模型

世界已有正确或错误方块时，不再在同一位置绘制投影方块模型，只绘制纠错面与描边。这样避免两个有纹理表面共面产生 Z-fighting，也避免纠错颜色覆盖后看不清真实方块。

未放置方块仍显示原版投影模型，并叠加蓝色纠错提示。

### 7.3 纠错面

- 使用精确 1×1×1 单元外壳，不跟随栅栏、玻璃板等非完整碰撞模型。
- 相邻纠错单元通过优先级和邻居检查剔除内部共享面，避免同一平面绘制两次。
- 优先级：类型错误 > 状态错误 > 未放置。
- RGB 使用固定 Litematica 风格色；alpha 由“纠错提示透明度”动态写入顶点色。
- 默认 alpha 15%，范围 0～100%。

### 7.4 描边

- 使用真正 `LineList` 的 12 条边，贴合 1×1×1 单元。
- 默认透明度 100%，范围 0～100%。
- 使用原版 outline selection material，保证普通和灵动视效路径可见。
- 整体结构边框是独立网格，不受纠错描边透明度控制。

### 7.5 准心选中闪烁修复

Minecraft 会对准心选中的真实方块额外绘制 hit-select overlay。若该位置同时为红/黄纠错，第二个共面 overlay 会只在选中时出现并闪烁。

`LevelRendererPlayer::renderHitSelect` Hook 在目标坐标对应 `WrongType` 或 `WrongState` 时跳过原版 overlay。其他方块和未放置位置继续调用原函数。新版本适配必须验证该 Hook 签名和坐标语义。

---

## 8. 高性能设计

### 8.1 16³ 分区

结构按局部 `(x/16, y/16, z/16)` 分区。每个分区保存：

- 方块索引列表。
- 四种投影渲染桶网格。
- 纠错面网格。
- 纠错描边网格。
- 分区中心，用于透明排序。
- dirty 标记。

稳定帧不重新 Tessellate 方块，只提交已有 GPU 网格。

### 8.2 有界纠错扫描

`kCorrectionChecksPerFrame = 4096`，按 round-robin cursor 扫描。无论结构多大，每个渲染帧查询上限固定，避免全结构每帧扫描。

注意：该常量控制“纠错响应速度 vs 单帧 CPU 成本”。适配新版本时必须实测后调整，不要根据理论 FPS 盲目增大。建议记录：

- 1 万、10 万、50 万方块结构的帧时间。
- 放置/拆除方块后提示更新延迟。
- 普通渲染和灵动视效的 CPU/GPU 占用。

### 8.3 增量失效

- 方块状态实际变化：只标记所属分区和六个邻居所在分区 dirty。
- 每帧最多重建一个 dirty 分区。
- 旋转/镜像：局部模型改变，全部分区失效。
- 投影透明度或纠错样式改变：顶点 alpha 改变，全部分区一次性失效，之后继续缓存。
- X/Y/Z 整体移动：GPU 局部网格不重建，只更新 world lookup、纠错扫描位置和矩阵平移。
- 显示层变化：只失效可见性跨越边界的分区。

### 8.4 HUD 零额外扫描

纠错扫描同时维护：

- `progressCorrect` 与正确计数。
- `progressErrorKind`：每个结构坐标使用一个字节记录无错误、类型错误或状态/朝向错误。
- `progressWrongTypeCount` 与 `progressWrongStateCount` 两个独立计数。
- 原子发布的 `placed/total/wrongType/wrongState`。

HUD 每帧只读取原子计数，不查询世界、不遍历结构。

### 8.5 透明排序

透明网格按分区中心到相机距离从远到近排序。整体移动只改变矩阵/世界中心，不需要重建网格。若新版本出现特定角度黑块或闪烁，检查：

1. 是否错误地让 opaque 网格走了 blend pass。
2. 分区中心是否包含当前 anchor + offset。
3. 同一表面是否由投影模型、纠错面和 hit-select 重复绘制。
4. 材质是否写深度、混合状态是否被其他 Hook 污染。

---

## 9. GUI、快捷键与输入交接

### 9.1 ImGui 页面

GUI 覆盖当前 `ImGuiIO::DisplaySize`，顶部导航包含：投影、结构变换、渲染设置、快捷键、HUD。界面缩放范围 1～5。

GUI 是全屏 ImGui 窗口，不是切换 Minecraft 窗口模式。

### 9.2 输入所有权

菜单打开时，WndProc 将鼠标、键盘、字符和 Raw Input 交给 ImGui并阻止游戏同时处理，以避免拖动 GUI 时转动视角或继续移动。

打开菜单前记录游戏当前按键/鼠标按下状态，并向游戏补发必要的 key-up/button-up，避免“按住移动键打开菜单，关闭后角色自动走”。

关闭菜单时：

- 将鼠标恢复到游戏客户区中心。
- 短暂限制鼠标到客户区并设置输入阻断窗口。
- 消费菜单关闭动作对应的释放消息。
- 防止关闭后的首次右键变成 ESC/暂停效果或鼠标落到客户区外。

不要依赖“消息积压”解释输入 bug；应检查鼠标坐标、Capture/ClipCursor、按键状态和 Raw Input 所有权。

### 9.3 快捷键配置

修饰键位图：Ctrl=1、Alt=2、Shift=4。捕获快捷键时忽略单独修饰键，F11 不允许成为模组快捷键。

恢复默认快捷键：

- 菜单：Alt+M。
- X/Z：Ctrl+方向键。
- Y：Shift+上/下。
- 显示层：Alt+上/下。

---

## 10. ImGui 图形链与 F11 生命周期

### 10.1 Hook 列表

`ImGuiOverlay.cpp` 使用 MinHook 连接：

- `IDXGISwapChain::Present`
- `IDXGISwapChain1::Present1`
- `IDXGISwapChain::ResizeBuffers`
- `ID3D12CommandQueue::ExecuteCommandLists`
- 游戏窗口 WndProc

`ExecuteCommandLists` 用于捕获可用的 Direct D3D12 command queue。随后建立 D3D11On12 device/context，并让 ImGui 使用 DX11 后端。

### 10.2 每帧资源规则

需要绘制 GUI/HUD 时：

1. 从当前 swap chain 取得当前 back buffer。
2. 为该帧创建 wrapped resource 和 RTV。
3. `AcquireWrappedResources`。
4. 绘制 ImGui。
5. 解除 RTV 绑定。
6. `ReleaseWrappedResources` 并 `Flush`。
7. 释放该帧 wrapped resource 和 RTV。

不跨帧持有 back buffer wrapper。GUI/HUD 都不显示时跳过这条提交路径。

### 10.3 F11 崩溃根因

历史崩溃条件：只要初始化过 ImGui，之后 F11 切换全屏就以 `0xC0000409`/`std::terminate` 结束。IDA 与日志表明进入 Minecraft 的 device-lost 处理链，而不是普通输入异常。

根因是 Minecraft 开始全屏/交换链转换时，外部 D3D11On12 后端仍持有旧图形状态。仅在 `ResizeBuffers` 释放 RTV 已经太晚。

### 10.4 正确修复

WndProc 收到首次 `WM_KEYDOWN + VK_F11`，在消息交回 Minecraft 前：

1. `ImGui_ImplDX11_Shutdown()`。
2. 解除 RTV。
3. `ClearState()`、`Flush()`。
4. 释放 D3D11On12 device/context/device。
5. 保留 ImGui Context、Win32 后端、WndProc 和业务状态。
6. 延迟约 750 ms，允许 Minecraft 完成交换链切换。
7. 后续有效 Present 使用新 swap chain/queue 重建图形后端。

不要回退为只清 RTV、只 invalidate ImGui device objects 或只依赖 ResizeBuffers。

### 10.5 图形故障日志

以下失败必须记录 HRESULT 与 `GetDeviceRemovedReason`：

- D3D12/D3D11 `GetBuffer`
- `CreateWrappedResource`
- `CreateRenderTargetView`
- `ResizeBuffers`

错误日志是正式版维护能力，不应作为“调试死代码”删除。

---

## 11. 网络命令与 Hook

菜单命令在 `LoopbackPacketSender::$sendToServer` 和 `$send` 两个路径拦截 Text Chat packet。只匹配长度恰好为 5 的 ASCII `lholo`，逐字符转小写比较。

匹配时：

- 确保 overlay 已安装。
- 请求打开菜单。
- 返回，不调用原发送函数。

不匹配时原样发送。不要使用前缀匹配，否则普通聊天可能被误吞。

投影相关 LeviLamina Hook：

- 两个 LoopbackPacketSender 发送路径：本地菜单命令。
- `LevelRendererPlayer::renderHitSelect`：避免红/黄纠错与原版选中覆盖共面闪烁。
- `LevelRendererPlayer::$renderBlockEntities`：在原版调用后更新/提交投影。
- `LocalPlayer::$tickWorld`（`place` 模块）：轻松放置的每 tick 驱动。

新版本最容易变化的是成员函数符号、签名、调用层次和 render pass 时序，必须逐一验证，不能只以“Hook 安装成功”判断适配完成。

---

## 12. 轻松放置

### 12.1 行为

菜单“投影”页勾选“轻松放置”后常驻生效：准心指向投影中的蓝色缺块位置（`correctionStates == Missing`）时，自动从快捷栏取对应物品并放置。只放投影期望的方块，液体单元与隐藏层不参与。

### 12.2 实现要点

- 驱动：直接 Hook `LocalPlayer::$tickWorld`，模拟线程每 tick 一次，不使用 LL 事件系统（与全项目 Hook 风格一致）。
- 定位：不能使用 `Level::getHitResult()`——那是原版射线，只命中真实世界方块，永远看不到 LHolo 自绘的投影幽灵。改为自身体素 DDA（Amanatides & Woo）射线：原点 `Actor::getEyePos()`、方向 `Actor::getViewVector(1.0f)`、上限 `LocalPlayer::getPickRange()`。逐格判定：真实方块挡住射线（此时检查其相机侧邻居是否为待放幽灵），投影 `Missing` 幽灵格直接作为放置目标；支持面用 `BlockPos::neighbor` + `Facing::getOpposite` 选取朝向相机、且为真实方块的邻居。
- 投影查表：`Projection::queryProjection()`——一次锁 `gStateMutex` 内同时查 `expectedWorldBlocks`（期望块，液体/隐藏层返回 null）与 `expectedWorldBlockIndices`/`correctionStates`（是否 Missing）。DDA 每格只调一次，避免两次独立加锁；命中结果（含期望块指针）随 `ProjectionTarget` 一并返回，`tickEasyPlace` 不再二次查询。
- 取物：遍历完整背包（36 格）用 `sameItemAndAuxAndBlockData` 匹配。快捷栏命中直接 `Player::setSelectedSlot`；背包命中用 legacy `NormalTransaction`（`ComplexInventoryTransaction::fromType` + 两个 `InventoryAction`）把物品与当前选中格**交换**（服务器同步，不假设目标格为空，避免被 net 管理器回滚）。交换后本 tick 不放置，下一 tick 物品已在选中格、走单包快速路径——同 tick 立即放置会被服务器 net 记账滞后拒绝，再触发格锁反而更慢。服务器只接受选中快捷栏槽位的放置事务。
- 放置：直接构造 `ItemUseInventoryTransaction`(Place) 经 `IClientInstance::getPacketSender().sendToServer()` 发送（单机走 LoopbackPacketSender，联机走网络发送器），服务器权威落块并保存。**关键细节**（踩坑记录）：
  - `mPos` 是“被点击的方块”，服务器在 `mPos.neighbor(mFace)` 落块——必须填支撑块 `at`，填目标格会导致偏一格。
  - `setIncludeNetIds(true)`：服务器栈 net-id 系统按“含 net id”读包，缺此标志流错位、包被静默丢弃（`onTransactionError` 都不会触发）。
  - `setTargetBlock` / `setSelectedItem` 用导出 setter 填 `mTargetBlockId` / `mItem`，避免未导出的赋值运算符。
  - 点击点取支撑面中心：`(cell + at)` 的中点。
  - `GameMode::useItemOn`（客户端）只做本地预测不持久；`Player::sendNetworkPacket` 在单机不送达集成服务器，两条路都不可用。
- 节流：逐格锁——已放置的格在 `kCellLockMs`（200ms）内不重复发包，等服务器应用 + 纠错扫描更新；新格沿射线立即放置（受 tick 20Hz 上限约束）。发送地板间隔 `kMinSendIntervalMs`（40ms）防异常 tick 率双发。
- 守卫：开关关闭、未进世界、`isInGameInputEnabled()` 为假（菜单/暂停）或 LHolo 菜单打开时全部跳过。

### 12.3 已知限制

- 服务器只接受快捷栏槽位（`mSlot` 为 0-8）的放置事务：直接指定背包槽会被消耗物品但不落块。背包物品靠 legacy `NormalTransaction` 交换到选中格——单机（客户端托管世界）实测有效；联机服务器是否接受 legacy 背包事务需实测。
- 直接 `swapSlots`（绕过物品栈请求系统）会被服务器 net 管理器回滚（物品在快捷栏/背包间闪烁），不可用。
- LeviLamina 26.20 客户端包的 `ItemStackNetManagerClient` / `ItemStackRequest*`（服务器权威物品请求系统）符号均为 `MCNAPI` 未导出（编译期 “This API is not available” 警告、链接期 LNK2019），无法用它做联机安全的跨容器交换。

---

## 13. 配置与持久化

配置路径：

```text
mods/LHolo/config/config.json
```

当前配置版本：`7`。

正式持久化字段：

- `version`
- `lastStructurePath`
- `uiScale`
- `opacity`
- `correctionFillOpacity`
- `correctionOutlineOpacity`
- `structureBoundsEnabled`
- `easyPlaceEnabled`
- `easyPlaceManual`
- `rangePlaceEnabled`
- `placementRadius`
- HUD 开关、各项显示开关（含 `hudShowBlockEntity` 方块实体名称）、位置
- GUI、移动、显示层快捷键与修饰键
- 上次投影是否存在、文件路径、绝对锚点
- 上次投影旋转、镜像、偏移、显示模式、显示层和分层轴

普通结构变换和显示层属于当前会话；只有“恢复上次投影”记录显式跨会话保存。纠错样式、投影透明度、GUI/HUD 和快捷键属于用户偏好，始终持久化。

配置读取必须：

- 为缺失字段提供当前默认值。
- 对数值做范围限制。
- 解析失败记录错误，不导致模组加载崩溃。
- 写入前创建配置目录。
- 更新字段时同步递增配置版本并更新本文档。

---

## 14. 构建、发布与部署

### 14.1 依赖

- Visual Studio 2022 C++ 工具链
- xmake
- LeviLamina 26.20.7 client
- levibuildscript
- Dear ImGui 1.91.9，Win32 + DX11，静态
- MinHook
- zlib

编译设置：C++20、MD runtime、UTF-8、警告等级 `/W4`。

### 14.2 干净 Release 构建

```powershell
xmake clean
xmake f -m release
xmake -r
```

唯一发布目录：

```text
bin/LHolo/
├─ LHolo.dll
└─ manifest.json
```

不要发布 `build/`、`.exp`、`.lib`、日志、崩溃转储、配置或测试结构文件。

### 14.3 本机部署

测试路径：

```text
D:\games\LeviLauncher\MC\versions\1.26.20.04\mods\LHolo
```

部署前确认 `Minecraft.Windows.exe` 未运行。复制 DLL 后对构建产物和部署文件计算 SHA256，必须一致。

---

## 15. 新版本适配流程

### 阶段 A：建立新目录与依赖

1. 复制当前稳定版本到新的 `Windows/<版本>/LHolo`。
2. 修改 `xmake.lua` 的 LeviLamina 版本和 Mod Packer 版本。
3. 修改 manifest 版本、描述中的 Minecraft 版本。
4. 保留项目名、DLL 名、配置目录和命名空间 `LHolo/lholo`。
5. 先做不运行游戏的干净 Release 构建，解决 SDK/API 编译变化。

### 阶段 B：验证启动与基础 Hook

1. 无结构加载启动游戏，确认模组日志正常。
2. 测试 Alt+M 和 `LHolo` 指令。
3. 打开/关闭菜单，确认鼠标、键盘和视角交接正确。
4. 连续切换 F11 至少三轮，并在每次切换后重新打开 GUI。
5. 检查 Present/Present1/ResizeBuffers/ExecuteCommandLists 是否仍使用预期 vtable 索引和接口。

### 阶段 C：验证结构解析

准备固定回归样本：

- 小型 `.mcstructure`：草方块、石头、玻璃板、栅栏、楼梯、门、活塞、观察者、普通水、岩浆、不同液位，以及至少一个含水方块。水/岩浆样本同时验证 proxy 投影（蓝色水面/橙色岩浆面、液面高度随液位变化、相邻共享面剔除）与纠错。
- 多区域 `.litematic`：正/负 Size、区域重叠、不同 palette 位宽。
- 大型结构：至少 10 万方块。
- 损坏/截断/超大文件。

确认尺寸、方块数、朝向、负区域归一化和错误提示。

### 阶段 D：验证原版渲染 API

逐项检查：

- `LevelRenderer::mAtlasTexture` 是否仍为方块 atlas。
- `BlockTessellator` 构造与 `mCachedGetBlock` 行为。
- `BlockSource::$getBlock` 两个重载的 Hook 签名和线程局部虚拟实体方块作用域；门的上下半块与邻居模型是固定回归项。
- `BlockSource::getLiquidBlock()` 的读取语义；它仅用于纠错查询，当前没有虚拟液体 Hook。
- `tessellateInWorld()` 参数和顶点数据布局。
- `BlockGraphics::getRenderLayer()`。
- `VanillaBlockStateTransformUtils::transformBlock()`。
- ItemInHandRenderer 中 opaque/alpha/one-sided/blend 材质。
- Deferred 标志与 outline/selection overlay 材质。
- `RenderMaterial` primitive、blend、depth bias 字段。

出现黑块、白顶、材质错位时先定位上述 API，禁止盲目叠加亮度或手写材质替代。

### 阶段 E：验证纠错和性能

1. 未放置显示蓝色，正确后完全隐藏。
2. 错类型红色，错方向/状态黄色。
3. 非完整方块仍使用完整 1×1×1 提示。
4. 相邻提示无内部面闪烁。
5. 准心选中红/黄真实方块不闪烁。
6. 普通与灵动视效下透明度和颜色均正常。
7. 修改纠错提示/描边透明度，只发生一次分区重建。
8. 移动投影不重建所有方块模型。
9. 切换一层只更新受影响分区。
10. 大结构稳定帧无持续 Tessellation 峰值。

### 阶段 F：世界生命周期

1. 加载投影后退出主菜单，再进入同一世界：旧运行态不残留，可手动恢复记录。
2. 切换到另一个世界：旧投影绝不出现。
3. 维度切换：旧 `DimensionBlockSource` 不被继续使用。
4. 世界加载未完成时打开菜单/恢复投影不崩溃，资源就绪后正常启用。

### 阶段 G：发布

1. 完成代码审计：无旧命令、旧项目名、测试单方块路线和一次性诊断统计。
2. 保留必要错误日志和图形故障诊断。
3. 更新本文档的版本基线、API 变化和已知限制。
4. 执行干净 Release 构建。
5. 检查 `bin/LHolo` 仅有 DLL 和 manifest。
6. 部署并核对 SHA256。
7. 完整执行下一章回归矩阵。

---

## 16. 发布前回归矩阵

### GUI/输入

- [ ] 首次进游戏无需先打开其他界面，Alt+M 可打开菜单。
- [ ] `LHolo`、`lholo`、混合大小写均打开菜单且不发送聊天。
- [ ] 菜单打开时鼠标不转视角、按键不移动玩家。
- [ ] 移动中打开菜单，关闭后不会自动移动。
- [ ] 关闭菜单后鼠标位于游戏客户区，右键不会触发暂停/ESC 效果。
- [ ] 输入框、浏览对话框、缩放 1～5、顶部导航正常。

### 图形生命周期

- [ ] 未打开菜单时 F11 正常。
- [ ] 打开并关闭菜单后 F11 连续切换三次不崩溃。
- [ ] F11 后 GUI/HUD/投影仍能重新显示。
- [ ] 调整窗口尺寸或切换全屏后无旧 back buffer 引用。

### 文件与变换

- [ ] `.mcstructure` 从任意路径加载。
- [ ] `.litematic` 单区域、多区域、负 Size 正确。
- [ ] 0/90/180/270 度位置和方块朝向一致。
- [ ] X/Z/X+Z 镜像正确。
- [ ] X/Y/Z 偏移快捷键和输入一致。
- [ ] 远坐标（至少 ±20000）稳定。

### 分层/HUD/持久化

- [ ] 四种显示范围正确。
- [ ] 完整结构模式下显示层快捷键无效。
- [ ] X/Y 分层轴正确。
- [ ] HUD 四角位置和各项开关持久化。
- [ ] 建造进度和错误数随放置/拆除更新。
- [ ] 恢复上次投影包含文件、锚点和保存的变换参数。

### 纠错与渲染

- [ ] 未放置的水显示带原版水贴图的蓝色半透明外壳（无波浪动画，静态贴图为已知限制），岩浆显示原版岩浆贴图。
- [ ] 玩家穿过投影水/岩浆无任何游戏效果（无伤害、无着火、无声音、无游泳状态）。
- [ ] 在服务器上使用时服务器日志无异常、无踢出、世界数据无变化。
- [ ] 液体放对后虚拟水在一小段时间内消失；拆除后虚拟水恢复。
- [ ] 移动/旋转投影后旧位置虚拟水消失、新位置出现。
- [ ] 退出世界再进入，虚拟水不残留。
- [ ] 灵动视效/Deferred 路径下注入水体正常。
- [ ] 草方块颜色、顶面和侧面与原版一致。
- [ ] 石头、玻璃、玻璃板、栅栏、楼梯、门等模型正常。
- [ ] 透明度 100% 与低透明度均无整体黑块。
- [ ] 蓝/红/黄提示及描边透明度输入 0、15、50、100 均正确。
- [ ] 一键恢复默认得到提示 15%、描边 100%。
- [ ] 相邻提示、非完整方块和准心选中不闪烁。
- [ ] 普通画面与灵动视效均执行上述测试。

### 性能/生命周期

- [ ] 大结构稳定时不持续重建所有分区。
- [ ] 移动整体结构时 GPU 模型不全量重建。
- [ ] 修改样式只触发一次重建波次。
- [ ] 退出/切换世界后投影和世界指针全部清理。

---

## 17. 已淘汰方案与禁止回归项

以下路线已验证失败或已被正式架构替代，不应重新引入：

- `sp`/`sp <block>` 头顶单方块测试命令及其延迟切换状态。
- `spgui` 旧菜单命令。
- 单方块即时 Tessellator、首次黑块预热、单方块射线选中日志。
- 手写草方块 UV、手动替换材质或只修改顶点 alpha 的早期实验链。
- 液体 proxy 单元壳（纯色半透明截顶立方体）：已被世界注入方案整体替代并删除。
- `tessellateLiquidInWorld()` 几何自行提交：盲提交黑块/过曝，受控提交（顶点色覆写 + Blend 桶）未知方块纹理，顶点格式/UV 语义与普通方块材质根本不兼容。
- 用 `BlockSource::$fireBlockChanged` 触发区块重建：该事件会抵达游戏逻辑监听者（液体流动 tick、燃烧、声音、网络），实测导致投影岩浆/水被流动模拟写成真实方块。渲染失效只能走 `RenderChunkCoordinator::$onAreaChanged`。
- 世界注入（Hook `BlockSource::getBlock`/`getLiquidBlock` 对读取路径返回投影液体 + `RenderChunkCoordinator::$onAreaChanged` 失效重建）：即使加了线程门和 Level 门，全类读取 Hook 仍无法穷尽区分渲染读者与游戏逻辑读者，且会污染客户端对世界的认知，违背“纯客户端、不修改游戏内容”的产品边界。液体渲染只允许 LHolo 自绘网格方案。
- 同一位置同时绘制投影模型、纠错外壳和原版 hit-select 的多层共面方案。
- 通过扩大外壳几何长期规避 Z-fighting；相邻方块会产生新重叠。
- 未做共享面剔除的每方块完整六面叠加。
- 每帧全量扫描整个结构。
- 整体移动时重建所有 GPU 网格。
- 只在 ResizeBuffers 清理 RTV 的 F11 修复。
- GUI 使用游戏内置表单替代外部 ImGui。
- 写死导入路径。
- 跨世界缓存世界对象或 ECS 裸指针。

---

## 18. 日志和故障文件

测试实例日志：

```text
D:\games\LeviLauncher\MC\versions\1.26.20.04\logs\latest.log
```

崩溃文件：

```text
D:\games\LeviLauncher\MC\versions\1.26.20.04\logs\crash\trace_*.log
D:\games\LeviLauncher\MC\versions\1.26.20.04\logs\crash\minidump_*.dmp
```

排障优先级：

1. 确认实际加载 DLL 的 SHA256 与最新构建一致。
2. 确认 Minecraft/LeviLamina 精确版本。
3. 查看 Hook 安装、结构加载和图形 HRESULT 日志。
4. 图形异常区分普通路径与 Deferred 路径。
5. 输入异常记录窗口模式、鼠标坐标、GUI 打开状态和触发消息。
6. 崩溃使用 trace/minidump/IDA 定位，不根据画面表现猜函数偏移。

维护原则：先证明根因，再改动；能通过稳定公开/生成 API 完成时不依赖固定偏移；必须逆向时记录版本、符号、签名、调用点和验证依据。
