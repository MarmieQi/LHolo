# LHolo 开发与版本适配手册

本文档描述 LHolo Windows 客户端模组的正式版架构、关键实现、性能约束、故障历史和新 Minecraft/LeviLamina 版本适配流程。维护者在修改渲染、输入、结构解析或配置前，应先阅读对应章节，并在发布前执行完整回归矩阵。

当前基线：

- Minecraft Bedrock Windows：`1.26.51.01`
- LeviLamina：`26.51.0`，目标类型 `client`
- 架构：Windows x64
- 图形接口：Minecraft D3D12 + LHolo D3D11On12 + Dear ImGui DX11 后端
- 模组名称、DLL、目录和内部命名空间：`LHolo` / `LHolo.dll` / `mods/LHolo` / `lholo`
- 支持格式：Bedrock `.mcstructure`、Java Litematica `.litematic`

文件结构预览通过独立原生 UIScene 展示原文件，不发布活动投影或修改任务选择。只读解析使用独立 StructureReadQueue，与任务加载仅共享原生解析互斥和世界生命周期屏障；关闭立即撤销结果发布，迟到结果丢弃。当前行为、资源所有权和维护约束见 [文件结构预览](docs/STRUCTURE_PREVIEW.md)。

2026-10-28 任务异步加载提示修复：`StructureFileLoader::startTaskComposition` 接入现有 `StructureUiState` 加载提示 token，在解析与合成期间通过旧 ImGui 动作提示栏显示 `TasksLoading`（“加载中...”）。成功和失败均由 Worker 的作用域清理结束提示，启动失败同步清理；关闭投影、退出世界和停用沿用现有清理，旧 token 不影响后续加载。单文件加载保留原文案，提示随当前语言渲染，不新增 HUD 设置或前端状态。隔离回归与编译不替代游戏内验收。

2026-10-28 每文件维度和世界坐标：按用户要求不记录世界/服务器标识，`tasks.json` 升为 v2，每个文件保存 `dimension` 和 `position: {x,y,z}`。任务只组织文件，不再保存/缓存任务锚点；原生脚部方块坐标与维度在添加新文件时立即登记；添加时没有玩家坐标的文件在后续加载时补取。退出世界仍释放原生缓存与迟到加载，已绑定文件重进或换服务器继续使用同一维度和绝对坐标，不做主世界/下界 8:1 转换。活动任务随玩家维度切换重新筛选视图，其他维度的文件保留设置但不参与加载、AABB、渲染、纠错或放置，且不能用快捷键误编辑。现有非重叠单文件管线保持不变。

按用户要求只读取当前 v2 任务格式，不保留旧格式读取、相对坐标迁移或锚点校准逻辑。新文件在添加动作成功登记到目录时立即保存玩家的脚部方块坐标和维度；加载等待、失败或玩家移动不改写已登记锚点。`dimension: null` 表示添加时尚无玩家坐标的文件，之后加载时补取，成功后保存；该补取失败时仍保持未定位状态。TaskStoreWriter 继续异步原子保存，不引入服务器插件、世界 ID 或额外世界查询。回归覆盖跨退出/重启恢复、同任务主世界/下界/末地过滤、同 XYZ 不同维度互不重叠、首次放置失败、加载器繁忙期间跨维度重试及不支持的格式拒绝；实际服务器与跨维度体验由用户实机验收。

2026-10-28 非重叠文件移动路径简化：撤回造成移动等待的依赖组、双场景交接及轮转更新试验。`IndependentTaskFiles` 只比较变换后完整 AABB：所有可见文件互不重叠且不紧贴时保留原始文件数据，分别调用原来的单文件渲染、变换和纠错流程，不合并方块。选中文件的实时变换直接进入当前渲染帧，不等待任务异步发布；原始数据不变的其他文件复用原运行状态。边界紧贴或重叠时仍使用原 TaskComposer/合成管线，保留跨文件邻接模型与优先级。没有 16 格扩张、依赖图或等待空气扫描完成后的显示交接。

原始文件缓存、单 Worker 和世界监听继续共享；`IndependentFileProjection` 只管理独立文件实例和公共队列分发，事件先按文件范围过滤，结果按实例 generation 路由（同路径的不同任务文件也有独立身份）。每个文件每帧继续执行原单文件流程及其原预算；首次同时加载多文件的纠错总预算会随可见文件数增加，跨独立/合成模式仍按旧规则重新激活。HUD、材料、辅助放置和每文件边框跟随对应文件。AABB 与真实任务控制帧隔离回归通过，用户于 2026-10-28 反馈本版实机测试通过；后续回归继续覆盖移动、红石粉、纠错、跨模式切换与多文件加载性能。

2026-10-28 任务关闭重载位置修复（旧方案，已由上述每文件维度/坐标方案替代）：原实现关闭时丢弃任务锚点，重新激活改用玩家当前位置，导致文件相对位移不变但世界位置整体偏移。TaskActions 现在按任务 ID/维度保存首次成功激活的原始锚点，关闭和切换任务可复用；StructureFileLoader 在世界销毁/重启时推进 revision，经 TaskContext 使关闭状态下的锚点也失效。缓存仅属当前世界会话，不修改任务/配置格式，不跨重启保存。回归覆盖实际世界坐标、归一化变化、任务/维度隔离、换世界和迟到结果；渲染、网格复用及移动重建策略保持远程 main 的实现。实机由用户验证。

2026-10-28 整体边框改为每个可见文件各一个完整边框。TaskComposer 在切片和重叠裁剪前记录旋转后的完整文件范围，再与合成结果统一归一化；范围仅用于显示，不加入纠错空气覆盖。原生绘制将所有独立线框批量缓存到一个 LineList 网格，每文件 24 顶点、整体一次边框绘制；关闭边框时不新建网格，重建/旋转继续沿用现有失效和释放规则。空文件、完全被覆盖及完全被切片过滤的可见文件仍保留边框，隐藏文件不显示。未改方块模型、移动重建和网格复用逻辑。隔离回归通过，实机由用户验证。

2026-10-26 普通 HUD 与材料显示 HUD 已接入原版 `ScreenView::render`，复用 `UIRenderContext` 和游戏字体；保留现有业务快照、显示开关和四角定位，采用旧 ImGui HUD 的宽松内边距、行间距和圆角比例，字体仍为原生游戏字体，背景 alpha 为用户选择的 0.6。普通/材料 HUD、原生物品图标、间距与多语言冒号留白已获用户实测通过确认。后续版本仍按回归清单验收。分层与接口依据见 [原生 HUD](docs/NATIVE_HUD_RENDERING.md)。

写入进度作为第三个独立原生 HUD，默认启用、顶部居中，复用 NativeHudPainter 和现有布局编辑器。HudPlacement 集中声明面板数量和默认外观；独立开关走 SettingsActions，配置增量保存于 hud.write，旧配置缺省兼容。任务身份及真实结束时间来自 StructureWriteActions，源适配和展示不依赖投影可见性；状态文本与 OreUI 共用映射，完成/取消提示 2.5 秒后隐藏，原版原子放置不伪造逐块进度。见 docs/NATIVE_HUD_RENDERING.md。

HUD 布局编辑使用独立原生 UIScene，入口仍在 OreUI HUD 设置页。editor/HudEditor 管理草稿和输入状态，HudEditorScene 在客户端更新中创建/弹出场景，HudEditorView 与正常 HUD 共用 NativeHudPainter。HUD、边框与按钮均原生绘制，鼠标由游戏场景管理；完成保存，取消/Esc 丢弃，待原生场景移除后重新打开 HUD 设置页。有结构时使用真实内容，没有结构时使用编辑专用本地化示例。编辑器支持独立调整位置及背景不透明度（0～100%、精度 1%，默认 60%），底部为“重置、透明度、缩放”，后两者复用向上展开的百分比滑条；缩放范围 50～200%，默认 100%，两个 HUD 分别保存；完成保存、取消丢弃，“重置”恢复选中 HUD 的位置、默认透明度 60% 和缩放 100%；全部重置恢复两个 HUD 的位置、默认不透明度与缩放。全部按钮背景统一默认 0.5，悬停 0.6，按下 0.7。用户已于 2026-10-26 确认原生编辑器可用。操作按钮移至顶部居中，共用 HUD 圆角背景，顶部三个按钮默认不透明度为 0.5，不额外显示面板名称，仅选中项显示红色边框。2026-10-27 用户已验证 `UIDefRepository::findDef` 提供的内存场景定义可用于原生编辑，不再依赖资源包中的 HUD JSON UI 注册；OreUI 菜单仍依赖资源包。

OreUI 渐进迁移状态（2026-10-25）：聊天命令和菜单快捷键均打开当前自有样式的 OreUI 主菜单，旧 ImGui 菜单保留代码但不开放入口。
主界面入口试验：菜单快捷键允许在 Idle + start_screen 打开同一页面；路由调度在主 ClientInstance::update 上执行，不依赖已有 OreUI View。进入/退出世界或加载状态变化关闭旧上下文页面。无世界时允许浏览文件和编辑偏好，禁用加载/恢复投影、选区/导出与辅助放置启用；前后端均校验 worldAvailable。主界面实机结果待验收。
默认入口为建造任务，旧单文件“投影”页面和专用浏览/加载/恢复桥接已移除；材料清单位于任务页。前端任务意图经 TaskBindings/TaskProtocol 进入 app/TaskActions，复用现有加载器、合成器和投影会话；MenuBindings 只提供材料与运行上下文快照。OreUI 不提供缩放控件，不写旧 ImGui 缩放。协议、取消、测试与部署要求见
[OreUI 迁移说明](docs/OREUI_MIGRATION.md)；前端入口见 [frontend/README.md](frontend/README.md)。
材料清单经 useMaterials → MaterialsBridge → MenuBindings 读取 StructureUiState 的版本化一致快照。协议 iconId 仅用于显示，保留完整命名空间；版本私有格式转换集中在 tools/oreui/runtime_adapter.js，不借图标显示改变材料背包计数或辅助放置规则。原生快照未变化时不复制材料数组或重建 JSON。
收尾审计、官方依据/私有适配边界与未闭合事项见 [OreUI 审计报告](docs/OREUI_AUDIT.md)。
前端可运行 `npm.cmd --prefix frontend run preview`，在 http://127.0.0.1:4173/ 实时预览，不启动游戏；preview/ 的渲染环境与模拟数据只用于开发，正式构建禁止引入。

