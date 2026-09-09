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

LHolo 的投影、纠错、HUD 和菜单都只存在于客户端，不产生碰撞，也不会把虚拟方块或液体写入世界。玩家可以穿过投影，服务器无需安装配套插件。可选的轻松放置、手动放置和范围放置会以本地玩家身份发送正常的背包/放置事务；最终方块由服务端按玩家权限权威落地，因此这些放置功能会像玩家手动放置一样修改世界。

正式版功能：

- 从任意用户选择的路径加载 `.mcstructure` 或 `.litematic`，导入目录不写死。
- 以加载时玩家脚下的整数方块坐标作为投影锚点；恢复记录时使用保存的锚点。
- 支持 X/Y/Z 结构偏移、0°/90°/180°/270°旋转、X 或 Z 镜像。
- 支持 Y 轴水平分层和 X 轴纵向切片。
- 显示范围支持“完整结构”“单层”“当前层及以下”“当前层及以上”。
- 投影透明度 0～100%，默认 100%。
- 纠错状态：未放置为蓝色、方块类型错误为红色、方向或方块状态错误为黄色；蓝图空气位置存在实体方块时以品红色标记为多余方块，完全正确时隐藏。
- 纠错提示透明度默认 15%，描边透明度默认 100%；均为 0～100 整数输入、即时生效、持久化保存，可一键恢复默认值。
- 可选整体结构边框。
- 支持准心轻松放置、按住右键的手动放置，以及半径 1～4 的范围放置；三种模式在 GUI 中互斥。
- `.mcstructure` 中带 NBT 的方块实体优先使用原版方块实体渲染器；没有可用渲染器或 Tessellation 结果的方块使用贴图占位外壳。
- HUD 可显示文件名、显示层、建造进度、放置错误数、朝向错误数、多余方块数和准心指向的投影方块名称；支持四角定位和单项关闭，各类错误可分别配置。
- GUI 使用外部注入 Dear ImGui，不使用游戏表单。
- 默认 `Alt + M` 打开菜单；聊天栏输入 `LHolo`（ASCII 大小写不敏感）也可打开，消息在客户端拦截，不发往服务器。
- LHolo 菜单打开及关闭过渡期间，客户端阻止本地控制玩家开始或继续破坏方块；本地存档和远程服务器均有效，服务器无需安装 LHolo。
- 默认结构移动：`Ctrl + 方向键` 调整 X/Z，`Shift + ↑/↓` 调整 Y。
- 默认显示层：`Alt + ↑/↓`；“完整结构”模式下按键无效。
- 支持保存和恢复上次投影文件、锚点及当时的变换/分层参数。
- “创建结构”页支持用玩家脚下位置或手动 XYZ 设置包含端点的选区，以红色整体线框持续显示，并通过原版结构 API 导出 `.mcstructure`。目前只提供客户端模式，且只读取当前已加载范围；实体默认不包含。
- 退出世界、切换存档或失去有效客户端上下文时清理投影，禁止跨世界复用世界对象；完成清理后在底部提示投影已关闭。
- 仅切换维度时保留已加载结构、锚点、变换、HUD 配置和辅助放置模式。投影与原维度绑定：进入其他维度时释放旧维度运行资源、隐藏投影及 HUD，并在底部提示投影已暂停；返回原维度并成功按原锚点重建后提示投影已恢复，材料 HUD 随恢复后的投影重新统计。投影生命周期状态提示均显示 2.5 秒。

不在当前范围内：

- 不绕过玩家背包、放置距离、方块状态预测、服务端校验或权限进行无条件自动建造。
- 不把投影本身写入世界，也不同步投影、纠错状态或 GUI 给其他玩家。
- 不支持旧测试命令 `sp`、`sp <block>` 或 `spgui`。
- 不保留早期单方块测试渲染、单方块射线选中或首次预热链路。

---

## 2. 项目目录与模块职责

```text
LHolo/
├─ src/
│  ├─ plugin/
│  │  ├─ LHolo.cpp              模组启停，只委托给 AppKernel
│  │  ├─ LHolo.h
│  │  └─ MemoryOperators.cpp    Windows 客户端内存运算符适配
│  ├─ app/
│  │  └─ AppKernel.*            唯一应用入口：启停编排与加载顺序
│  ├─ block/
│  │  └─ BlockPlacementRules.*  共享运行态方块身份与“方块→实际放置物品”规则
│  ├─ settings/
│  │  └─ SettingsStore.*        config.json 读写与字段映射
│  ├─ overlay/
│  │  ├─ ImGuiOverlay.cpp       DXGI/D3D11On12、WndProc、GUI/HUD 帧提交
│  │  ├─ ImGuiOverlay.h
│  │  └─ BoundsWireframe.*      创建结构选区的红色整体线框
│  ├─ structure/
│  │  ├─ capture/               客户端选区状态、原版结构捕获与 `.mcstructure` 导出
│  │  ├─ formats/               结构格式解析与 generation 分配
│  │  │  └─ StructureFormatLoaders.* `.mcstructure`/`.litematic` 解析为 LoadedStructure
│  │  ├─ java_to_bedrock/       Java 方块状态映射、文本组件与方块实体到 Bedrock 的转换
│  │  ├─ StructurePaths.*       UTF-8 路径转换
│  │  ├─ MaterialTracker.*      材料需求聚合、物品标识缓存与游戏线程背包快照
│  │  ├─ StructureSession.*    结构会话状态（已加载结构、变换/分层、恢复快照、状态文案）
│  │  ├─ StructureUiState.*     UI 会话状态（GUI、热键、HUD、待处理动作、材料清单）
│  │  ├─ StructureLoader.cpp    HUD、快捷键事件、加载入口与公开 getter（菜单/GUI 由 MenuController 负责）
│  │  └─ StructureLoader.h      LoadedStructure 统一数据模型
│  ├─ ui/
│  │  ├─ FileDialog.*           通用结构打开与 `.mcstructure` 保存对话框
│  │  ├─ HotkeyFormat.*         纯按键名/修饰键格式化（不读会话状态）
│  │  ├─ MenuController.*       菜单模型构建/应用、路径缓冲、页面状态、动作回调与 GUI 渲染
│  │  ├─ MenuWidgets.*          通用菜单控件（分节、行、步进器、居中数值）
│  │  ├─ MenuPages.*            各页面渲染、导航与材料清单弹窗
│  │  └─ LHoloMenu.*            纯菜单模型、页面分发与动作回调类型
│  ├─ projection/
│  │  ├─ Projection.cpp         对外门面：Hook 生命周期、设置与查询转发
│  │  ├─ Projection.h           GUI/HUD 和辅助放置使用的投影控制接口
│  │  ├─ ProjectionTypes.h      对外查询与进度的纯数据类型
│  │  ├─ ProjectionController.* 投影 Hook 安装/回滚与禁用入口
│  │  ├─ core/                  内部共享模型、资源所有权与纯规则
│  │  │  ├─ ProjectionInternalTypes.h 内部键、状态枚举与无资源引用类型
│  │  │  ├─ ProjectionRules.*   坐标、分层、状态匹配和渲染分类纯规则
│  │  │  └─ ProjectionState.h   世界身份、CPU 状态、Worker 状态与 Mesh 所有权
│  │  ├─ mesh/                  Mesh CPU 构建、Worker、上传与渲染提交
│  │  │  ├─ ProjectionMeshScheduler.* 主线程 section 选择、局部快照与同步回退调度
│  │  │  ├─ ProjectionMeshUpload.* 主线程完成队列校验、MeshData 上传与失败回退
│  │  │  ├─ ProjectionMeshWorker.* 单线程 executor、generation 与完成队列
│  │  │  ├─ ProjectionRenderer.* 已构建 Mesh/方块实体的 pass 分类与材质提交
│  │  │  └─ ProjectionSectionBuilder.* Worker/同步共用的 section CPU 几何构建
│  │  ├─ section/               section 状态结构与初始化
│  │  │  └─ ProjectionSectionStateStore.* 并行数组合并后的 SectionState 与初始化
│  │  ├─ correction/            纠错状态机与进度维护
│  │  │  └─ ProjectionCorrectionTracker.* 有界扫描、事件刷新、非空气四态/稀疏多余方块纠错与六邻居失效
│  │  ├─ world/                 投影局部世界视图与对外只读查询
│  │  │  ├─ ProjectionPlacement.* 变换/分层后的虚拟世界与方块实体放置视图
│  │  │  ├─ ProjectionQueries.* 辅助放置使用的只读单格与范围查询
│  │  │  └─ ProjectionVirtualWorld.* Tessellation 线程局部虚拟方块与方块实体视图
│  │  ├─ runtime/               投影会话生命周期与每帧运行时协调
│  │  │  ├─ ProjectionFramePipeline.* opaque 帧纠错、上传、边框与构建调度流水线
│  │  │  ├─ ProjectionInvalidation.* 设置变化检测、section/revision 失效与缓存清理
│  │  │  ├─ ProjectionLifecycle.* ProjectionState 资源准备、停止清理与世界身份匹配
│  │  │  ├─ ProjectionProgress.* 渲染线程到 HUD 的无锁进度快照发布
│  │  │  ├─ ProjectionRenderFrame.* 帧编排：结构激活、世界校验、提交与命中抑制
│  │  │  ├─ ProjectionSession.* 会话状态存储与访问契约
│  │  │  └─ ProjectionWorldEvents.* 真实世界方块/子区块通知的监听与排队
│  │  └─ hooks/
│  │     ├─ ProjectionGameHooks.* 无状态 BlockSource 虚拟查询与客户端命令 Hook
│  │     └─ ProjectionRenderHooks.* 有状态 LevelRendererPlayer 渲染 Hook 薄壳
│  ├─ place/
│  │  ├─ PlaceHelper.cpp        放置 Hook、开关与公开接口
│  │  ├─ PlacementState.*       放置会话状态（开关、定时、近期格、失败计划缓存）
│  │  ├─ PlacementExecutor.*    轻松/手动/范围放置规划与执行
│  │  └─ PlaceHelper.h          配置开关与 Hook 生命周期接口
│  └─ input/
│     ├─ HotkeyTypes.h          快捷键槽位、顺序与数量的唯一共享定义
│     ├─ MenuInputGuard.cpp     Bedrock 鼠标与 HID 键盘输入源拦截
│     └─ MenuInputGuard.h       输入保护安装状态与生命周期接口
├─ tools/java_to_bedrock/       开发期映射生成器（运行时不需要 Java）
├─ manifest.json                Mod Packer 模板
├─ xmake.lua                    依赖、编译选项和发布规则
├─ DEVELOPMENT.md               本文档
├─ THIRD_PARTY_NOTICES.md       源码仓库中的 Chunker 许可说明，不打包
├─ build/                       xmake 中间产物，不发布
└─ bin/LHolo/                   唯一发布目录
   ├─ LHolo.dll
   ├─ manifest.json
   └─ LICENSE
```