---

渲染设置经 application/useRenderSettings → RenderSettingsBindings/Protocol → app/RenderSettingsActions 接入。AppKernel::processControlFrame 在 ImGui 初始化前编排结构动作与渲染设置，并复用 saveSettings。应用启用时向 Overlay 注册原子帧回调，Overlay 不依赖 AppKernel，关闭时移除回调。投影视觉参数仍由 projection 持有，分层和真实范围由 StructureSession / LayerSettings 持有；下层不得反向依赖 app 或 OreUI。请求只包含改动字段，控制帧校验原值、会话和分层轴，避免整页配置覆盖。

HUD/快捷键/语言/实验性功能由 app/SettingsActions 统一接收字段或具体命令，原生持有实际状态；前端共享 useSettings 的应答调度，各页 hook 只持有局部编辑状态。语言列表/快捷键标签与默认值不在正式前端复制；input/HotkeyCapture 共用原有输入规则。模式不持久化，知情确认和参数沿用原有保存；缩放协议不开放。详见迁移说明。

建造任务的数据规则和原子存储位于 `src/tasks/`，`app/TaskActions` 负责控制帧、运行态和发布，`structure/TaskComposer` 合成获胜方块并保留文件来源与真实覆盖区域。原始文件缓存复用 `StructureFileLoader` 的世界生命周期，现有投影管线消费单个不可变合成快照；选中文件决定变换、切片、HUD/材料和放置目标。任务保存在 `data/tasks.json`，与全局配置及旧单文件恢复记录分离。实现边界和验证清单见 [建造任务](docs/TASKS_DESIGN.md)；本次多文件运行仍需游戏内验收。

任务审计修正：`TaskActions` 保留成功/待合成两份运行定义，失败不阻断旧投影的文件编辑；`TaskContext` 隔离玩家锚点和数据路径，隔离测试可运行真实任务控制帧。`TaskStoreWriter` 使用单写入线程合并 300 ms 内连续编辑，停用时完成最终保存；任务级变换与切片不重复写全局配置。任务 Facet 的 JSON 信封大小与任务协议一致，前端超长输入有明确错误且不进入等待；任务名称 IME 候选确认不误提交。无调用的 `restoreSavedProjection()` 门面已删除，隐藏旧 ImGui 菜单仍按现有约束保留。

OreUI 可视控件集中在 frontend/src/components/LHoloControls.tsx，视觉与排版以当前旧 ImGui 的 FluentTheme.cpp / MenuPages.cpp 为准；原版仅提供交互焦点/激活原语，输入框显式绑定真实 DOM input 事件，不直接套用原版表单外观。

迁移必须逐项对照旧 UI 的按钮、选项和操作流程，不擅自增加应用/确认等步骤。创建结构页自动同步有效输入，连续编辑合并并防止旧应答覆盖新内容。浏览器与游戏菜单隐藏滚动条，但保留滚轮、触摸及焦点滚动；不以禁用滚动替代隐藏。

写入世界入口位于 OreUI“创建结构”下方。支持结构方块和指令两种模式，写入当前激活任务选中文件的完整原始结构，使用文件绝对坐标、旋转与镜像，不受显示切片影响；默认包含空气、液体和已有方块实体 NBT，不包含普通实体；页面提供不持久化的“跳过空气方块”（结构方块模式默认关闭，指令模式默认开启），开启后仅跳过空气覆盖，仍写完整结构。应用层固定操作数据，structure/write/local 在本地服务端 tick 调用原版模板放置，只接受已加载且高度有效的范围；指令模式位于 structure/write/command/，按玩家附近 3×3 区块流水线发送 setblock（每次客户端更新最多 128 条、1024 条在途上限，无额外时间间隔）并分别等待原生回执，只还原主层方块类型和状态，支持有界重试、暂停后继续和取消，提供默认关闭、不持久化的“跳过已放置的投影方块”，开启后仅跳过已加载且类型/状态匹配的位置，不还原方块实体 NBT 与独立含水副层。世界保存沿用原版流程，调用返回不等于已刷盘或逐块验证。用户已于 2026-10-31 确认当前版本测试通过；后续仍按回归范围验证，接口、分层与限制见 docs/OREUI_MIGRATION.md 的“写入世界”。

## 1. 产品行为与边界

LHolo 的投影、纠错、HUD 和菜单都只存在于客户端，不产生碰撞，也不会把虚拟方块或液体写入世界。玩家可以穿过投影，服务器无需安装配套插件。可选的轻松放置、手动放置和范围放置会以本地玩家身份发送正常的背包/放置事务；最终方块由服务端按玩家权限权威落地，因此这些放置功能会像玩家手动放置一样修改世界。

正式版功能：

- 从任意用户选择的路径加载 `.mcstructure` 或 `.litematic`，导入目录不写死。
- 以加载时玩家脚下的整数方块坐标作为投影锚点；恢复记录时使用保存的锚点。加载新文件是全新放置意图：重置旋转/镜像/偏移与分层（否则新结构会继承上一个结构被手动挪出的位置）；"恢复上次投影"不受影响，它显式应用保存的变换。
- 支持 X/Y/Z 结构偏移、0°/90°/180°/270°旋转、X 或 Z 镜像。
- 支持 Y 轴水平分层、X 轴纵向切片和按材料分层；材料分组按用量从多到少排序。
- 显示范围支持“完整结构”“单层”“当前层及以下”“当前层及以上”；分层轴为“按材料”时上述范围按材料分组划分。
- 投影方块不提供整体透明度设置，沿用原 100% 时的渲染行为；玻璃、液体等保留各自的透明渲染。
- 纠错状态：未放置为蓝色、方块类型错误为红色、方向或方块状态错误为黄色；蓝图空气位置存在实体方块时以品红色标记为多余方块，完全正确时隐藏。
- 纠错提示透明度默认 15%，描边透明度默认 100%；均为 0～100 整数输入、即时生效、持久化保存，可一键恢复默认值。
- 可选整体结构边框：每个可见投影文件各有一个完整边框；重叠文件分别保留，隐藏文件不显示。边框随各文件变换更新，不随切片或获胜方块范围缩小。
- 支持准心轻松放置、按住右键的手动放置，以及半径 1～4 的范围放置；三种模式在 GUI 中互斥。手动放置按住右键不放，可沿准心扫过的投影连续放置。
- `.mcstructure` 中带 NBT 的方块实体优先使用原版方块实体渲染器；没有可用渲染器或 Tessellation 结果的方块使用贴图占位外壳。
- HUD 可显示文件名、显示层、建造进度、放置错误数、朝向错误数、多余方块数、准心指向的投影方块名称和当前辅助放置模式（手动/轻松/范围）；支持四角定位和单项关闭，各类错误可分别配置。
- GUI 使用外部注入 Dear ImGui，不使用游戏表单。
- 界面语言由 `src/i18n/lang/*.json` 自动发现，可在“界面设置”页切换，默认简体中文；语言代码随配置文件持久化。
- 默认 `Alt + M` 打开 OreUI 主菜单；聊天栏输入 `LHolo`（ASCII 大小写不敏感）打开 OreUI 菜单，消息在客户端拦截，不发往服务器。
- LHolo 菜单打开及关闭过渡期间，客户端阻止本地控制玩家开始或继续破坏方块；本地存档和远程服务器均有效，服务器无需安装 LHolo。
- 默认结构移动：`Ctrl + 方向键` 按玩家朝向水平移动（左右垂直于朝向、前后沿朝向），`Shift + ↑/↓` 调整 Y。
- 默认显示层：`Alt + ↑/↓`；“完整结构”模式下按键无效。
- 默认结构偏移：按住 `Alt` 并滚动滚轮，结构沿视线方向整体移动；按住期间快捷栏锁定，滚轮不会切换手持物品，松开后恢复。触发键固定为 `Alt`，不是按下式热键，不出现在热键页，但可在热键页用“`Alt + 滚轮 调整结构位置`”开关整体关闭；关闭后不认领 `Alt`、不接管滚轮、也不锁定快捷栏。
- 任务目录持久化外部文件路径、选择、顺序、显隐、变换与切片；进入世界后显式加载任务。旧单文件恢复记录仅为内部兼容保留，现行菜单没有恢复入口。
- “创建结构”页支持用玩家脚下位置或手动 XYZ 设置包含端点的选区，以红色整体线框持续显示，并通过原版结构 API 导出 `.mcstructure`。目前只提供客户端模式，且只读取当前已加载范围；实体默认不包含。
- 退出世界、切换存档或失去有效客户端上下文时清理投影，禁止跨世界复用世界对象；完成清理后在底部提示投影已关闭。
- 仅切换维度时保留已加载结构、锚点、变换、HUD 配置和辅助放置模式。投影与原维度绑定：进入其他维度时释放旧维度运行资源、隐藏投影及 HUD，并在底部提示投影已暂停；返回原维度并成功按原锚点重建后提示投影已恢复，材料 HUD 随恢复后的投影重新统计。投影生命周期状态提示均显示 2.5 秒。