投影子系统依赖方向（只允许从上往下，禁止反向）：

- `core/` 不依赖 `world/`、`mesh/`、`runtime/`、`hooks/` 与门面。
- `section/` 只依赖 `core/`，提供 `SectionState` 的初始化与状态存储。
- `world/` 只依赖 `core/`；不依赖 mesh/runtime/hooks 与门面。
- `mesh/` 依赖 `core/` 与 `world/`；不依赖 runtime/hooks 与门面。
- `runtime/` 依赖 `core/`、`world/`、`mesh/`；不依赖 hooks 与门面。
- `hooks/` 依赖 `world/` 与 `runtime/ProjectionRenderFrame` 回调契约；不直接持有投影状态。
- `projection/ProjectionController` 只编排 Hook 安装/回滚与投影禁用，不包含渲染帧逻辑。
- `app/AppKernel` 是唯一启停入口，依赖 `ProjectionController` 与各外部模块；`plugin` 只调用 `app/`。
- 内部模块不得 include `projection/Projection.h`；只有门面自身和外部消费者（`place`、`ui`、`plugin`）可以。
- `ProjectionSession` 是运行时可变状态的唯一所有者；状态算法只能通过会话加锁的作用域回调运行，
  mutex、`ProjectionState` 和捕获线框不能被分别取得。

模块边界必须保持清晰：

- `structure` 负责“文件和用户意图”，不直接提交 Minecraft 网格。
- `structure/capture` 只维护会话选区、读取当前客户端世界并调用原版捕获/导出 API；不手工生成方块调色板、索引或实体 NBT。
- `structure/formats` 只把 `.mcstructure`/`.litematic` 解析为 `LoadedStructure` 并分配 generation；不接触
  GUI、配置、投影状态或放置逻辑，也不依赖 `projection`、`ui`、`place`。
- `structure/java_to_bedrock` 只负责 Java 数据到 Bedrock 运行时数据的边界转换：方块状态继续使用生成映射，
  方块实体由独立转换器生成原生 Bedrock NBT；不得让 Java NBT 字段泄漏到 `projection`。
- `settings/SettingsStore` 只负责 `config.json` 的读写与字段映射，不持有运行状态；应用到全局状态由
  `StructureLoader` 完成，配置默认值与钳制语义保持不变。
- `block/BlockPlacementRules` 集中维护运行态方块到可放置基础方块的身份规则，并通过 Bedrock 原生
  `Block`/`ItemStack` 转换解析实际放置物品；`place`、材料统计和投影纠错不得各自复制名称映射。
- `structure/MaterialTracker` 只在本地玩家 tick 线程聚合材料需求和扫描背包；同一投影的材料清单只计算
  一次，先按唯一方块状态累计数量、再解析并缓存 `itemId`，避免大型结构逐格访问物品注册表。HUD/菜单
  关闭且没有消费者时停止刷新，渲染线程只读取 `StructureUiState` 的一致快照。
- `structure/StructureUiState` 持有 UI/菜单会话状态：GUI 可见性、热键配置与按下状态、HUD 开关、
  待处理偏移/切层/保存与材料清单；atomic、mutex 和容器均不向调用者暴露，交互逻辑仍在
  `StructureLoader`，菜单线程专属的路径输入缓冲和当前页面由 `MenuController` 私有持有。
- `structure/StructureSession` 持有结构会话状态：已加载结构、变换/分层值、恢复快照与状态文案；
  mutex、原子量、字符串和容器均不向调用者暴露，菜单/HUD 通过一致快照读取，变更通过具体操作完成。
  关闭活动投影时必须先把当前变换/分层冻结到恢复快照，再释放 `LoadedStructure`；空会话将显示层钳到
  0 的临时 UI 值不得覆盖恢复记录。
- `ui/HotkeyFormat` 只根据显式参数生成按键名与和弦字符串，不读取会话状态；快捷键交互仍由
  `StructureLoader` 的会话状态驱动。
- `input/HotkeyTypes` 是快捷键槽位顺序与数量的唯一来源；设置、会话状态、菜单和输入处理必须从
  `HotkeyId`/`Count` 推导索引与数组大小，不得复制数字槽位。
- `ui/MenuController` 负责菜单模型构建/应用、动作回调与 GUI 渲染，只通过 `StructureSession`/
  `StructureUiState` 访问状态；`StructureLoader` 保留 HUD、快捷键事件与加载入口。
- `ui/MenuWidgets` 提供通用菜单控件（分节、值行、复选框、步进器、居中数值绘制），不读取会话状态。
- `ui/MenuPages` 承载各页面渲染、导航指示与材料清单弹窗；`LHoloMenu.cpp` 只保留 `renderMenu` 分发。
- `structure/StructurePaths` 提供 UTF-8 路径转换，结构模块与 UI 共用。
- `projection` 负责“结构如何出现在世界中”，不弹文件选择框、不直接操作 ImGui。
- `projection/ProjectionTypes.h` 不包含运行状态、Hook 或 Minecraft 资源所有权；内部纯数据定义集中在
  `projection/core/ProjectionInternalTypes.h`，投影状态与 Worker/Mesh 生命周期仍由实现层负责。
- `projection/core/ProjectionRules.*` 只根据显式参数计算结果，不读取全局投影状态，也不持有游戏对象；
  旋转、镜像、分层可见性和状态匹配规则修改时应集中在这里回归。
  方块状态旋转/镜像必须使用当前 Bedrock 的
  `VanillaBlockStateTransformUtils::transformBlock`；禁止退回只覆盖旧 aux data 的
  `LegacyStructureTemplate::_mapToData`，否则 `rail_direction` 等现代状态不会随结构变换。
- `projection/correction/ProjectionCorrectionTracker.*` 在固定每帧预算内比较真实世界与投影单元，维护纠错状态和进度计数；
  它可以标记受影响 section，但不创建 Mesh、不访问 Tessellator，也不发布 HUD 原子状态。
- `projection/runtime/ProjectionFramePipeline.*` 只在 opaque pass 按固定顺序执行纠错扫描与进度发布、完成队列上传、
  轻量结构边框准备和下一 section 调度；它不采集 GUI 设置、不判断 placement 失效，也不提交最终渲染 pass。
- `projection/hooks/ProjectionGameHooks.*` 隔离不依赖活动投影状态的 Minecraft 接口：Tessellation thread-local
  虚拟 BlockSource 查询和 `/lholo` 客户端命令包拦截；安装失败时必须按原逆序回滚已挂 Hook。
- `projection/hooks/ProjectionRenderHooks.*` 只保留有状态渲染 Hook 薄壳：命中选择抑制与 `$renderBlockEntities`
  后的投影帧入口。Hook 不读取投影状态，仅调用 `runtime/ProjectionRenderFrame` 回调；两个 Hook 的安装失败
  回滚顺序与门面原有的 LevelRenderer 顺序保持一致。
- `projection/runtime/ProjectionRenderFrame.*` 在 runtime 内实现结构激活、世界身份校验、opaque/transparent
  Mesh 提交、命中选择抑制和 `$renderBlockEntities` 后的帧入口；它只通过 `ProjectionSession` 访问会话状态，
  不直接触碰 `Projection.cpp` 的全局变量。
- `projection/runtime/ProjectionSession.*` 持有全部会话级可变状态：状态锁、`ProjectionState`、捕获边框、
  透明度、结构边框开关与下一次锚点；帧编排和只读查询通过锁内作用域回调操作状态，门面通过具体设置/
  锚点操作访问会话，`Projection.cpp` 不直接取得锁或可变状态引用。恢复锚点只属于紧随其后的恢复激活；
  显式关闭/禁用或成功发起普通文件加载时必须撤销尚未消费的请求，禁止旧锚点泄漏到后续结构。