底部生命周期提示继续通过 ImGui 动作提示条显示。任务按维度重建时，新 generation 使用自身的文件坐标，以 `Rebuilding` 保留维度过渡语义；只有成功激活后才结束暂停，有可见已加载文件时发送恢复提示。空任务视图离开时不发送暂停提示，空视图激活也不发送恢复提示。可见性依据独立文件成员或合成前完整文件边界，不以分层后的方块数量判断；失败重试保留过渡状态，显式关闭和退出世界清除。普通/材料 HUD 的绘制与开关不在此提示修复范围内。

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
│  │  ├─ AppKernel.*            唯一应用入口：启停、加载顺序与控制帧编排
│  │  └─ RenderSettingsActions.* 渲染设置用例，协调会话分层与 projection 参数
│  ├─ block/
│  │  └─ BlockPlacementRules.*  共享运行态方块身份与“方块→实际放置物品”规则
│  ├─ i18n/
│  │  ├─ TextKeys.h            文案键 X-macro（枚举 + 稳定键名）的唯一来源
│  │  ├─ lang/                 一语言一文件的翻译 JSON；文件名就是 locale 代码
│  │  ├─ LanguageStore.*       内置资源 JSON 解析、查找表与回退链
│  │  ├─ Translator.*          当前语言选择与查表转发
│  │  └─ Message.*             跨帧保存的“键 + 参数”消息
│  ├─ settings/
│  │  └─ SettingsStore.*        config.json 读写与字段映射
│  ├─ overlay/
│  │  ├─ ImGuiOverlay.cpp       DXGI/D3D11On12、WndProc、旧 GUI/动作提示提交
│  │  ├─ ImGuiOverlay.h
│  │  └─ BoundsWireframe.*      创建结构选区的红色整体线框
│  ├─ render/
│  │  ├─ OverlayMaterials.h    纠错/整体边框/创建选区共享的原版材质与白纹理查找
│  │  ├─ ScopedShaderColor.h   绘制颜色的作用域设置与恢复，不持有跨帧状态
│  │  └─ BoxWireframeMesh.*    局部空间方框的 24 顶点生成与网格构建
│  ├─ structure/
│  │  ├─ capture/               客户端选区状态、原版结构捕获与 `.mcstructure` 导出
│  │  ├─ formats/               结构格式解析与 generation 分配
│  │  │  └─ StructureFormatLoaders.* `.mcstructure`/`.litematic` 解析为 LoadedStructure
│  │  ├─ java_to_bedrock/       Java 方块状态映射、文本组件与方块实体到 Bedrock 的转换
│  │  ├─ StructurePaths.*       UTF-8 路径转换
│  │  ├─ LayerDisplayTypes.h    分层轴/显示范围枚举与持久化整数边界转换
│  │  ├─ LayerSettings.h        分层值与真实范围快照，不含 UI 或通信协议
│  │  ├─ MaterialTracker.*      材料需求聚合、物品标识缓存与游戏线程背包快照
│  │  ├─ StructureSession.*    结构会话状态（已加载结构、变换/分层、恢复快照、状态文案）
│  │  ├─ StructureUiState.*     UI 会话状态（GUI、热键、HUD、待处理动作、材料清单）
│  │  ├─ StructureLoader.cpp    动作提示、快捷键事件、加载入口与公开 getter（菜单/GUI 由 MenuController 负责）
│  │  └─ StructureLoader.h      LoadedStructure 统一数据模型
│  ├─ ui/
│  │  ├─ hud/                  普通/材料 HUD：快照读取、内容整理、布局与原生绘制
│  │  ├─ MaterialFormat.*      菜单与原生 HUD 共用的材料名称/组数/图标身份规则
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
│     ├─ ViewMoveBasis.*        朝向/视线到整块步进的纯方向规则
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
- `block/BlockPlacementRules` 集中维护运行态方块到可放置基础方块的身份规则和稳定材料 key，并通过
  Bedrock 原生 `Block`/`ItemStack` 转换解析实际放置物品；材料清单与材料分层必须共用该 key 和排序规则，
  `place`、格式加载、材料统计和投影纠错不得各自复制名称映射。
- `structure/MaterialTracker` 只在本地玩家 tick 线程聚合材料需求和扫描背包；同一投影的材料清单只计算
  一次，先按唯一方块状态累计数量、再解析并缓存 `itemId`，避免大型结构逐格访问物品注册表。HUD/菜单
  关闭且没有消费者时停止刷新，渲染线程只读取 `StructureUiState` 的一致快照。
- `structure/StructureUiState` 持有 UI/菜单会话状态：GUI 可见性、热键配置与按下状态、HUD 开关、
  固定手势开关（Alt+滚轮结构偏移）、待处理偏移/切层/保存与材料清单；atomic、mutex 和容器均不向
  调用者暴露，交互逻辑仍在 `StructureLoader`，菜单线程专属的路径输入缓冲和当前页面由
  `MenuController` 私有持有。固定手势开关是裸 `Alt` 认领、滚轮准入和快捷栏锁三处判据的唯一来源，
  只能由它统一开关，禁止任一处单独读取其它状态。
- `structure/StructureSession` 持有结构会话状态：已加载结构、变换/分层值、恢复快照与状态文案；
  mutex、原子量、字符串和容器均不向调用者暴露，菜单/HUD 通过一致快照读取，变更通过具体操作完成。
- 分层轴与显示范围在运行时统一使用 `LayerAxis`/`LayerDisplayMode`；只有 JSON 持久化和 Dear ImGui 控件边界
  使用稳定整数编码并立即转换，禁止在投影、纠错或放置逻辑中用 `0/1/2/3` 表达分层语义。
  关闭活动投影时必须先把当前变换/分层冻结到恢复快照，再释放 `LoadedStructure`；空会话将显示层钳到
  0 的临时 UI 值不得覆盖恢复记录。
- `ui/HotkeyFormat` 只根据显式参数生成按键名与和弦字符串，不读取会话状态；快捷键交互仍由
  `StructureLoader` 的会话状态驱动。
- `input/HotkeyTypes` 是快捷键槽位顺序与数量的唯一来源；设置、会话状态、菜单和输入处理必须从
  `HotkeyId`/`Count` 推导索引与数组大小，不得复制数字槽位。
- `input/ViewMoveBasis` 是“玩家朝向/视线 → 整块步进”的唯一规则来源：纯浮点数学，不含任何 Minecraft
  或 LeviLamina 头，逻辑测试可直接覆盖方向表。移动快捷键按 `Actor::getRotation().y` 的偏航水平移动
  （忽略俯仰，只取主轴，一次只改一个坐标），固定 Alt+滚轮保留“沿视线、含俯仰、逐轴取整”的独立规则；
  只有 `StructureLoader` 读取玩家并把结果交给 `StructureUiState::queueOffsetDelta`，会话状态不得自行
  推导世界方向。
- `i18n/` 是界面文案的叶子模块：不依赖任何其它 LHolo 模块，只被展示边界（`ui/`、底部提示、
  文件对话框）使用。`i18n/TextKeys` 用 X-macro 同时定义 `TextKey` 枚举和并行的稳定字符串键名
  （如 `page.projection`）；键名是对外翻译格式的一部分，发布后禁止改名，弃用旧键、新增新键。
  翻译存放在 `src/i18n/lang/*.json`（一语言一文件，纯数据，也是唯一的翻译源）。每个文件的 basename
  是稳定 locale 代码，例如 `zh_CN.json`；文件中 `_meta.displayName` 是语言选择器显示的自称名称。
  xmake 每次加载项目时扫描该目录，自动生成被忽略的 `build/generated/i18n/Languages.rc` 和
  `build/generated/i18n/LanguageRegistry.generated.h`，再将每个 JSON 作为 Windows `RCDATA` 资源编入 DLL；运行时不读写
  语言文件，也不需要手动运行脚本。资源 ID 会由文件名规范化得到，例如 `zh_CN.json` 对应
  `LHOLO_LANG_ZH_CN`；资源 ID 冲突或不符合 locale 文件名规则会使构建失败。新增语言只需要添加一个
  JSON 文件并重新构建，不需要修改 C++ 注册表、菜单或测试枚举；`zh_CN.json` 是必需的默认/最终回退语言。
  `LanguageStore` 在 `AppKernel::load()` 时把注册表中的每个内置 JSON 解析为按 `TextKey` 索引的查找表，
  之后 `tr()` 无锁只读；缺键或空串按“当前语言 → 简体中文（`zh_CN`）→ 空串”回退，绝不显示裸键名。
  完备性由 `LHoloLogicTests` 接管（原编译期 `static_assert` 校验已随 constexpr 表移除）：测试遍历当前
  注册的每种语言，断言解析成功、元数据有效、无缺键/空值/未知键/非字符串项，并检查占位符一致。新增界面
  文案必须登记键，不得在展示边界之外写用户可见字符串字面量。
  `TextKey::None` 是显式的“空消息”哨兵（枚举首项、各语言文件填空串），`i18n::Message` 默认用它，
  因此默认构造的消息渲染为空而不是撞上某个真实条目。
- `structure`/`place`/`projection` 只保存 `i18n::Message`（键 + 语言中立参数）或 `TextKey`，
  禁止保存已翻译的句子，否则切换语言时旧文案不会更新。日志与异常消息保持英文技术描述，
  不进入语言表。
- Dear ImGui 的 `##`/`###` ID、`CalcTextSize` 数字宽度探针（`"0000000000"`、`"-0.0"` 等）
  与 `Ctrl`/`Alt`/`Shift`、`X`/`Y`/`Z` 等标识符不参与翻译。
- `MenuPage` 以尾部 `Count` 收尾并由此推导 `kMenuPageCount`（同 `HotkeyId`/`TextKey`）；导航标签表
  按 `kMenuPageCount` 定长，新增页面若不登记标签会因 `TextKey::None` 残留而编译失败。
- `ui/MenuController` 负责菜单模型构建/应用、动作回调与 GUI 渲染，只通过 `StructureSession`/
  `StructureUiState` 访问状态；`StructureLoader` 保留动作提示、快捷键事件与加载入口；普通和材料 HUD 绘制位于 `ui/hud/`。
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
  虚拟 BlockSource 查询；安装失败时必须按原逆序回滚已挂 Hook。LHolo 聊天命令已移入 oreui/OreUiScreen。
- `projection/hooks/ProjectionRenderHooks.*` 只保留有状态渲染 Hook 薄壳：命中选择抑制与 `$renderBlockEntities`
  后的投影帧入口。Hook 不读取投影状态，仅调用 `runtime/ProjectionRenderFrame` 回调；两个 Hook 的安装失败
  回滚顺序与门面原有的 LevelRenderer 顺序保持一致。
- `projection/runtime/ProjectionRenderFrame.*` 在 runtime 内实现结构激活、世界身份校验、opaque/transparent
  Mesh 提交、命中选择抑制和 `$renderBlockEntities` 后的帧入口；它只通过 `ProjectionSession` 访问会话状态，
  不直接触碰 `Projection.cpp` 的全局变量。
- `projection/runtime/ProjectionSession.*` 持有全部会话级可变状态：状态锁、`ProjectionState`、捕获边框、
  纠错填充/描边透明度、结构边框开关与下一次锚点；帧编排和只读查询通过锁内作用域回调操作状态，门面通过具体设置/
  锚点操作访问会话，`Projection.cpp` 不直接取得锁或可变状态引用。恢复锚点只属于紧随其后的恢复激活；
  显式关闭/禁用或成功发起普通文件加载时必须撤销尚未消费的请求，禁止旧锚点泄漏到后续结构。
- `projection/runtime/ProjectionInvalidation.*` 对比当前帧设置与缓存值，集中处理旋转/镜像、移动、切层和
  纠错样式变化引起的 section dirty、revision、旧 Mesh 清理及可见进度重算；placement 重建仍由调用方触发。
- `projection/runtime/ProjectionLifecycle.*` 构造尚未激活的 `ProjectionState`、解析 terrain atlas、建立 section 索引，
  并按 Worker 停止、世界事件解绑、进度清零、资源释放的顺序执行锁内清理；它将当前上下文分类为正常、
  维度变化、暂不可用或世界变化。锁、pending anchor 消费、状态激活、跨维度暂停/恢复和解锁后的
  `structure::clear()` 由 `ProjectionRenderFrame`/`ProjectionSession` 负责，门面只转发。
- `projection/runtime/ProjectionProgress.*` 保持原有 acquire/release 语义，在渲染状态计数与 GUI/HUD 读取之间发布
  无锁快照；它不扫描世界，也不自行推导纠错结果。
- `projection/mesh/ProjectionSectionBuilder.*` 统一生成原版方块、液体代理、方块实体占位和纠错覆盖
  的 CPU 几何；构建输入必须来自显式只读设置，Worker 使用 `UploadMode::Never`，同步回退保持 `Buffered`。
- 文件整体边框由 `projection/runtime/ProjectionFramePipeline` 在开启时缓存，复用 `render/BoxWireframeMesh` 批量绘制，不进入分区 Worker。
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
- `place/PlacementState` 持有放置会话状态：开关、幂等的手动按下/释放状态、手动/范围定时、近期放置格、失败计划缓存与准心投影方块名称；
  atomic、mutex、字符串和容器均不向调用者暴露，`PlaceHelper`/`PlacementExecutor` 只通过具体操作读写。
- `place/PlacementExecutor` 承载轻松/手动/范围放置的规划与执行（背包查找、交换、放置事务、
  点击候选与预测匹配），不直接读取操作系统物理输入；游戏 Hook 和右键物理状态边界留在 `PlaceHelper`，
  只向 `PlacementState` 提交状态转换并调用 `tickEasyPlace`/`tickRangePlace`。
- `input` 负责菜单期间的 Minecraft 输入边界：在 `MouseDevice`/`HIDControllerGameCoreDesktop` 输入源阻断游戏与原生页面输入；是否捕获统一读取 `StructureLoader::isMenuInputCaptured()`，不得在各输入入口分别组合 GUI 与过渡状态。
- `plugin` 只把生命周期委托给 `app/AppKernel`，不承载业务逻辑。

---

## 3. 启动、关闭与世界生命周期

### 3.1 模组启用

`LHolo::enable()` 的顺序：

1. 开放结构文件后台任务入口，再安装投影相关 LeviLamina Hook。
2. 安装辅助放置 Hook：`LocalPlayer::$tickWorld` 负责每 tick 驱动，三个 `GameMode` build Hook 负责命中真实方块时的右键状态和原版放置抑制，`GameMode::$useItem` 负责捕获指向空气的右键操作（tick Hook 失败仅告警，不阻断；单个手动 Hook 失败会分别告警并降级对应行为）。
3. 安装菜单输入保护：`MouseDevice::feed` 与 `HIDControllerGameCoreDesktop::$onKeyDown/$onKeyUp` 在游戏和原生 UI 处理前取得输入所有权。三项 Hook 状态独立告警，不阻断菜单启用。
4. 尝试安装 ImGui/DXGI Hook；图形环境尚未可用时允许后续 `Present` 重试。

配置由 `LHolo::load()` 在 enable 之前从 `mods/LHolo/config/config.json` 读取。世界退出由 `LevelListener::onLevelDestruction()` 发布轻量信号；投影渲染入口和 Overlay Present 都能观察该信号，真正的结构、辅助放置、捕获和投影清理在引擎回调之外执行。投影渲染入口仍通过 `client/level/dimension` 身份变化检测世界/维度切换，并在上下文失效时清理投影。

文件加载由 `structure/StructureFileLoader` 独立管理一个后台 future，普通加载、菜单恢复与恢复快捷键
共用入口。请求在派发前捕获位置/维度或保存的恢复参数，成功/失败均校验 StructureSession 的请求
版本；关闭、退出世界和卸载会使旧结果失效。工作线程不写菜单路径缓冲区，提示令牌由作用域自动
结束；异常转为加载失败，不留下永久忙状态。普通关闭和逐帧渲染不等待文件解析。
由于原版 StructureTemplate 借用 Level 的 unknown-block registry，文件模块在启动任务前独立注册
LevelListener，即使首个投影尚未激活，世界销毁回调也先清空会话并等待文件任务，再允许注册表释放。
卸载同样先禁止派发、提升请求版本并等待任务；只有这些销毁边界等待尚未结束的解析。
此监听器与现有投影/网格监听器分别负责文件和网格任务；前者不获取投影状态或 Worker 锁。

投影启用入口只有 `enableStructureProjection()`。它要求：

- 本地玩家存在。
- `LoadedStructure` 非空且至少有一个可渲染方块。
- Minecraft level atlas 已可用。
- 创建同步回退使用的 `BlockTessellator`，并启动单线程 `LHoloProjectionMesh` Worker。Worker 启动失败时投影仍可启用，但本次游戏会话固定使用同步回退。

### 3.2 模组关闭

当前关闭顺序：

1. 关闭结构文件任务入口、取消待发布结果并等待已有文件任务完成，再保存配置。
2. 清理辅助放置、结构、菜单、捕获和投影状态；投影停止接收网格任务，提升 Worker generation，清空待处理结果并等待 in-flight Worker 退出。
3. 卸载菜单鼠标/HID 输入源 Hook。
4. 卸载辅助放置的 tick/build Hook。
5. 卸载投影 Hook，防止渲染路径再次触发 Overlay 安装重试。
6. 关闭 ImGui 图形后端、恢复原 WndProc、移除 MinHook，并清空 Overlay 输入缓存。

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
- `RenderBlock{x,y,z,block,liquid,blockEntityNbt,materialIndex,liquidMaterialIndex}`：归一化局部坐标、可空实体方块指针、可空液体指针、可选的原生 Bedrock 方块实体 NBT，以及与材料清单稳定排序一致的实体/液体材料索引。同一坐标可同时具有实体与液体，用于含水方块；`.mcstructure` 直接保留原生 NBT，`.litematic` 仅为已有明确转换规则的方块实体生成 NBT。

### 4.2 `.mcstructure`

加载流程：