- `projection/runtime/ProjectionInvalidation.*` 对比当前帧设置与缓存值，集中处理旋转/镜像、移动、切层、透明度和
  纠错样式变化引起的 section dirty、revision、旧 Mesh 清理及可见进度重算；placement 重建仍由调用方触发。
- `projection/runtime/ProjectionLifecycle.*` 构造尚未激活的 `ProjectionState`、解析 terrain atlas、建立 section 索引，
  并按 Worker 停止、世界事件解绑、进度清零、资源释放的顺序执行锁内清理；它将当前上下文分类为正常、
  维度变化、暂不可用或世界变化。锁、pending anchor 消费、状态激活、跨维度暂停/恢复和解锁后的
  `structure::clear()` 由 `ProjectionRenderFrame`/`ProjectionSession` 负责，门面只转发。
- `projection/runtime/ProjectionProgress.*` 保持原有 acquire/release 语义，在渲染状态计数与 GUI/HUD 读取之间发布
  无锁快照；它不扫描世界，也不自行推导纠错结果。
- `projection/mesh/ProjectionSectionBuilder.*` 统一生成原版方块、液体代理、方块实体占位、纠错覆盖和结构边框
  的 CPU 几何；构建输入必须来自显式只读设置，Worker 使用 `UploadMode::Never`，同步回退保持 `Buffered`。
- `projection/core/ProjectionState.h` 只声明投影运行态及资源所有权；创建、替换、清理和跨世界校验由
  `ProjectionLifecycle`/`ProjectionRenderFrame` 统一执行，状态类型本身禁止暗藏线程启动或游戏 API 调用。
- `projection/mesh/ProjectionMeshWorker.*` 只管理单线程 executor、任务异常边界、generation 失效和完成队列；
  Worker 任务由调度层提供，基础设施不得读取 `gState`、世界对象或渲染上下文。
- `projection/mesh/ProjectionMeshScheduler.*` 仅在 opaque 主线程选择 dirty section、准备两格 halo 的只读世界视图并
  提交 Worker Task；增量优先、距离排序、revision 合并和连续三次失败后的同步回退策略集中在这里。
  Task 只持有显式快照，不得捕获 `gState`、`renderContext` 或活动共享 `BlockTessellator`。
- `projection/mesh/ProjectionMeshUpload.*` 仅在主线程消费完成队列，校验 Worker/结构 generation 与 section revision，
  按每帧两个 section、1 ms 预算上传 `MeshData`；连续失败三次时保持原有同步回退策略。
- `projection/world/ProjectionPlacement.*` 在变换、偏移或分层变化后重建虚拟方块/方块实体表、世界坐标索引、
  section 中心与 Tessellator 查询缓存；它不生成 Mesh、不上传 GPU，也不调度 Worker。
- `projection/world/ProjectionQueries.*` 只在调用方已持有投影状态锁且完成世界身份校验后读取虚拟世界索引和
  纠错状态，为手动/轻松/范围放置返回单格或按距离排序的缺失方块；它不持锁、不清理状态，也不修改世界。
- `projection/mesh/ProjectionRenderer.*` 只提交已经上传完成的 Mesh 和投影方块实体：保持 opaque/transparent pass
  分类、透明 section 后向前排序、液体/方块实体占位、结构边框和纠错覆盖的现有材质语义；方块实体仍在
  局部虚拟世界作用域内复用原 dispatcher 参数。它不生成 CPU 几何、不消费 Worker 结果，也不改变
  projection 生命周期；资源预检和提交异常后的清理由 `runtime/ProjectionRenderFrame` 负责。
- `projection/world/ProjectionVirtualWorld.*` 隐藏 Tessellation 使用的 thread-local 虚拟方块表；只有显式 RAII
  作用域内的 `BlockSource` Hook 查询可以命中投影邻居，作用域结束后必须恢复上一个视图。
- `projection/runtime/ProjectionWorldEvents.*` 只监听真实世界的方块变化和子区块加载并按原顺序排队；它不读取
  `gState`，也不决定 section 如何失效；事件消费、纠错更新和 dirty 传播由
  `ProjectionFramePipeline`/`ProjectionCorrectionTracker` 负责。
- `overlay` 负责“外部 GUI 如何安全进入游戏图形链”，不解析结构或扫描世界方块。
- `place` 负责轻松、手动和范围放置：调用 projection 查询接口，在完整背包中查找物品，必要时交换到快捷栏，并发送 `InventoryTransactionPacket`；不碰渲染与配置。
- `place/PlacementState` 持有放置会话状态：开关、手动/范围定时、近期放置格、失败计划缓存与准心投影方块名称；
  atomic、mutex、字符串和容器均不向调用者暴露，`PlaceHelper`/`PlacementExecutor` 只通过具体操作读写。
- `place/PlacementExecutor` 承载轻松/手动/范围放置的规划与执行（背包查找、交换、放置事务、
  点击候选与预测匹配）；游戏 Hook 留在 `PlaceHelper`，只调用 `tickEasyPlace`/`tickRangePlace`。
- `input` 负责菜单期间的 Minecraft 输入边界：在 `MouseDevice`/`HIDControllerGameCoreDesktop` 输入源阻断游戏与原生页面输入；是否捕获统一读取 `StructureLoader::isMenuInputCaptured()`，不得在各输入入口分别组合 GUI 与过渡状态。
- `plugin` 只把生命周期委托给 `app/AppKernel`，不承载业务逻辑。

---

## 3. 启动、关闭与世界生命周期

### 3.1 模组启用

`LHolo::enable()` 的顺序：

1. 安装投影相关 LeviLamina Hook。
2. 安装辅助放置 Hook：`LocalPlayer::$tickWorld` 负责每 tick 驱动，三个 `GameMode` build Hook 负责命中真实方块时的右键状态和原版放置抑制，`GameMode::$useItem` 负责捕获指向空气的右键操作（tick Hook 失败仅告警，不阻断；单个手动 Hook 失败会分别告警并降级对应行为）。
3. 安装菜单输入保护：`MouseDevice::feed` 与 `HIDControllerGameCoreDesktop::$onKeyDown/$onKeyUp` 在游戏和原生 UI 处理前取得输入所有权。三项 Hook 状态独立告警，不阻断菜单启用。
4. 尝试安装 ImGui/DXGI Hook；图形环境尚未可用时允许后续 `Present` 重试。

配置由 `LHolo::load()` 在 enable 之前从 `mods/LHolo/config/config.json` 读取。当前没有单独依赖世界退出事件；投影渲染入口通过 `client/level/dimension` 身份变化检测世界切换，并在上下文失效时调用 `projection::disable()` 等价的状态清理和 `structure::clear()`。

投影启用入口只有 `enableStructureProjection()`。它要求：

- 本地玩家存在。
- `LoadedStructure` 非空且至少有一个可渲染方块。
- Minecraft level atlas 已可用。
- 创建同步回退使用的 `BlockTessellator`，并启动单线程 `LHoloProjectionMesh` Worker。Worker 启动失败时投影仍可启用，但本次游戏会话固定使用同步回退。

### 3.2 模组关闭

当前关闭顺序：

1. 保存配置。
2. 投影停止接收网格任务，提升 Worker generation，清空待处理结果并等待 in-flight Worker 退出；随后清理投影状态和 GPU 网格。
3. 卸载菜单鼠标/HID 输入源 Hook。
4. 卸载辅助放置的 tick/build Hook。
5. 关闭 ImGui 图形后端、恢复原 WndProc、移除 MinHook。
6. 清除已加载结构、菜单和快捷键运行态。
7. 卸载投影 Hook。

### 3.3 世界切换

`ProjectionState` 保存 `IClientInstance*`、`Level*`、`Dimension*`，仅用于验证当前上下文是否仍为创建投影时的世界。每次渲染先调用 `contextIsValid()`；不一致时立即清空投影和已加载结构。

创建结构选区独立保存当前 `Level*` 和 `Dimension*` 身份。离开世界或身份变化时恢复客户端模式、清空两个端点和“包含实体”，红色线框随即释放；这些会话状态不写入配置。

绝对禁止：

- 跨世界保留 `BlockSource`、ECS Storage、组件指针或实体裸指针。
- 仅依据玩家是否为空判断世界相同。
- 在旧维度的 `BlockTessellator` 上继续查询新维度。
- 持有 projection 的 `gStateMutex` 时调用 `structure::clear()`：后者会回调
  `projection::disable()` 并再次获取同一把锁。必须先在锁内清理投影状态，再解锁后清理结构模块。

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
- `RenderBlock{x,y,z,block,liquid,blockEntityNbt}`：归一化局部坐标、可空实体方块指针、可空液体指针和可选的原生 Bedrock 方块实体 NBT。同一坐标可同时具有实体与液体，用于含水方块；`.mcstructure` 直接保留原生 NBT，`.litematic` 仅为已有明确转换规则的方块实体生成 NBT。

### 4.2 `.mcstructure`

加载流程：