1. 读取文件，单文件上限 512 MiB。
2. 使用 Bedrock little-endian binary NBT 解析。
3. 校验 `size`、`structure.block_indices`、`palette.default.block_palette`。
4. 校验每个 block index layer 的长度等于结构体积。
5. 使用客户端 Level 的 unknown-block registry 构造原版 `StructureTemplate`，再把完整根 NBT 交给 `StructureTemplate::load()`。不要逐项调用 `Block::tryGetFromRegistry()`：那条捷径会绕开格式版本升级、世界方块调色板和 unknown-block registry，旧状态可能被错误解析成未知方块。
6. 从加载后的 `StructureTemplateData` 取得原版已升级的主/副索引数组和 `StructureBlockPalette`，用 `StructureBlockPalette::tryGetBlock()` 解析方块。不要用 `StructureTemplate::tryGetBlockAtPos()` 遍历文件：26.20 客户端该接口的坐标访问约定与 `.mcstructure` 的线性索引布局不一致，曾导致门上下半块错位和水取成错误方块。26.51 起 `mExtraBlockIndices` 是 `std::optional`：副层没有任何方块（全部为原版 `NO_BLOCK_INDEX_VALUE`，即结构不含水/含水方块）时，原版加载器会把整层收成空值，这不是损坏，按全空索引处理；仅当副层为空但文件自身第二层仍统计出占用格子（数据自相矛盾），或层有值但长度不等于体积时，才按“索引数量与结构体积不一致”拒绝。
7. `block_indices` 有两种已知的文件形状（`format_version` 1 与 2），预校验必须都接受：
   - format 1（旧）：`List<List<Int>>`——两个 Int 列表，主层 + 可选副层（水）。
   - format 2（1.26.5x 起游戏导出）：`List<IntArray>`——每层一个 `TAG_Int_Array`，副层取消（含水内联为方块状态），通常只有一个 IntArray，长度等于体积。
   - 差异仅在预校验与层读取（`inspectBlockLayer` 有 List/IntArray 两个重载）；真正的解析仍交给原版 `StructureTemplate::load()`，它把两种形状规范化为 `mBlockIndices` + `optional<mExtraBlockIndices>`。
8. 依照格式文档的 ZYX 顺序还原线性索引：`index = x * (sizeY * sizeZ) + y * sizeZ + z`。主副层分别解析后，同一坐标的非液体写入实体层、液体写入液体层。
9. 门的上下半块本来就是两个坐标、两个完整 palette state，不做合并；格式升级后的上下半块、铰链、朝向和开关状态由原版加载器保留。
9. 原版加载失败、原版尺寸与文件尺寸不一致时直接拒绝加载，不再带着未知方块继续渲染。

适配新版本时重点检查：`StructureTemplate` 构造与 `load()`、`StructureTemplateData` 索引访问、`StructureBlockPalette::tryGetBlock` 的符号及语义、NBT 标签路径和两个 block index 的格式。固定回归门的上下相邻坐标与水坐标；不要退化回手工注册表解析，也不要未经验证改用 `tryGetBlockAtPos()` 遍历。

渲染与纠错约束：

- 实体模型走原版 `tessellateInWorld()`，并按原版 render layer 分桶。相邻实体方块只在 LHolo 生成网格的线程局部作用域内通过 `BlockSource::getBlock()` 暴露，供门等邻居相关模型正确生成；作用域外始终调用原版函数，不改变世界。
- 1.26.51 起栅栏、玻璃板、铁栏杆等连接方块的连接臂由原版从方块自身的派生连接状态读取，而结构调色板从不存储这些状态，直接网格化会全是光杆。网格化与纠错前经 `ProjectionRules::withFlattenedConnections()` 调用原版 `BlockType::connectionUpdate()` 重算连接：网格化时 `BlockSource::getBlock()` 钩子让重算读到投影虚拟邻域（旋转/镜像后方向因此仍正确），纠错时读真实邻域，使期望方块与真实方块的派生状态可比。不要退化回手搓 `canConnect()`/`setState()` 推导：那只能覆盖 `FenceBlock` 一族，玻璃板等数据驱动原型的连接不存储在内建 `Connection*` 状态里，只有原版更新认识其规则。
- `connectionUpdate()` 会把重算结果写进传入的区域，直接调用曾把可选中、有碰撞的幽灵栅栏写进真实世界（重进世界才消失）。因此调用必须包在 `ScopedRegionWriteSuppression` 内（`ProjectionVirtualWorld`），`ProjectionGameHooks` 里的两个 `BlockSource::setBlock` 钩子在抑制期间吞掉写入并返回成功，只取更新的返回值；不要在抑制作用域外做任何依赖写入生效的操作。
- 所有投影方块实体创建完成后，使用 `BlockActor::isType()` 识别箱子，并在同一虚拟世界作用域内调用原版 `ChestBlockActor::_tryToPairWith()` 配对。必须先建立完整的虚拟方块和方块实体表，再执行配对；结构 NBT 中的 `pairx`/`pairz` 是原世界绝对坐标，不能直接作为投影配对坐标使用。
- 水和岩浆使用贴图 proxy 单元壳，完全由 LHolo 自绘，不与原版世界或区块管线交互：仅 Missing（未放置）状态的液体格绘制半透明截顶外壳，最上层液体格顶面固定为原版源液体高度 8/9（`getHeightFromDepth()` 在 1.26 上对源液体的返回值不可靠，不再使用；逐格流动深度不参与视觉，只参与纠错比较），上方有同液体时侧壁满格；相邻同种液体剔除共享面；UV 取自 `BlockGraphics::getForBlock(liquid)->getTexture(0, 0)` 的 terrain atlas 水/岩浆贴图；水顶点色为原版蓝 #3F76E4（atlas 水贴图无色），岩浆顶点色为熔岩橙 #FF8010（0x001080FF，与原版岩浆色谱一致，避免侧面流动贴图及无色材质渲染发白）；alpha 固定为水 0.65、岩浆 0.85，保留移除整体透明度设置前的默认效果；经 `liquidProxySectionMeshes` 独立网格在 alpha pass 用 `mMatBlendBlock` + terrain atlas 提交（与玻璃同路径），按 section 距离排序。静态贴图无波浪动画是已知限制。纯液体格的 Missing 不再叠加蓝色纠错面/描边（proxy 本身即提示），WrongType/WrongState 仍保留红/黄纠错面。`.litematic` 加载时液体路由到 `RenderBlock::liquid` 字段，与 `.mcstructure` 语义一致。
- 气泡柱必须保留独立的实体/液体语义：`RenderBlock::block` 是气泡柱本体，`liquid` 只保留原版加载器实际解析出的另一层液体。无水气泡柱不补造水；含水气泡柱保留气泡本体与独立水体绘制。不能仅凭原始文件的索引层数判断含水，格式升级由原版 StructureTemplate 负责；Java 映射仍依据其来源格式的映射规则处理水层。
- 气泡柱保留通用 `tessellateInWorld` 的世界/缓存设置；若该入口未追加顶点，调用实际 SDK 提供的 `tessellateBubbleColumnInWorld` 一次。2026-10-19 无水与含水样本实测中，通用入口均追加 0 个顶点（graphics shape=-1），专用入口追加 32/36 个顶点；其返回值为 false 时也可能有 36 个顶点，因此仍以网格增量判断几何。无几何的气泡柱跳过通用方块实体占位壳，避免白色立方体。原版资源提供气泡柱动画 atlas，水体仍走独立液体网格。本修复不修改分层数据、纠错、世界或任务生命周期，不新增刷新/重试队列。
- 用户已确认无水样本 `D:/temp/气泡柱.mcstructure` 和含水样本均显示通过。临时诊断日志、原子计数、同步 flush 和仅供诊断的日志依赖已移除；保留实测有效调用顺序，不在本轮清理中推断专用入口可绕过通用前置设置。
- 纠错分别比较 `BlockSource::getBlock()` 与 `getLiquidBlock()`；气泡柱使用 `getExtraBlock()` 取得实际副层，避免把气泡柱本体误认作副层水。只对结构实际要求的液体检查缺失、类型和状态，不因无水样本缺少副层水而误报。
- 材料轴的完整视图仍显示没有材料索引的 Extra 世界方块；选择具体材料或材料范围时，Extra 因无法归属某项材料而隐藏。
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

原生 IntentFacet 必须在构造时建立 EnableNonOwnerReferences::mControlBlock；SDK 生成头文件未提供初始化构造，不能直接依赖默认空指针。销毁、discard 和 init 失败都会经过游戏基类析构。回归测试同时覆盖原生对象生命周期与前端协议；具体转储依据见 OreUI 迁移说明。

OreUI 创建页使用 application/useCapture → bridge → CaptureProtocol/CaptureBindings → capture/CaptureActions。坐标文本暂存在前端，有效坐标和实体选项自动同步原生选区，不增加“应用选区”按钮；取玩家位置和捕获在现有应用控制帧执行。另存为使用独立 STA 的现代 IFileSaveDialog，关闭页面取消选择；提交前校验 revision，防止旧保存结果用于已变化的世界或选区。旧 ImGui 页仍复用原有捕获 API。

“创建结构”不经过 `LoadedStructure`，也不会自动载入投影。选区端点先按每轴最小值/最大值归一化，两个端点都包含在内；导出前由 `BlockSource::areChunksFullyLoaded(min, max)` 拒绝客户端尚未完整加载的范围。

捕获只使用 LeviLamina 26.40.0 客户端头文件确认的原版接口：

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
- X/Y/Z 输入与快捷键只修改 `userOffset`，不改原始结构数据。数字输入按世界轴精确对齐；移动快捷键按
  玩家朝向产生整块步进（见 9.4），两者最终都只是 `userOffset` 的整数增量。
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
- biome-tinted 方块（草和四种 foliage tint）的群系颜色使用 `BiomeColorSampling::getTessellationPolicy()`
  计算并与网格顶点色相乘。26.40 起 `BlockTessellator::buildBiomeWeights()` 不再导出、tessellator 内的
  群系权重缓存无法填充，因此策略调用的 `BiomeTintCache` 参数传 `nullptr`，让策略按坐标现场从
  `BlockSource` 采样群系色（禁止把未填充的缓存指针传进去，那会得到白色/空染色）。草方块仍由原版
  Tessellator 按面着色，不能把整块顶点统一乘绿色，否则泥土面也会变色。树叶类型和颜色不由 LHolo 维护。
- 渲染虚像的 opaque/alpha/alpha-one-sided 桶必须使用 ItemInHandRenderer 的 **Colored 系材质**
  （`mMatOpaqueBlockColor`/`mMatAlphaColoredBlock`/`mMatAlphaOneSidedColoredBlock`）：26.40 的
  entity 族着色器输入签名没有 COLOR0，普通版材质（`opaque_block`/`entity_alphatest` 等）会把顶点色
  整体丢弃，树叶等灰度贴图方块会显示成白色。Colored 缺失时回退普通版。
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

投影方块固定沿用原 100% 时的渲染规则，尽量使用匹配的原版 opaque/alpha/one-sided 材质；只有原有 Blend 桶在透明 pass 中按分区距离排序并使用 blend material。液体继续使用独立透明网格。

### 6.3 相机相对坐标

网格生成后，顶点减去投影 `renderOrigin`。每帧只在 world matrix 中应用相机相对平移。不要把几万格绝对坐标直接上传为 float 顶点，否则会复现远坐标渲染错误。

### 6.4 灵动视效 / Deferred 路径

通过 `ItemInHandRenderer::mIsDeferredEnabled` 判断灵动视效路径。

- 普通路径：纠错面使用选择覆盖材质，并暂时借用标准 alpha blend 状态。
- Deferred 路径：使用已验证可见的 outline material，提交纠错 QuadList 前临时替换 primitive、blend、depth bias 和 slope bias。
- 每次临时改动材质状态后必须立即恢复，不能污染 Minecraft 后续绘制。

纠错颜色状态隔离：保留原来的顶点色、顶点 alpha、两组 Missing/Wrong 网格、混合及深度状态，
仅在纠错面/描边提交期间将 `ScreenContext::currentShaderColor` 固定为中性白色 `(1,1,1,1)`。
结束或异常离开时恢复原颜色，并置 `dirty=true` 让下一次绘制重新同步常量；不能直接恢复原来
的 dirty 标志，否则原本为 clean 的状态可能使后续绘制漏掉颜色恢复。整体边框、投影模型和
方块实体均在作用域之外，作用域不保存跨帧对象。字段核对实际依赖 LeviLamina 26.51.0。
此修复不修改世界光照，不增加网格、绘制调用、查询或异步任务；用户已确认显示效果恢复。

四个可见纠错绘制列表都为空时，在整体边框绘制之后直接结束纠错阶段，避免无意义地设置/
恢复全局颜色及临时材质状态；原有网格有效性检查继续保留。
白天实机诊断的 13 条覆写前颜色均为 `(1,1,1,1)`，与修复使用的中性白色一致；临时采样日志、
计数、计时和时间查询均已移除。版本适配时仍须回归白天/夜间、15%/100%、描边及面颜色、穿透开关、
灵动视效、切维度/退出，并对比原版选框和手持物；逻辑测试不能证明 GPU 视觉结果。

新版本若灵动视效下投影全白、全黑或纠错颜色消失，优先检查这些材质字段是否仍存在、语义是否改变，以及 deferred 标志是否仍可靠。不要简单提高颜色亮度掩盖材质错误。

---

## 7. 纠错状态机与视觉提示

### 7.1 状态定义

同种原版 `minecraft:*_sapling` 树苗之间只忽略 `age_bit` 生长阶段差异。Litematic 的 Java
`stage` 会映射为该状态，而玩家放置树苗时游戏会重置它；这不是可由玩家控制的朝向或放置错误。
`minecraft:bamboo`（竹子）的所有状态（`age_bit`、`bamboo_leaf_size`、`bamboo_stalk_thickness`）均为游戏引擎根据高度与随机 Tick 模拟的动态植物学生长状态，不存在任何由玩家朝向或操作控制的摆放姿态，类型相同即判定为匹配与正确。
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
- 整体结构边框、创建选区与纠错描边共用 `render/OverlayMaterials.h` 取得当前
  LevelRendererPlayer 的原版 outline selection material；不再扫描材质组或缓存发光告示牌材质。
- `render/ScopedShaderColor.h` 在一次绘制作用域内设置并恢复颜色，恢复后强制 dirty，
  保证后续原版绘制同步回原颜色。纠错沿用已验证的中性白色；整体边框显式传青色
  `#33D6FF`，创建选区显式传其自身颜色（默认红色）。方框顶点使用中性白色，避免
  uniform 与顶点重复着色。原版 BlockSelectionOutline 着色器分析显示其输出取颜色常量，
  但各画质路径仍须实机确认最终常量绑定与颜色。
- `render/BoxWireframeMesh.*` 仅生成局部坐标十二条边、24 个顶点和白纹理中心 UV；
  调用方决定范围和调试名，并独立持有缓存与坐标变换。整体边框统一在不透明帧阶段
  创建，与分区 Worker 的启用/降级状态无关；分区构建器不再包含边框生成入口。
  整体边框和创建选区均在渲染线程使用 Buffered 上传并贴齐真实边界，不做几何外扩。
  用户已实测此材质在无外扩时不闪烁。创建选区仅颜色改变
  时更新绘制参数，无需重建网格。公共 render 层不读取投影会话、玩家、配置或创建状态。
- 方框每次显示仍一次网格提交，颜色作用域只复制数值，不进行堆分配或世界查询；转头
  与移动不触发网格生成。网格仍在原来的不透明阶段生成、透明阶段提交，生命周期不变。
- LevelRenderer 缺失时跳过叠加阶段，不再拿方块材质代替选框材质；白纹理查找接收已校验
  的 LevelRenderer 引用。资源尚未就绪时的材质/纹理/Mesh 有效性检查，以及颜色恢复后的
  dirty 更新仍保留，不能作为“兜底”删除。

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
- 纠错样式改变：纠错顶点 alpha 改变，全部分区一次性失效，之后继续缓存。
- X/Y/Z 整体移动：GPU 局部网格不重建，只更新 world lookup、纠错扫描位置和矩阵平移。
- 显示层变化：只失效可见性跨越边界的分区。

### 8.5 HUD 零额外扫描

材料 HUD 标题左侧显示本地化“缺失材料”，页码单独放在同一行右端，与材料数量右对齐，每页沿用 materialHudMaxItems（默认 5）。ui/hud/MaterialHudPaging 持有临时页码与上下文；input/MaterialHudPageRequest 仅传递原子翻页意图，不依赖 HUD。上一页/下一页追加到 HotkeyId 尾部，默认按键/修饰键均为空，沿用现有捕获、重复绑定清除、按住去重、释放处理与 SettingsStore 保存。HUD 未启用或维度暂停时不认领翻页输入，菜单及编辑期间丢弃请求；退出世界清除待处理意图。文件、分层或每页项数改变回第一页，数量减少自动钳制有效页，首尾不循环。分页只消费原有材料/背包快照，不增加世界查询；nth_element 分离前页后仅排序当前页，数量相同按原索引稳定排序。页码不写配置、不进入任务或投影业务。

材料 HUD 的 `materialHudStackConversion` 是独立持久化布尔偏好，默认关闭；OreUI 材料 HUD 页通过现有 SettingsActions 字段通路即时修改。关闭只显示缺失总数，开启使用 `43x64+34` 格式并按物品实际堆叠上限换算，不同时显示总数。格式化位于 `ui/MaterialFormat`，原生 HUD 与编辑示例共用；材料业务统计与菜单材料清单保持原有行为。显示项数通过 `materialHudMaxItems` 持久化，范围 1～50、默认 5，只对当前页排序；数量独立存入 HudLine 并在 NativeHudPainter 统一右对齐，材料名称完整显示，面板按每行名称与数量的实际合计宽度自适应，列间距为字高的 0.8 倍，不将不同材料的最长名称和最长数量额外相加留白；超过视口宽度时同比例缩小文字、图标和间距；视口高度限制继续生效。材料图标通过完整 ItemStack 保留原生附加值与用户数据；有蓝图方块实体 NBT 时，游戏线程为每个既有调色板身份的显示样本创建临时 BlockActor，经原生 asItemInstance 取得床/旗帜等物品，保留其图案和用户数据。不逐帧建物品、不扫描真实世界，不改变当前材料分组或背包计数。最终审计删除无调用的 formatStackCount、完整物品接入后重复的 aux 转发/缓存及仅测试消费的布局输出，保留默认关闭的堆叠换算；旧隐藏 ImGui 按项目约束保留。

纠错扫描同时维护：

- `progressCorrect` 与正确计数。
- `progressErrorKind`：每个结构坐标使用一个字节记录无错误、类型错误或状态/朝向错误。
- `progressWrongTypeCount` 与 `progressWrongStateCount` 两个独立计数。
- 稀疏 `progressExtraCount`，不改变建造进度分母，也不进入材料需求。
- 原子发布的 `placed/total/wrongType/wrongState/extra`。

HUD 每帧只读取原子计数，不查询世界、不遍历结构。`ui/hud/HudContentSource` 读取既有会话/进度/材料快照，`HudContent` 整理本地化行，`HudLayout` 计算四角与同角堆叠，`NativeHudRenderer` 只测量/绘制这些展示数据；业务模块不依赖原生绘制 SDK。材料图标身份与 OreUI 清单共用 `ui/MaterialFormat`，`NativeHudIcons` 在 tick 线程准备显示物品、在本次匹配的原生 UI flush 中绘制，不在渲染线程解析注册表。

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