1. 读取文件，单文件上限 512 MiB。
2. 使用 Bedrock little-endian binary NBT 解析。
3. 校验 `size`、`structure.block_indices`、`palette.default.block_palette`。
4. 校验两个 block index layer 的长度等于结构体积。
5. 使用客户端 Level 的 unknown-block registry 构造原版 `StructureTemplate`，再把完整根 NBT 交给 `StructureTemplate::load()`。不要逐项调用 `Block::tryGetFromRegistry()`：那条捷径会绕开格式版本升级、世界方块调色板和 unknown-block registry，旧状态可能被错误解析成未知方块。
6. 从加载后的 `StructureTemplateData` 取得原版已升级的主/副索引数组和 `StructureBlockPalette`，用 `StructureBlockPalette::tryGetBlock()` 解析方块。不要用 `StructureTemplate::tryGetBlockAtPos()` 遍历文件：26.20 客户端该接口的坐标访问约定与 `.mcstructure` 的线性索引布局不一致，曾导致门上下半块错位和水取成错误方块。
7. 依照格式文档的 ZYX 顺序还原线性索引：`index = x * (sizeY * sizeZ) + y * sizeZ + z`。主副层分别解析后，同一坐标的非液体写入实体层、液体写入液体层。
8. 门的上下半块本来就是两个坐标、两个完整 palette state，不做合并；格式升级后的上下半块、铰链、朝向和开关状态由原版加载器保留。
9. 原版加载失败、原版尺寸与文件尺寸不一致时直接拒绝加载，不再带着未知方块继续渲染。

适配新版本时重点检查：`StructureTemplate` 构造与 `load()`、`StructureTemplateData` 索引访问、`StructureBlockPalette::tryGetBlock` 的符号及语义、NBT 标签路径和两个 block index 的格式。固定回归门的上下相邻坐标与水坐标；不要退化回手工注册表解析，也不要未经验证改用 `tryGetBlockAtPos()` 遍历。

渲染与纠错约束：

- 实体模型走原版 `tessellateInWorld()`，并按原版 render layer 分桶。相邻实体方块只在 LHolo 生成网格的线程局部作用域内通过 `BlockSource::getBlock()` 暴露，供门、栅栏等邻居相关模型正确生成；作用域外始终调用原版函数，不改变世界。
- 所有投影方块实体创建完成后，使用 `BlockActor::isType()` 识别箱子，并在同一虚拟世界作用域内调用原版 `ChestBlockActor::_tryToPairWith()` 配对。必须先建立完整的虚拟方块和方块实体表，再执行配对；结构 NBT 中的 `pairx`/`pairz` 是原世界绝对坐标，不能直接作为投影配对坐标使用。
- 水和岩浆使用贴图 proxy 单元壳，完全由 LHolo 自绘，不与原版世界或区块管线交互：仅 Missing（未放置）状态的液体格绘制半透明截顶外壳，最上层液体格顶面固定为原版源液体高度 8/9（`getHeightFromDepth()` 在 1.26 上对源液体的返回值不可靠，不再使用；逐格流动深度不参与视觉，只参与纠错比较），上方有同液体时侧壁满格；相邻同种液体剔除共享面；UV 取自 `BlockGraphics::getForBlock(liquid)->getTexture(0, 0)` 的 terrain atlas 水/岩浆贴图；水顶点色为原版蓝 #3F76E4（atlas 水贴图无色），岩浆白色顶点色保留贴图原色；alpha 跟随投影透明度；经 `liquidProxySectionMeshes` 独立网格在 alpha pass 用 `mMatBlendBlock` + terrain atlas 提交（与玻璃同路径），按 section 距离排序。静态贴图无波浪动画是已知限制。纯液体格的 Missing 不再叠加蓝色纠错面/描边（proxy 本身即提示），WrongType/WrongState 仍保留红/黄纠错面。`.litematic` 加载时液体路由到 `RenderBlock::liquid` 字段，与 `.mcstructure` 语义一致。
- `.litematic` 加载时把 `getMaterial().isLiquid()` 的方块路由到 `RenderBlock::liquid` 字段，与 `.mcstructure` 语义一致。
- 纠错分别比较 `BlockSource::getBlock()` 与 `getLiquidBlock()`。缺少液体判为“未放置”，液体类型错误判为“类型错误”，液体深度等状态不同判为“状态错误”。
- 投影进度仍以结构坐标计数，而不是把同一坐标的实体层和液体层重复计数。

不要重新引入 `tessellateLiquidInWorld()` 自行提交或把虚拟液体注入 `BlockSource`/区块管线的方案。前者已出现黑块、过曝和未知方块纹理，后者会让游戏逻辑读取到虚拟液体，污染客户端世界认知。当前唯一正式方案是上述 `liquidProxySectionMeshes` 贴图 proxy：它只进入 LHolo 自己的网格和渲染提交，不触发区块重建，也不修改世界。

### 4.3 `.litematic`

加载流程：

1. 读取文件；检测 gzip 头 `1F 8B`，使用 zlib 解压，解压上限 1 GiB。
2. 使用本项目只读 Java big-endian NBT 解析器读取根 Compound。
3. 读取根 `MinecraftDataVersion`，用于选择与源文件版本对应的映射记录。
4. 遍历 `Regions`，读取每个区域的 `Position`、有符号 `Size`、`BlockStatePalette`、`BlockStates` 和 `TileEntities`；区域自己的 `DataVersion` 优先于根 `MinecraftDataVersion`。
5. 通过 `java_to_bedrock` 模块把完整 Java 名称和 Properties 转换成 Bedrock 1.26.20 名称与状态，再从当前游戏的 `BlockTypeRegistry` 解析实际 permutation；含水状态拆到 `RenderBlock::liquid` 第二层。
6. 每项位宽为 `max(2, bit_width(paletteSize - 1))`，从 LongArray 解包 palette index。
7. Litematica 的 `BlockStates` 容器始终从区域最小角按正 X/Y/Z 排列。有符号 `Size` 只记录 `Position` 是哪一个选区角，绝不能依据负号倒序读取方块数组，否则会镜像结构而不镜像方块状态。先由 `Position` 和 `Size` 求区域最小角，再加 `localX/localY/localZ`。
8. Litematica 源码在保存时使用 `方块实体局部坐标 = 世界坐标 - 区域最小角`，加载时也直接按容器 `(localX,localY,localZ)` 查表；因此 `TileEntities` 与方块数组使用同一局部坐标，不随负 `Size` 额外镜像。版本 2 及以后直接读取实体 Compound，版本 1 从 `TileNBT` 包装读取实际数据。
9. 当前 Java 方块实体转换器支持普通告示牌和悬挂告示牌：现代 `front_text`/`back_text.messages` 与旧版 `Text1`～`Text4` 转为 Bedrock `FrontText`/`BackText`，四行 Java JSON 文本组件由 `nlohmann::json` 解析为纯文本，同时保留发光和蜡封状态。当前固定样本为黑色文字；其他染料色必须取得对应 Bedrock 导出样本后再增加映射，不猜测色值。
10. 计算所有区域的全局最小/最大坐标，将多区域合并到从 `(0,0,0)` 开始的正坐标包围盒。方块和转换后的方块实体作为同一个 cell 原子覆盖，禁止分别合并，否则重叠区域可能留下旧方块实体。
11. 重叠坐标使用后处理区域覆盖先处理区域，最终按 `(x,y,z)` 排序。

Java→Bedrock 映射不再手工散落维护。`GeneratedChunkerMappings.inc` 由固定 Chunker commit 的 `JavaBlockIdentifierResolver` 和 `BedrockBlockIdentifierResolver` 枚举各 Java 数据版本的合法状态生成，目标固定为 Bedrock 1.26.20。缺少 `Properties` 的非标准 Litematic 使用 Chunker 的规范默认状态；无法映射或无法在当前游戏注册表解析的方块安全跳过，不猜测替代 API、状态名或枚举值。

生成器位于 `tools/java_to_bedrock/`，更新方法见该目录 README。Java、Gradle 和 Chunker 只在重新生成映射时使用，正式 LHolo 运行时只读取已压缩进 DLL 的生成表。手动运行生成脚本时，`javac` 的临时 `.class` 文件写入 `build/java-to-bedrock-generator/`；普通 `xmake` 构建不会生成这些文件，该目录也不得发布。更新 Chunker 或目标 Bedrock 版本后必须重新生成、检查 unsupported 输出、完成 Release 构建，并由用户在游戏中手动验证固定样本。

运行时映射模块缓存 `BlockTypeRegistry` 拥有的 permutation 裸指针。Minecraft 退出世界时会注销或重建这些方块对象，因此缓存不得跨世界保留：`clear()` 必须调用 `resetJavaBlockMappingCache()`，每次 `loadLitematic()` 在确认当前客户端 Level 有效后也必须先重置缓存。重置同时清空 permutation 表和含水映射使用的 `gWaterSource`；禁止仅清除 `LoadedStructure` 而遗留 Java 映射指针。

### 4.4 文件安全约束

- 所有数组长度在分配前检查负值和上限。
- gzip 解压必须限制总输出，防止压缩炸弹。
- 体积乘法使用 64 位整数。
- 坐标范围在转换为 `int` 前检查溢出。
- 解析异常转换为用户可见错误，不允许越界继续。

### 4.5 客户端创建与导出

“创建结构”不经过 `LoadedStructure`，也不会自动载入投影。选区端点先按每轴最小值/最大值归一化，两个端点都包含在内；导出前由 `BlockSource::areChunksFullyLoaded(min, max)` 拒绝客户端尚未完整加载的范围。

捕获只使用 LeviLamina 26.20.7 客户端头文件确认的原版接口：

1. `ll::service::getClientInstance()` 和 `ClientInstance::getLocalPlayer()` 获取当前客户端玩家。
2. `Actor::getDimensionBlockSource()` 取得当前维度的 `BlockSource`。
3. `StructureTemplate::create(name, blockSource, BoundingBox{min, max}, false, !includeEntities)` 同步捕获。
4. `StructureManager::exportStructure(template, Core::Path)` 写出官方 `.mcstructure`。

不得改用依赖服务端 `ll::service::getLevel()` 的 NBT 重载，不调用 `StructureBlockActor::_saveStructure()`，也不增加手工 palette、索引、实体 NBT 或备用序列化路径。捕获过程中不异步读取 `BlockSource`，不每帧扫描选区。

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
- 方块朝向/状态：`VanillaBlockStateTransformUtils::transformBlock()`，旋转和镜像取自同一份
  `LegacyStructureSettings`。

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
- Worker 中每个 biome-tinted 方块（草和四种 foliage tint）在 Tessellate 前都调用 `BlockTessellator::buildBiomeWeights()`，禁止复用空缓存或上一方块位置的群系权重。独立投影 Tessellator 不经过原版区块管线的树叶着色步骤，因此四种 foliage tint 还使用 `BiomeColorSampling::getTessellationPolicy()` 计算原版群系颜色并与网格顶点色相乘；草方块仍由原版 Tessellator 按面着色，不能把整块顶点统一乘绿色，否则泥土面也会变色。树叶类型和颜色不由 LHolo 维护。
- Minecraft level atlas 提供纹理。
- `BlockGraphics::getRenderLayer()` 取得实际渲染层。
- `VanillaBlockStateTransformUtils::transformBlock()` 取得旋转/镜像后的方块状态。

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

同种原版 `minecraft:*_sapling` 树苗之间只忽略 `age_bit` 生长阶段差异。Litematic 的 Java
`stage` 会映射为该状态，而玩家放置树苗时游戏会重置它；这不是可由玩家控制的朝向或放置错误。
树苗的其他状态、自定义命名空间方块及其他方块状态仍按下述规则严格比较。

每个结构非空气方块保存一个字节状态：

- `Unknown`：尚未扫描。
- `Missing`：世界方块为空气，蓝色提示。
- `Correct`：类型和完整方块状态相同，不显示投影或纠错。
- `WrongType`：类型名不同，红色提示。
- `WrongState`：类型相同但完整状态不同，黄色提示。

蓝图覆盖范围内的空气格不进入上述数组；真实世界主方块层存在非空气方块时，以局部坐标稀疏记录为 `Extra`（多余方块），使用品红色 `#FF4CE6`。计数覆盖完整结构，纠错网格只保存并绘制当前显示范围。真实世界只有液体层时默认忽略，与 Litematica 的默认语义一致。`.litematic` 保留各 Region 的覆盖盒，Region 之间的空隙不属于蓝图，禁止误报为多余方块。

判定顺序：空气 → 类型名 → 完整 `Block` 相等 → 状态错误。这个顺序不能交换，否则空气可能被算成普通类型错误，方向错误也可能被吞掉。

### 7.2 为什么错误位置不再绘制投影模型

世界已有正确或错误方块时，不再在同一位置绘制投影方块模型，只绘制纠错面与描边。这样避免两个有纹理表面共面产生 Z-fighting，也避免纠错颜色覆盖后看不清真实方块。

未放置方块仍显示原版投影模型，并叠加蓝色纠错提示。

### 7.3 纠错面

- 使用精确 1×1×1 单元外壳，不跟随栅栏、玻璃板等非完整碰撞模型。
- 相邻纠错单元通过优先级和邻居检查剔除内部共享面，避免同一平面绘制两次。
- 优先级：类型错误 > 状态错误 > 多余方块 > 未放置。
- RGB 使用固定 Litematica 风格色；alpha 由“纠错提示透明度”动态写入顶点色。
- 默认 alpha 15%，范围 0～100%。

### 7.4 描边

- 使用真正 `LineList` 的 12 条边，贴合 1×1×1 单元。
- 默认透明度 100%，范围 0～100%。
- 使用原版 outline selection material，保证普通和灵动视效路径可见。
- 整体结构边框是独立网格，不受纠错描边透明度控制。

### 7.5 准心选中闪烁修复

Minecraft 会对准心选中的真实方块额外绘制 hit-select overlay。若该位置同时为红/黄/品红纠错，第二个共面 overlay 会只在选中时出现并闪烁。

`LevelRendererPlayer::renderHitSelect` Hook 在目标坐标对应 `WrongType`、`WrongState` 或稀疏 `Extra` 时跳过原版 overlay。其他方块和未放置位置继续调用原函数。新版本适配必须验证该 Hook 签名和坐标语义。

---

## 8. 高性能设计

### 8.1 16³ 分区

结构按局部 `(x/16, y/16, z/16)` 分区。每个分区保存：

- 方块索引列表。
- 稀疏多余方块坐标。
- 四种投影渲染桶网格。
- 纠错面网格。
- 纠错描边网格。
- 分区中心，用于透明排序。
- `requestedRevision` / `uploadedRevision`、dirty、增量优先级和 in-flight 标记。

稳定帧不重新 Tessellate 方块，只提交已有 GPU 网格。

### 8.2 Worker 构建与主线程上传

dirty 分区的 `BlockTessellator` 和全部 CPU 几何生成不在 `$renderBlockEntities` 渲染线程执行。`ProjectionMeshWorker` 固定为单 Worker，并遵守：

- 一个分区最多一个 in-flight Task；连续变化只递增 revision，旧结果不会覆盖新状态。
- 增量方块变化优先于初次加载，二者内部都按分区中心到相机距离由近到远选择。
- 主线程用 `ChunkViewSource::move(..., DontGenerateOnlyGet, ...)` 固定分区加两格 halo 的局部视图。投影虚拟方块/方块实体/世界坐标索引表按 placement generation 发布为共享不可变版本：移动、旋转、镜像或切层时新建一组 Map，in-flight Task 通过 `shared_ptr` 保活旧版本，禁止原地清空或修改已发布版本。Task 只复制会增量变化的纠错字节、方块实体渲染可用性、本 section 索引，以及当前/六邻居 section 的稀疏多余方块坐标；不复制完整多余方块集合，不再扫描/拷贝 halo Map，也不再为每个 Task 构造紧凑 `LoadedStructure`。
- Worker 独占 `BlockSource`、`BlockTessellator` 和 `Tessellator`，不读取 `gState`、`renderContext` 或渲染线程的活动 Tessellator。
- Worker 必须用 `Tessellator::end(UploadMode::Never, ...)` 生成 CPU `mce::MeshData`；禁止在 Worker 使用 `Buffered` 或触碰 GPU。
- `UploadMode::Never` 返回的 CPU-only `mce::Mesh` 尚未设置上传态 vertex count，因此 Worker 不能用 `Mesh::getMeshVertexCount()` 校验结果（该值在 1.26.20.04 实测为 0）。CPU 阶段以 `MeshData::mPositions.size()` 为权威顶点数，并检查所有非空顶点属性数组与它一致。
- opaque pass 每帧最多接收两个完成结果，并以 1 ms 为提交预算。提交前同时检查 Worker generation、结构 generation 和 section revision，再用当前 `BufferResourceService` 的官方 `mce::Mesh(service, MeshData&&, false, name)` 构造 GPU Mesh。
- transparent pass 只提交已完成 Mesh，不调度任务、不消费完成队列、不上传资源。
- 普通 dirty 更新保留旧 Mesh 直到替换完成；旋转/镜像会立即清除旧方向的几何。
- 每成功上传 64 个 section，以 DEBUG 日志聚合输出主线程快照准备（进一步拆分为动态字节/索引复制和 `ChunkViewSource`）、Worker 构建和主线程上传的平均/峰值微秒数；不得为性能统计逐方块写日志。

Worker 初始化失败，或构建/上传连续失败三次后，本次游戏会话禁用异步路径，恢复每帧同步重建一个 dirty 分区。该回退继续复用同一份几何生成函数，避免两条路径产生视觉差异。

关闭投影、换世界、切维度和模组卸载时，必须先停止接收任务、提升 generation 并 join Worker，之后才能释放 `Level`、`Dimension`、`ChunkViewSource`、Block/BlockActor 和 Mesh。Worker 不得获取 `gStateMutex`；完成队列使用独立 mutex，避免清理时 join 死锁。

### 8.3 有界纠错扫描

`kCorrectionChecksPerFrame = 4096`。非空气方块纠错和蓝图空气格的多余方块发现共享同一预算；后者只扫描源格式实际覆盖的 Region。无论结构多大，每个渲染帧处理上限固定，避免全结构每帧扫描。

注意：该常量控制“纠错响应速度 vs 单帧 CPU 成本”。适配新版本时必须实测后调整，不要根据理论 FPS 盲目增大。建议记录：

- 1 万、10 万、50 万方块结构的帧时间。
- 放置/拆除方块后提示更新延迟。
- 普通渲染和灵动视效的 CPU/GPU 占用。