`input/MenuInputGuard.cpp` 只 Hook 鼠标输入源和键盘按下/释放三个入口。菜单关闭时只额外判断当前事件是否为投影已取得所有权的滚轮输入，随后立即进入原函数；不扫描方块、不分配内存、不加锁、不查询玩家、不写逐次日志。

---

## 9. GUI、快捷键与输入交接

### 9.1 ImGui 页面

GUI 覆盖当前 `ImGuiIO::DisplaySize`，侧栏导航包含：投影、创建结构、结构变换、渲染设置、HUD、快捷键、界面设置和实验性功能。界面缩放范围 1～5。

此处记录仍在使用的 ImGui 实现。OreUI 在 `frontend/` 独立开发，按功能接入后逐步删除对应旧页面；
菜单与 HUD 设置接入位于 `src/oreui/`；普通/材料 HUD 的画面由 `src/ui/hud/` 在原版 HUD 回调绘制。不要把 JS 协议加入结构或投影内部模块。原生 HUD 使用游戏 GUI 缩放，不读取旧 ImGui 的 uiScale。

GUI 是全屏 ImGui 窗口，不是切换 Minecraft 窗口模式。

### 9.2 输入所有权

菜单打开时，WndProc 将鼠标、键盘、字符和 Raw Input 交给 ImGui；`MenuInputGuard` 同时在 `MouseDevice::feed` 和 HID 键盘入口阻止输入进入 Minecraft 原生页面。两层都读取 `StructureLoader::isMenuInputCaptured()`，避免窗口消息已被界面消费、原生页面仍收到内部输入。

打开菜单前记录游戏当前按键/鼠标按下状态，并向游戏补发必要的 key-up/button-up，避免“按住移动键打开菜单，关闭后角色自动走”。

关闭菜单时：

- 将鼠标恢复到游戏客户区中心。
- 短暂限制鼠标到客户区并设置输入阻断窗口。
- 消费菜单关闭动作对应的释放消息。
- 防止关闭后的首次右键变成 ESC/暂停效果或鼠标落到客户区外。

菜单可见期间必须让系统光标保持可见，这是与其它覆盖层模组之间的隐含契约：

- ImGui 的自绘光标会让 Win32 后端每帧执行 `SetCursor(nullptr)`，`GetCursorInfo()` 因此报告光标隐藏。其它模组（例如 ChiyanMap）以该系统标志判断“游戏是否仍抓着鼠标”，会把光标夹回客户区中心，导致菜单里的鼠标无法移动。
- 因此菜单期间 `MouseDrawCursor` 保持 false，光标形状交给 ImGui 后端设置；并在窗口线程通过 `kMsgAcquireMenuCursor` 用 `ShowCursor` 把显示计数顶到 ≥ 0。`ShowCursor` 返回的是调用后的计数，`> 0` 表示光标本来就可见、需要撤销这次探测增量；关闭、失焦和卸载时只归还自己加的增量，Minecraft 的负数计数不受影响。
- 菜单可见时，客户端区域的 `WM_SETCURSOR` 不再分发给游戏，避免光标被重新隐藏；窗口边框等非客户端区域仍然放行，不影响缩放光标。

不要依赖“消息积压”解释输入 bug；应检查鼠标坐标、Capture/ClipCursor、按键状态和 Raw Input 所有权。

### 9.3 Bedrock 输入源保护

WndProc/Raw Input 只覆盖 Windows 消息边界，不能作为阻止 Minecraft 原生 UI 和游戏动作的唯一保证。当前在 Bedrock 输入源增加第二道边界：

- `MouseDevice::feed` 与 `HIDControllerGameCoreDesktop::$onKeyDown/$onKeyUp` 是主路径；菜单可见或关闭过渡期间直接停止向原生 UI 分发，F11 例外并继续交给 Minecraft 的全屏切换生命周期。只有存在活动投影时才取得 Alt 按键所有权，按住 Alt 后也仅 `MouseAction::ActionWheel` 由投影移动取得所有权；没有投影时的 Alt，以及数字键、手柄和其他选槽路径均不受影响。
- 打开菜单前向 Minecraft 补发的释放消息由 `MenuInputHandoffScope` 临时放行；禁止把所有 key-up/button-up 长期放行，否则原生按钮通常会在释放边沿触发，重新产生穿透。
- 三个 Hook 的安装状态由 `MenuInputGuardStatus` 分别返回；单个 Hook 冲突不得伪装成整体成功，也不得导致菜单模块无法启用。
- PreLoader/LeviLamina Hook 返回值是 0 成功、非 0 失败，禁止用 `< 0` 判断安装结果。
- 输入源 Hook 只在菜单持有输入期间暂停本机的移动、放置、使用、攻击和原生 UI 操作；关闭菜单后立即恢复，不修改其他玩家或服务端状态。
- 鼠标按下状态在打开菜单前通过合成释放消息归还给游戏，因此无需再维护 `GameMode::$startDestroyBlock/$continueDestroyBlock` 专用保护。

### 9.4 快捷键配置

修饰键位图：Ctrl=1、Alt=2、Shift=4。捕获快捷键时忽略单独修饰键，F11 不允许成为模组快捷键。

恢复默认快捷键：

- 菜单：Alt+M。
- 左右/前后：Ctrl+方向键（按玩家朝向水平移动，只取主轴的整块步进）。
- 上下：Shift+上/下。
- 显示层：Alt+上/下。

“恢复默认快捷键”同时把固定手势开关（`Alt + 滚轮 调整结构位置`）恢复为默认开启。该开关是持久化的
输入偏好，不随退出世界清理；关闭后模组的裸 `Alt` 认领、滚轮接管与快捷栏锁定同时失效，因此不存在
“已经关闭但仍锁定快捷栏”的中间状态。

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

需要绘制旧 ImGui GUI 或动作提示时（普通/材料 HUD 不经过此路径）：

1. 从当前 swap chain 取得当前 back buffer。
2. 为该帧创建 wrapped resource 和 RTV。
3. `AcquireWrappedResources`。
4. 绘制 ImGui。
5. 解除 RTV 绑定。
6. `ReleaseWrappedResources` 并 `Flush`。
7. 释放该帧 wrapped resource 和 RTV。

不跨帧持有 back buffer wrapper。旧 GUI 和动作提示都不显示时跳过这条提交路径。

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
- `MouseDevice::feed` 与 `HIDControllerGameCoreDesktop::$onKeyDown/$onKeyUp`（`input` 模块）：菜单期间在 Bedrock 输入源阻止游戏和原生 UI 接收输入；Alt 投影移动期间只阻止对应的滚轮动作进入原生热栏路径。

`PlaceHelper` 分别记录上述放置 Hook 的安装结果，并且卸载时只逆序卸载成功安装的入口。Hook 返回值按
PreLoader/LeviLamina 的约定处理：`0` 表示成功，任何非 `0` 值都表示失败；不得用 `< 0` 判断。

新版本最容易变化的是成员函数符号、签名、调用层次和 render pass 时序，必须逐一验证，不能只以“Hook 安装成功”判断适配完成。

---

## 12. 轻松、手动与范围放置

### 12.1 行为

菜单“投影”页提供三种互斥模式：

- 轻松放置：准心指向投影中的蓝色缺块位置（`correctionStates == Missing`）时自动放置。
- 手动放置：准心定位规则相同，但只有按下/按住右键时才放置；命中真实方块时，首次按下立即尝试，持续按住经过 150 ms 初始延迟后每 120 ms 重复；指向空气中的浮空投影时，空气右键入口创建同一幂等按下请求，重复 `$useItem` 回调不会重置初始延迟或重复首击。
- 范围放置：每 tick 查询玩家周围配置半径（1～4）内的缺块，按距离从近到远选择一个满足触及距离、物品和原版放置预测的候选。

三种模式都会在完整 36 格玩家背包中寻找对应物品；背包栏命中时先通过服务端同步的普通背包事务与当前快捷栏槽位交换，下一 tick 再放置。液体单元与隐藏层不参与放置。

### 12.2 实现要点

- 驱动：直接 Hook `LocalPlayer::$tickWorld`，模拟线程每 tick 一次，不使用 LL 事件系统（与全项目 Hook 风格一致）。手动模式另外 Hook `GameMode::$startBuildBlock`、`$buildBlock` 和 `$stopBuildBlock` 获取命中真实方块时的右键按下、持续与释放状态，并阻止同一次操作被原版重复放置；指向空气时 Bedrock 不进入 build 链路，因此通过官方 `GameMode::$useItem(ItemStack&)` 入口创建幂等按下请求。浮空路径可能缺少 `$stopBuildBlock`，所以右键物理释放仅在 `PlaceHelper` 的 tick Hook 边界补充检测；`PlacementExecutor` 只消费逻辑状态。
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

当前配置版本：`13`。完整字段、默认示例、快捷键写法及迁移规则见 [配置文件说明](docs/CONFIGURATION.md) 和 [默认配置](docs/config.example.json)。

配置只保存界面、渲染、HUD、输入、辅助放置偏好，按 `interface`、`rendering`、`hud`、`input`、`placement` 分组。HUD 内容、位置、缩放与背景不透明度归入各自面板；快捷键以 `Alt+M` 等组合字符串或 `null` 保存；百分比与秒明确标注单位，固定分组顺序、4 空格缩进和末尾换行。