初次扫描完成后，稳定世界依靠 `BlockSourceListener` 的方块变化通知更新相关坐标；新加载 subchunk 刷新其中的投影方块和蓝图覆盖空气格。稳定帧不循环扫描完整结构。

### 8.4 增量失效

- 方块状态实际变化：只标记所属分区和六个邻居所在分区 dirty。
- 异步路径每次只构建一个 Task；同步回退每帧最多重建一个 dirty 分区。
- 旋转/镜像：局部模型改变，全部分区失效。
- 投影透明度或纠错样式改变：顶点 alpha 改变，全部分区一次性失效，之后继续缓存。
- X/Y/Z 整体移动：GPU 局部网格不重建，只更新 world lookup、纠错扫描位置和矩阵平移。
- 显示层变化：只失效可见性跨越边界的分区。

### 8.5 HUD 零额外扫描

纠错扫描同时维护：

- `progressCorrect` 与正确计数。
- `progressErrorKind`：每个结构坐标使用一个字节记录无错误、类型错误或状态/朝向错误。
- `progressWrongTypeCount` 与 `progressWrongStateCount` 两个独立计数。
- 稀疏 `progressExtraCount`，不改变建造进度分母，也不进入材料需求。
- 原子发布的 `placed/total/wrongType/wrongState/extra`。

HUD 每帧只读取原子计数，不查询世界、不遍历结构。

材料显示 HUD 也不单独查询世界：`ProjectionState::progressCorrect` 每次变化时递增
`progressRevision`，`MaterialTracker` 只在结构、纠错版本或显示层设置变化后复制一次纠错字节快照。
版本查询和快照复制必须通过 `Projection.h` 的 `getMaterialProgressKey()` /
`captureMaterialProgress()` 门面完成；`structure` 模块不得包含 `ProjectionSession.h`、获取投影锁或读取
`ProjectionState` 内部字段。
后台任务只用不透明的 `Block const*` 作为键，统计当前显示范围内未正确放置单元；物品注册表、
本地化和背包读取仍在游戏 tick 线程执行，且每个唯一方块状态只解析一次。同时最多一个统计任务，活跃建造时最多
每 400 ms 重算一次；结果发布前再校验全部版本键，过期结果直接丢弃。卸载模组时由 `AppKernel` 先回收该任务，
再释放投影和结构。材料清单与材料 HUD 使用独立 UI 快照：前者只在用户打开清单时统计整张蓝图，
后者为当前显示范围，开启 HUD 不得隐式触发整图清单计算，两者也不得相互覆盖。
异步重算期间继续显示上一份完整 HUD 快照；新结果完成后，在 tick 线程立即计算对应背包数量，
并在同一个 UI 锁内一次性发布材料和库存向量。禁止在提交任务时清空旧快照，也禁止分两次发布两个向量，否则 HUD 会闪烁或短暂显示错误缺口。

### 8.6 透明排序

透明网格按分区中心到相机距离从远到近排序。整体移动只改变矩阵/世界中心，不需要重建网格。若新版本出现特定角度黑块或闪烁，检查：

1. 是否错误地让 opaque 网格走了 blend pass。
2. 分区中心是否包含当前 anchor + offset。
3. 同一表面是否由投影模型、纠错面和 hit-select 重复绘制。
4. 材质是否写深度、混合状态是否被其他 Hook 污染。

### 8.7 菜单输入保护开销

`input/MenuInputGuard.cpp` 只 Hook 鼠标输入源和键盘按下/释放三个入口。菜单关闭时读取 `StructureLoader::isMenuInputCaptured()` 后立即进入原函数；不扫描方块、不分配内存、不加锁、不查询玩家、不写逐次日志。

---

## 9. GUI、快捷键与输入交接

### 9.1 ImGui 页面

GUI 覆盖当前 `ImGuiIO::DisplaySize`，顶部导航包含：投影、结构变换、渲染设置、快捷键、HUD。界面缩放范围 1～5。

GUI 是全屏 ImGui 窗口，不是切换 Minecraft 窗口模式。

### 9.2 输入所有权

菜单打开时，WndProc 将鼠标、键盘、字符和 Raw Input 交给 ImGui；`MenuInputGuard` 同时在 `MouseDevice::feed` 和 HID 键盘入口阻止输入进入 Minecraft 原生页面。两层都读取 `StructureLoader::isMenuInputCaptured()`，避免窗口消息已被界面消费、原生页面仍收到内部输入。

打开菜单前记录游戏当前按键/鼠标按下状态，并向游戏补发必要的 key-up/button-up，避免“按住移动键打开菜单，关闭后角色自动走”。

关闭菜单时：

- 将鼠标恢复到游戏客户区中心。
- 短暂限制鼠标到客户区并设置输入阻断窗口。
- 消费菜单关闭动作对应的释放消息。
- 防止关闭后的首次右键变成 ESC/暂停效果或鼠标落到客户区外。

不要依赖“消息积压”解释输入 bug；应检查鼠标坐标、Capture/ClipCursor、按键状态和 Raw Input 所有权。

### 9.3 Bedrock 输入源保护

WndProc/Raw Input 只覆盖 Windows 消息边界，不能作为阻止 Minecraft 原生 UI 和游戏动作的唯一保证。当前在 Bedrock 输入源增加第二道边界：

- `MouseDevice::feed` 与 `HIDControllerGameCoreDesktop::$onKeyDown/$onKeyUp` 是主路径；菜单可见或关闭过渡期间直接停止向原生 UI 分发，F11 例外并继续交给 Minecraft 的全屏切换生命周期。
- 打开菜单前向 Minecraft 补发的释放消息由 `MenuInputHandoffScope` 临时放行；禁止把所有 key-up/button-up 长期放行，否则原生按钮通常会在释放边沿触发，重新产生穿透。
- 三个 Hook 的安装状态由 `MenuInputGuardStatus` 分别返回；单个 Hook 冲突不得伪装成整体成功，也不得导致菜单模块无法启用。
- PreLoader/LeviLamina Hook 返回值是 0 成功、非 0 失败，禁止用 `< 0` 判断安装结果。
- 输入源 Hook 只在菜单持有输入期间暂停本机的移动、放置、使用、攻击和原生 UI 操作；关闭菜单后立即恢复，不修改其他玩家或服务端状态。
- 鼠标按下状态在打开菜单前通过合成释放消息归还给游戏，因此无需再维护 `GameMode::$startDestroyBlock/$continueDestroyBlock` 专用保护。

### 9.4 快捷键配置

修饰键位图：Ctrl=1、Alt=2、Shift=4。捕获快捷键时忽略单独修饰键，F11 不允许成为模组快捷键。

恢复默认快捷键：

- 菜单：Alt+M。
- X/Z：Ctrl+方向键。
- Y：Shift+上/下。
- 显示层：Alt+上/下。

---

## 10. ImGui 图形链与交换链生命周期

### 10.1 Hook 列表

`ImGuiOverlay.cpp` 使用 MinHook 连接：

- `IDXGISwapChain::Present`
- `IDXGISwapChain1::Present1`
- `IDXGISwapChain::ResizeBuffers`
- `IDXGISwapChain3::ResizeBuffers1`
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

### 10.3 历史崩溃根因

历史崩溃条件：只要初始化过 ImGui，之后 F11 切换全屏就以 `0xC0000409`/`std::terminate` 结束。IDA 与日志表明进入 Minecraft 的 device-lost 处理链，而不是普通输入异常。

根因是 Minecraft 开始全屏/交换链转换时，外部 D3D11On12 后端仍持有旧图形状态。仅在 `ResizeBuffers` 释放 RTV 已经太晚。

窗口大小调整还有一条相同性质的历史路径：菜单绘制过一次后，ImGui 已创建 DX11 device objects；旧实现只 Hook 了 `IDXGISwapChain::ResizeBuffers`，但 Minecraft 的 D3D12 交换链使用 `IDXGISwapChain3::ResizeBuffers1` 调整窗口大小，导致 LHolo 完全错过实际缩放入口，D3D11On12 device/context 跨越旧、新 back buffer 生命周期，Minecraft 会以 `0xC0000409` 结束进程。

### 10.4 正确修复

WndProc 收到首次 `WM_KEYDOWN + VK_F11`，在消息交回 Minecraft 前：

1. `ImGui_ImplDX11_Shutdown()`。
2. 解除 RTV。
3. `ClearState()`、`Flush()`。
4. 释放 D3D11On12 device/context/device。
5. 保留 ImGui Context、Win32 后端、WndProc 和业务状态。
6. 延迟约 750 ms，允许 Minecraft 完成交换链切换。
7. 后续有效 Present 使用新 swap chain/queue 重建图形后端。

普通窗口缩放必须同时 Hook `ResizeBuffers` 和 `ResizeBuffers1`，两者都进入同一套 `prepareForSwapChainResizeLocked()` / `deferGraphicsResumeAfterSwapChainResizeLocked()` 生命周期：完整释放 DX11/D3D11On12 图形后端，保留 ImGui Context、Win32 后端和菜单状态；缩放成功或失败后都不在 detour 内同步重建，而是短暂防抖，并由稳定的后续 Present 懒重建。连续 Resize 必须续期普通缩放的恢复时间，但不得缩短 F11 已安排的更长暂停。所有 DXGI vtable 索引集中定义在 `ImGuiOverlay.cpp` 顶部，不得在安装代码中散落数字常量。`gResourceMutex` 必须覆盖释放、原始缩放调用和恢复时刻更新，避免 Present 与交换链缩放并发访问图形状态。

LHolo 使用受 `gResourceMutex` 保护的弱 `gActiveSwapChain` 身份，只处理当前游戏交换链的 Present 和 Resize；不得对该指针 `AddRef`，以免延长 Minecraft 交换链生命周期。全屏或设备转换允许图形后端已释放时，由输出到同一游戏窗口的新交换链接管；无窗口的 composition/off-screen 交换链不得接管已绑定的 overlay。`initializeImGui()` 的所有失败出口必须回滚本次创建的 DX11/D3D11On12 对象，首次 Win32 后端初始化失败还必须销毁刚创建的 ImGui Context。

不要回退为只清 RTV、只 invalidate ImGui device objects、在 `ResizeBuffers` 内同步 CreateDeviceObjects，或只依赖窗口消息猜测普通缩放。

### 10.5 图形故障日志

以下失败必须记录 HRESULT 与 `GetDeviceRemovedReason`：

- D3D12/D3D11 `GetBuffer`
- `CreateWrappedResource`
- `CreateRenderTargetView`
- `ResizeBuffers`
- `ResizeBuffers1`

错误日志是正式版维护能力，不应作为“调试死代码”删除。

---

## 11. 网络命令与 Hook

菜单命令在 `LoopbackPacketSender::$sendToServer` 和 `$send` 两个路径拦截 Text Chat packet。只匹配长度恰好为 5 的 ASCII `lholo`，逐字符转小写比较。

匹配时：

- 确保 overlay 已安装。
- 请求打开菜单。
- 返回，不调用原发送函数。

不匹配时原样发送。不要使用前缀匹配，否则普通聊天可能被误吞。

LeviLamina Hook：

- 两个 LoopbackPacketSender 发送路径：本地菜单命令。
- `LevelRendererPlayer::renderHitSelect`：避免红/黄纠错与原版选中覆盖共面闪烁。
- `LevelRendererPlayer::$renderBlockEntities`：在原版调用后更新/提交投影，并提交独立的创建结构选区线框；没有新增渲染 Hook。
- `BlockSource::$getBlock` 两个重载和 `$getBlockEntity`（`projection` 模块）：仅在 LHolo Tessellation 的线程局部作用域内提供虚拟邻居和方块实体，作用域外立即调用原函数。
- `LocalPlayer::$tickWorld`（`place` 模块）：轻松、手动和范围放置的每 tick 驱动。
- `GameMode::$startBuildBlock` / `$buildBlock` / `$stopBuildBlock`（`place` 模块）：手动模式命中真实方块时的右键按下、持续、释放状态及原版重复放置抑制。
- `GameMode::$useItem`（`place` 模块）：手动模式指向空气时创建单次放置请求，使浮空投影方块也能进入放置链路。
- `MouseDevice::feed` 与 `HIDControllerGameCoreDesktop::$onKeyDown/$onKeyUp`（`input` 模块）：菜单期间在 Bedrock 输入源阻止游戏和原生 UI 接收输入。

新版本最容易变化的是成员函数符号、签名、调用层次和 render pass 时序，必须逐一验证，不能只以“Hook 安装成功”判断适配完成。

---

## 12. 轻松、手动与范围放置

### 12.1 行为

菜单“投影”页提供三种互斥模式：

- 轻松放置：准心指向投影中的蓝色缺块位置（`correctionStates == Missing`）时自动放置。
- 手动放置：准心定位规则相同，但只有按下/按住右键时才放置；命中真实方块时，首次按下立即尝试，持续按住经过 150 ms 初始延迟后每 120 ms 重复；指向空气中的浮空投影时，空气右键入口创建单次请求，避免缺少释放回调而遗留长按状态。
- 范围放置：每 tick 查询玩家周围配置半径（1～4）内的缺块，按距离从近到远选择一个满足触及距离、物品和原版放置预测的候选。

三种模式都会在完整 36 格玩家背包中寻找对应物品；背包栏命中时先通过服务端同步的普通背包事务与当前快捷栏槽位交换，下一 tick 再放置。液体单元与隐藏层不参与放置。

### 12.2 实现要点

- 驱动：直接 Hook `LocalPlayer::$tickWorld`，模拟线程每 tick 一次，不使用 LL 事件系统（与全项目 Hook 风格一致）。手动模式另外 Hook `GameMode::$startBuildBlock`、`$buildBlock` 和 `$stopBuildBlock` 获取命中真实方块时的右键按下、持续与释放状态，并阻止同一次操作被原版重复放置；指向空气时 Bedrock 不进入 build 链路，因此通过官方 `GameMode::$useItem(ItemStack&)` 入口创建单次请求。
- 定位：不能使用 `Level::getHitResult()`——那是原版射线，只命中真实世界方块，永远看不到 LHolo 自绘的投影幽灵。改为自身体素 DDA（Amanatides & Woo）射线：原点 `Actor::getEyePos()`、方向 `Actor::getViewVector(1.0f)`、上限 `LocalPlayer::getPickRange()`。逐格判定：真实方块挡住射线（此时检查其相机侧邻居是否为待放幽灵），投影 `Missing` 幽灵格直接作为放置目标；支持面用 `BlockPos::neighbor` + `Facing::getOpposite` 选取朝向相机、且为真实方块的邻居。
- 投影查表：`Projection::queryProjection()`——一次锁 `gStateMutex` 内同时查 `expectedWorldBlocks`（期望块，液体/隐藏层返回 null）与 `expectedWorldBlockIndices`/`correctionStates`（是否 Missing）。DDA 每格只调一次，避免两次独立加锁；命中结果（含期望块指针）随 `ProjectionTarget` 一并返回，`tickEasyPlace` 不再二次查询。
- 取物：遍历完整背包（36 格）用 `sameItemAndAux` 匹配。快捷栏命中直接 `Player::setSelectedSlot`；背包命中用 legacy `NormalTransaction`（`ComplexInventoryTransaction::fromType` + 两个 `InventoryAction`）把物品与当前选中格**交换**（服务器同步，不假设目标格为空，避免被 net 管理器回滚）。交换后本 tick 不放置，下一 tick 物品已在选中格、走单包快速路径——同 tick 立即放置会被服务器 net 记账滞后拒绝，再触发格锁反而更慢。服务器只接受选中快捷栏槽位的放置事务。
- 放置：直接构造 `ItemUseInventoryTransaction`(Place) 经 `IClientInstance::getPacketSender().sendToServer()` 发送（单机走 LoopbackPacketSender，联机走网络发送器），服务器权威落块并保存。**关键细节**（踩坑记录）：
  - `mPos` 是“被点击的方块”，服务器在 `mPos.neighbor(mFace)` 落块——必须填支撑块 `at`，填目标格会导致偏一格。
  - `setIncludeNetIds(true)`：服务器栈 net-id 系统按“含 net id”读包，缺此标志流错位、包被静默丢弃（`onTransactionError` 都不会触发）。
  - `setTargetBlock` / `setSelectedItem` 用导出 setter 填 `mTargetBlockId` / `mItem`，避免未导出的赋值运算符。
  - 点击点取支撑面中心：`(cell + at)` 的中点。
  - `GameMode::useItemOn`（客户端）只做本地预测不持久；`Player::sendNetworkPacket` 在单机不送达集成服务器，两条路都不可用。
- 节流：只有 `Missing` 格可进入规划，发送前再次读取真实世界；目标格只要已存在任何方块（包括错误类型或错误朝向/状态）就停止，不允许再次尝试放置。实际发包后才写入逐格锁，在 `kCellLockMs`（500 ms）内不重复发包，等待服务器应用和纠错扫描更新；新格可立即放置（受 tick 20 Hz 上限约束）。发送地板间隔 `kMinSendIntervalMs`（40 ms）防异常 tick 率双发。背包交换未生效时由 `kSwapRetryMs`（200 ms）限制重发；成功交换仍在下一游戏 tick 尝试放置。范围模式每 tick 最多执行 16 次昂贵的放置规划，失败规划缓存 250 ms。
- 破坏抑制：复用投影的 `BlockSourceListener::onBlockChanged`，以主方块层 `oldBlock` 非空气且 `block` 为空气识别破坏；纠错层只转发属于当前投影的坐标和原始发生时间。放置层用压缩坐标稀疏保存截止时间，时长为持久化的 0～60 秒用户偏好（默认 10 秒，0 表示不登记）；轻松/范围放置在库存搜索和方向规划前跳过，手动放置绕过。空表通过最早到期时间原子值直接返回，不扫描投影、不轮询世界、也不启动计时线程。修改时长只影响后续破坏，已经登记的位置沿用破坏时的截止时间。
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

当前配置版本：`11`。

正式持久化字段：