旧单文件最近路径、`saved*` 恢复记录及旧 ImGui `uiScale` 不再进入正式偏好文件。任务与各文件维度/坐标继续由 tasks 独立保存。隐藏 ImGui 和旧单文件恢复的内存逻辑按项目约束保留，但不再从配置恢复或写入。运行态 Settings 值只保留实际偏好，菜单/应用用例不依赖新 JSON 分组。

SettingsStore 在读取时兼容旧平铺字段（包括旧世界轴快捷键和 hudShowBlockEntity 别名），完整校验后一次性发布；保存只写 v13，第一次覆盖旧格式前备份为 config.json.legacy.bak。有效配置在启动应用后规范化；损坏、错误类型、非法组合键/锚点和未来版本拒绝读取，后续保存也不会覆盖。写入经临时文件关闭和系统原子替换，进程内写入串行化。原生 HUD 使用的归一化位置、缩放和不透明度均保持原偏好。

堆叠换算默认关闭、每页默认 5 项，翻页快捷键默认不绑定。手动/轻松/范围放置模式和 HUD 页码是会话状态；只有知情确认、放置半径与冷却参数持久化。

---

## 14. 构建、发布与部署

完整本机安装目录统一使用 `xmake`，输出 `bin/LHolo/`（包含资源包，不生成 ZIP）。默认使用 `tools/oreui/runtime/` 中固定版本、哈希校验的构建输入，无需安装游戏；`oreui_hbui` 仍可显式覆盖资源源路径。本地和 GitHub 默认 `xmake` 均同时构建原生与前端资源。构建/版本/哈希校验、工具分层及公开发行边界见 [打包说明](docs/OREUI_PACKAGING.md)。

### 14.1 依赖

- Visual Studio 2022 C++ 工具链（提供链接器与 Windows SDK）
- LLVM 22（`clang-cl`）
- xmake
- LeviLamina 26.51.0 client
- levibuildscript
- Dear ImGui 1.91.9，Win32 + DX11，静态
- MinHook
- zlib

编译设置：C++20、MD runtime、UTF-8、警告等级 `/W4`、工具链 `clang-cl`。

链接设置：`/DELAYLOAD:bedrock_runtime.dll` + `delayimp`。clang-cl 下只有 `/EHs` 生效，`/EHa` 会被静默忽略；`ProjectionRenderFrame.cpp` 依赖 C++ 异常，缺少 `/EH` 时会直接编译失败。

本机若出现 `LNK1181 无法打开输入文件 user32.lib`，说明 xmake 没有找到 Windows SDK 库目录（常见于 SDK 装在非默认盘、或注册表 64 位视图的 `KitsRoot10` 指向了不存在的路径）。此时在运行 xmake 前补上 `LIB` 环境变量即可，无需改仓库：

```powershell
$env:LIB = "<SDK>\Lib\<版本>\um\x64;<SDK>\Lib\<版本>\ucrt\x64;<VS>\VC\Tools\MSVC\<版本>\lib\x64"
```

### 14.2 干净 Release 构建

```powershell
xmake repo -u
xmake clean
xmake f -a x64 -m release -p windows --target_type=client -y
xmake -r
```

临时排查崩溃可启用完整 PDB，保持 Release 的 `/O2` 优化及 `/OPT:REF`、`/OPT:ICF`：

```powershell
xmake f -m release --debug_symbols=y -y
xmake -y
```

符号开关仅作用于 LHolo 原生目标，默认关闭。原生打包器会把匹配的 `LHolo.pdb` 复制到 `bin/LHolo/`；本机诊断时将 DLL/PDB 作为同一对保存、部署，不能用新 PDB 解析旧 DLL。正式六文件安装清单不包含 PDB，现有部署脚本不会自动复制符号；本机诊断部署可在游戏退出并成功部署 DLL 后另行复制匹配 PDB。恢复普通构建用 `xmake f -m release --debug_symbols=n -y` 再构建；不要发布残留符号文件。

唯一发布目录：

```text
bin/LHolo/
├─ LHolo.dll
├─ manifest.json
├─ LICENSE
└─ resource_packs/LHoloUI/
   ├─ manifest.json
   └─ hbui/
      ├─ lholo-menu.html
      └─ lholo-menu.js
```

`THIRD_PARTY_NOTICES.md` 只保留在源码仓库，不复制到 `bin/LHolo`。不要发布 `build/`、`.exp`、`.lib`、日志、崩溃转储、配置、开发工具或测试结构文件。

### 14.3 本机部署

OreUI 随模组资源包分发的目标目录、Mojang 清单规则和升级保留策略见 [OreUI 发布与更新方案](docs/OREUI_PACKAGING.md)。2026-10-25 用户已实测内置资源包菜单启动成功：由 `OreUiMenuResources` 仅适配 LHolo HTML/JS，解决原版 Hybrid 对未受信任包的读取限制。OreUI 页面是必需资源，正式包包含六个安装文件；原生编辑场景定义在内存中提供。正式发布使用对应标签提交在 GitHub Actions 编译的原始 artifact ZIP，`tools/oreui/release_package.py` 校验版本与清单，发行 CI 复核完整附件及远程构建来源；GitHub CI 运行同一完整构建，上传包含 DLL、清单及 OreUI 资源的目录。

测试路径：

```text
D:\games\LeviLauncher\MC\versions\1.26.51.01\mods\LHolo
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

### 界面语言

- [ ] 默认语言为简体中文；首次启动与旧配置（无 `language` 字段）均显示中文。
- [ ] “界面设置”页的“语言”下拉包含所有 `src/i18n/lang/*.json` 的 `_meta.displayName`，切换后立即生效：导航、各页标签、
      按钮、复选说明与下拉选项全部切换。
- [ ] 切换语言后，投影页状态行与底部提示同步切换，不残留上一语言。
- [ ] 英文下逐页检查换行与溢出：导航栏、值行标签、材料清单表格列、实验性功能说明弹窗。
- [ ] 重启游戏后语言代码保持；手工把 `config.json` 的 `language` 改成未知字符串、整数或缺失时均回退中文。
- [ ] 材料清单中的水与熔岩名称随界面语言变化，其它方块名仍跟随游戏语言。
- [ ] 聊天栏 `LHolo`、Alt+M 与快捷键绑定文案在英文下仍正确。
- [ ] 投影 HUD 与材料 HUD 的文案随界面语言切换（文件名、层/显示范围、进度、错误计数、辅助放置模式、
      “缺失材料”标题与“正在统计…”等状态）。
- [ ] 结构文件打开/保存对话框的筛选器名称随界面语言切换，筛选行为不变（默认仍为全部投影结构）。
- [ ] 已知且有意保留：加载失败详情来自 `structure/formats`，日志与状态行共用同一条文本，因此英文界面下
      会显示成 `Load failed: <中文技术细节>` 的中英混排。若将来要消除，必须让加载器改返回错误码并在界面侧翻译，
      不能只翻界面前缀。

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
- [ ] 移动快捷键方向与朝向一致（左/右垂直于朝向、前/后沿朝向、上/下为世界垂直，只走单一主轴），数字输入仍按世界轴。
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
- [ ] 栅栏、玻璃板在投影中互相连接；旋转/镜像后连接方向仍正确；不含水/含水方块的 `.mcstructure` 能正常加载。
- [ ] 移动/旋转投影后原地不残留可选中、有碰撞的方块（真实世界未被写入幽灵方块）；退出世界再进入后投影区亦无残留实体方块。
- [ ] 已按投影建好的栅栏、玻璃板纠错为绿色，不再误报“状态错误”（黄标）。
- [ ] 投影方块保持原 100% 时的显示效果，无整体黑块；玻璃与液体透明显示正常。
- [ ] 旧配置残留 `opacity` 时正常加载，保存后不再输出该字段；菜单中不再显示投影透明度。
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

- 在展示边界（`ui/`、底部提示、文件对话框）之外写用户可见文案，或把已翻译的句子存进
  `StructureSession`/`StructureUiState`/投影状态。界面文案一律经 `i18n::TextKey`。
- 翻译 Dear ImGui 的 `##`/`###` ID、`CalcTextSize` 数字宽度探针或 `Ctrl`/`Alt`/`Shift` 等标识符。
- 用“英文原文当键”的写法新增文案：改名会静默丢掉翻译，也无法做编译期完备性校验。
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
D:\games\LeviLauncher\MC\versions\1.26.51.01\logs\latest.log
```

崩溃文件：

```text
D:\games\LeviLauncher\MC\versions\1.26.51.01\logs\crash\trace_*.log
D:\games\LeviLauncher\MC\versions\1.26.51.01\logs\crash\minidump_*.dmp
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
- `ui/StructureFilePicker`：异步单任务、Unicode 路径、扩展名校验、取消与过期结果丢弃（替换系统对话框回调的隔离验证）。
- `i18n`：动态 locale 注册表、语言代码查找与未知代码回退、内嵌 JSON 解析统计（解析成功、元数据有效、
  无缺键/空值/未知键/非字符串项）、每个键在所有已发现语言下均非空、越界键返回空串、切换语言后的查表结果
  与显式语言查询、消息占位符参数渲染。

新增可独立链接的纯逻辑时必须同步补充对应断言；涉及 Minecraft 运行时对象（Block、BlockSource、Tessellator）的代码不进入该测试目标。

结构变换通过 useTransform → bridge → TransformBindings/TransformProtocol → structure/TransformActions → StructureSession，在现有控制帧执行并复用 saveSettings。TransformTypes 提供纯数据与范围校验，会话不依赖 OreUI 或队列。请求按字段校验 context 与 expected 原值，避免旧输入覆盖新加载或快捷键；细节与回归要求见 docs/OREUI_MIGRATION.md。