- `version`
- `lastStructurePath`
- `uiScale`
- `opacity`
- `correctionFillOpacity`
- `correctionOutlineOpacity`
- `structureBoundsEnabled`
- `placementRadius`
- `autoPlacementBreakCooldownSeconds`（0～60 秒，默认 10；只控制后续破坏产生的自动放置冷却）
- `correctionSeeThrough`、`missingSeeThrough`（错误与未放置标记的穿透显示开关，默认关闭）
- `experimentalConsent`（辅助放置风险提示是否已确认）
- `materialHudEnabled`、`materialHudPosition`（材料 HUD 开关与四角位置，新配置默认右下角）
- `loadProjectionHotkey`、`closeProjectionHotkey` 及其修饰键
- HUD 开关、各项显示开关（含 `hudShowProjectedBlockName` 投影方块名称）、位置；读取时兼容旧键
  `hudShowBlockEntity`，保存时只写新键
- GUI、移动、显示层快捷键与修饰键
- 上次投影是否存在、文件路径、绝对锚点
- 上次投影旋转、镜像、偏移、显示模式、显示层和分层轴

普通结构变换和显示层属于当前会话；只有“恢复上次投影”记录显式跨会话保存。手动放置、轻松放置和范围放置为安全敏感的临时功能，只能从实验性功能页面启用，不提供全局快捷键，不读取、不写入配置，每次启动均默认关闭；放置半径和投影方块破坏后的自动放置冷却时长仍持久化。纠错样式、投影透明度、GUI/HUD 和其他快捷键属于用户偏好，始终持久化。

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
xmake repo -u
xmake clean
xmake f -a x64 -m release -p windows --target_type=client -y
xmake -r
```

唯一发布目录：

```text
bin/LHolo/
├─ LHolo.dll
├─ manifest.json
└─ LICENSE
```

`THIRD_PARTY_NOTICES.md` 只保留在源码仓库，不复制到 `bin/LHolo`。不要发布 `build/`、`.exp`、`.lib`、日志、崩溃转储、配置、开发工具或测试结构文件。

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
5. 检查 Present/Present1/ResizeBuffers/ResizeBuffers1/ExecuteCommandLists 是否仍使用顶部集中定义的预期 vtable 索引和接口。

### 阶段 C：验证结构解析

准备固定回归样本：

- 小型 `.mcstructure`：草方块、石头、玻璃板、栅栏、楼梯、门、活塞、观察者、上下半砖、普通水、岩浆、不同液位、至少一个含水方块，以及带 NBT 的双箱/告示牌。放置单层半砖时，即使相邻支撑是同材质单层半砖，也不得把支撑误合并为双层；双箱必须显示为一个原版大箱子；水/岩浆样本同时验证贴图 proxy（顶层固定 8/9 高、同液体覆盖时满格、相邻同液体共享面剔除）与液位状态纠错；当前 proxy 不按流动深度改变视觉高度。
- 多区域 `.litematic`：正/负 Size、区域重叠、不同 palette 位宽；负 Size 样本必须包含楼梯、门、活塞、观察者等方向明显的方块。
- `主播公寓.litematic`：固定验证 X/Z 同时为负的区域不会被旋转 180°，建筑布局和楼梯朝向均与 Java 源文件一致。
- `borgital-strike-cube-by-baonam7910.litematic`：固定验证 12 个橡木墙上告示牌的正面文字与发光状态，包含单行和四行文本。
- 大型结构：至少 10 万方块。
- 损坏/截断/超大文件。

确认尺寸、方块数、朝向、负区域归一化和错误提示。实机视觉结果只能由用户手动确认，静态解析和构建成功不能替代游戏内验证。

### 阶段 D：验证原版渲染 API

逐项检查：

- `LevelRenderer::mAtlasTexture` 是否仍为方块 atlas。
- `BlockTessellator` 构造与 `mCachedGetBlock` 行为。
- `BlockSource::$getBlock` 两个重载的 Hook 签名和线程局部虚拟实体方块作用域；门的上下半块与邻居模型是固定回归项。
- `BlockSource::getLiquidBlock()` 的读取语义；它仅用于纠错查询，当前没有虚拟液体 Hook。
- `tessellateInWorld()` 参数和顶点数据布局。
- `BlockGraphics::getRenderLayer()`。
- `VanillaBlockStateTransformUtils::transformBlock()` 与 `LegacyStructureSettings`。
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
3. 维度切换：旧 `DimensionBlockSource` 不被继续使用；进入其他维度时投影与 HUD 隐藏，返回原维度后按原锚点自动恢复，结构和辅助放置模式不关闭。
4. 世界加载未完成时打开菜单/恢复投影不崩溃，资源就绪后正常启用。

### 阶段 G：发布

1. 完成代码审计：无旧命令、旧项目名、测试单方块路线和一次性诊断统计。
2. 保留必要错误日志和图形故障诊断。
3. 更新本文档的版本基线、API 变化和已知限制。
4. 执行干净 Release 构建。
5. 检查 `bin/LHolo` 仅有 DLL、manifest 和 LICENSE，不包含 `THIRD_PARTY_NOTICES.md`、日志或开发工具。
6. 部署并核对 SHA256。
7. 完整执行下一章回归矩阵。

---

## 16. 发布前回归矩阵

### GUI/输入

- [ ] 首次进游戏无需先打开其他界面，Alt+M 可打开菜单。
- [ ] `LHolo`、`lholo`、混合大小写均打开菜单且不发送聊天。
- [ ] 菜单打开时鼠标不转视角、按键不移动玩家。
- [ ] 菜单打开时，在本地存档与远程服务器分别短按/长按左键，方块均无裂纹进度且不会被破坏；其他玩家不受影响。
- [ ] 菜单关闭后立即恢复正常破坏；关闭菜单的同一次鼠标操作不破坏方块。
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
- [ ] X/Z 镜像正确。
- [ ] X/Y/Z 偏移快捷键和输入一致。
- [ ] 远坐标（至少 ±20000）稳定。

### 创建结构

- [ ] 未进入世界时创建操作禁用；只设置一个点时不显示线框。
- [ ] 正序、反序和手动修改两点时，红色线框包围包含两个端点的完整选区。
- [ ] 关闭菜单及导出后选区线框继续显示；“清除选区”使其立即消失。
- [ ] 单独确认原有“显示整体结构边框”开关和显示未受影响，不要求与红色选区线框同时开启。
- [ ] 导出的 `.mcstructure` 可由 LHolo 重新加载，尺寸、方向、含水层和方块实体正确。
- [ ] “包含实体”开关产生预期差异；多人游戏中的客户端实体与容器数据限制按页面说明处理。
- [ ] 选区包含未加载区块时拒绝导出。
- [ ] 切换维度或世界后，选区、实体选项和线框资源清理。
- [ ] 中文保存路径、默认扩展名和覆盖确认正常。

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
- [ ] 冷却时长设为 10 秒时，破坏投影位置上已放置的实体方块后，轻松放置和范围放置在 10 秒内不自动补回，到期后可再次自动放置；手动放置不受限制。
- [ ] 冷却时长的 0 秒和 60 秒边界正确，修改后重启仍保留；0 秒不登记新的破坏冷却。
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
- [ ] 切换维度后旧维度资源与坐标缓存清理，其他维度不显示投影/HUD，返回原维度后恢复且稳定帧没有额外扫描。

---

## 17. 已淘汰方案与禁止回归项

以下路线已验证失败或已被正式架构替代，不应重新引入：

- `sp`/`sp <block>` 头顶单方块测试命令及其延迟切换状态。
- `spgui` 旧菜单命令。
- 单方块即时 Tessellator、首次黑块预热、单方块射线选中日志。
- 手写草方块 UV、手动替换材质或只修改顶点 alpha 的早期实验链。
- 早期纯色液体单元壳：当前正式方案是使用 terrain atlas 水/岩浆贴图和水体 tint 的 `liquidProxySectionMeshes`，不得退回无贴图纯色版本。
- `tessellateLiquidInWorld()` 几何自行提交：盲提交黑块/过曝，受控提交（顶点色覆写 + Blend 桶）未知方块纹理，顶点格式/UV 语义与普通方块材质根本不兼容。
- 用 `BlockSource::$fireBlockChanged` 或 `RenderChunkCoordinator::$onAreaChanged` 为投影液体触发区块重建：当前液体完全由 LHolo 自绘网格管理，只失效自己的 16³ 分区，不进入原版区块重建链。
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

---

## 19. 自动化测试

纯逻辑单元使用独立的控制台测试目标，不加载游戏运行时：

```bash
xmake -b LHoloLogicTests
xmake r LHoloLogicTests
```

当前覆盖：

- `core/ProjectionLayoutRules`：镜像/旋转枚举映射、结构坐标变换、分层可见性、渲染桶分类。
- `runtime/ProjectionProgress`：进度快照初始化、发布、越界钳制与重置语义。
- `settings/SettingsStore`：配置文件缺失检测与保存/读取往返一致性。
- `structure/StructureSession`：空会话、结构替换、变换、层数、恢复快照、关闭前冻结及分层恢复语义。
- `place/PlacementState`：模式开关、手动放置时间、节流时间、自动放置抑制和缓存到期边界与准心名称读写。
- `structure/StructureUiState`：HUD 快照、快捷键绑定/去重、按键释放抑制、待处理动作与材料快照。
- `ui/HotkeyFormat`：修饰键判定、未设置与 Ctrl/Alt/Shift 和弦字符串。

新增可独立链接的纯逻辑时必须同步补充对应断言；涉及 Minecraft 运行时对象（Block、BlockSource、Tessellator）的代码不进入该测试目标。
