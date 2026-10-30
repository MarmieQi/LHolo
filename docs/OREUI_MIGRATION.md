# LHolo OreUI 架构与渐进迁移

## 当前状态（2026-10-27）

保留当前自有深色菜单，原版设置页风格试验已回滚。
左侧“建造任务”已接入原生任务目录：新建、重命名、删除、附加外部文件、路径修复、顺序、显隐和选中项均通过 tasks 协议保存到 `data/tasks.json`。世界中选择/新建任务后自动加载，先按文件各自的变换/切片和优先级合成，再交给现有投影管线；结构变换、切片、移动快捷键、HUD/材料及放置查询作用于激活任务的选中文件。任务页使用一个加载/关闭按钮：当前选中任务未激活时显示“加载任务”，已激活或原生正在加载时显示“关闭任务投影”；关闭后可重新加载以读取外部文件，标题界面只管理任务数据。示例数据只存在于 `frontend/preview/tasks.ts`；正式前端不保存任务副本。文件三维预览现使用独立原生场景，仍待实机验收。代码和隔离测试完成，游戏内验收待进行；打开菜单默认显示“建造任务”页，旧“投影”页和它专用的路径浏览、加载、关闭、恢复 query 已移除。材料清单入口移至任务页，统计选中文件并显示文件名。
聊天输入 LHolo 或使用已配置的菜单快捷键（默认 Alt+M）打开 OreUI 主菜单。旧 ImGui 菜单保留代码但隐藏，不再提供打开入口。
任务删除和文件移除按用户要求增加二次确认，复用新建/重命名任务的同一个弹窗。弹窗显示操作对象名称，只有点击“确定”才提交原生意图；取消、点击遮罩或切换页面都不删除数据。确认目标按打开弹窗时的任务/文件 ID 保存。
现行界面通过建造任务管理文件与投影，并提供创建结构、结构变换、渲染设置、HUD、快捷键、语言和实验性功能。
快捷键页不再提供“加载投影”和“关闭投影”绑定；加载与关闭由建造任务页控制。
材料清单弹窗已接入原生 `MaterialTracker` / `StructureUiState` 统计快照并复用原版物品缩略图渲染通路；OreUI 界面不提供缩放控件。
普通 HUD 和材料显示 HUD 的画面由 `src/ui/hud/` 原生绘制，OreUI 只负责原有 HUD 设置；实现边界及验收见 [原生 HUD](NATIVE_HUD_RENDERING.md)。
选择路径仍是菜单草稿，不自动加载或写配置；明确点击加载后复用现有加载器与投影会话，不改变配置格式。

收尾审计见 [OREUI_AUDIT.md](OREUI_AUDIT.md)：包含异步回退修复、官方接口边界、线程亲和性待核验与正式分发限制。

## 主界面入口试验

文件行现提供独立原生结构预览入口，经 PreviewBindings/Protocol → app/StructurePreviewActions → 只读文件加载 → ui/preview。显示原始文件，不改变任务选择、坐标或当前投影；无世界时禁用，关闭后回到任务页。实现与待实机验证边界见 [文件结构预览](STRUCTURE_PREVIEW.md)。

默认菜单快捷键（Alt+M 或用户自定义绑定）可尝试在主界面 `Idle + start_screen` 打开同一菜单，不改写原版主界面布局。登录、加载中和其他无世界页面不开放入口。世界内仍保留原有快捷键/聊天命令。

导航使用主 `ClientInstance::$update` 回调，不依赖主界面是否已有 OreUI View；只处理主客户端，配对使用 getCurrentSceneStack / getCurrentUIRouter，不保存 ClientInstance/Scene 指针。页面记录打开时的客户端状态和场景名称；通过原生 isOnSceneStack / hasScheduledScreens 判断页面归属，覆盖时暂停通信，实际出栈后清理订阅。状态改变时清理桥接；关闭使用原生 schedulePopScreenWithExpectedNames 和 getScheduledPopCount，不重复维护超时兜底或自定义关闭状态机。ESC 和关闭按钮回到原有主界面。

`lholo:projection` 只提供 worldAvailable / loading / hasProjection，供 HUD、实验性功能和材料弹窗读取运行上下文。任务动作使用 tasks 协议；标题界面允许管理文件引用，禁止加载世界投影和启用辅助放置。

## 分层与依赖

```text
TasksPage → application/useTasks → platform/taskBridge
    → OreUiBridge → TaskBindings / TaskProtocol → app/TaskActions
    → tasks/TaskCatalog + TaskStore
    → structure/StructureFileLoader + TaskComposer → StructureSession → projection

CreateStructurePage → useCapture → CaptureBindings / CaptureProtocol → CaptureActions

useProjectionState → platform/bridge → MenuBindings（只读世界/投影上下文）
useMaterials → platform/bridge → MenuBindings → MaterialTracker（当前选中文件）
```

菜单快捷键沿用原有绑定、去重和释放处理，只向 input/MenuOpenRequest 提交无参数意图；AppKernel 控制帧消费并调用 OreUiScreen::requestMenuOpen，路由统一在主 ClientInstance::update 回调中执行。structure/input 不直接依赖 OreUI，菜单内关闭使用 ESC 或关闭按钮。

OreUiScreen 单独负责路由、命令入口、开关屏和 ESC；主客户端更新负责调度，菜单原生状态下发仍在 View update 执行；AppKernel 统一编排启停。
OreUiMenuResources 负责内置资源包的限定 URL 读取适配，只处理 LHolo 自有 HTML/JS，不改变全局包信任判断，不依赖业务模块。
资源加载诊断与页面诊断浮层已在实测成功后移除；只保留正常错误处理、资源版本校验和菜单 ready 初始化协议。
页面不访问 engine 或原版私有符号，业务层不依赖 OreUI。
Gameface 的普通 div 默认按纵向 flex 排列直接子节点。连续文案须先组成一个字符串再交给单个文本节点；需要不同样式时，使用显式横向容器与独立 span。当前选中文件提示由 SelectedFileContext 统一提供。任务错误显式分为两行：第一行“任务错误: 文件名”，第二行错误原因；每行各自保持单个文本节点，避免标点单独成行。
打开与另存为的全局互斥由 StructureFilePicker 服务负责，两个功能绑定不互相引用。
FilePicker 不依赖 Gameface、ImGui、结构会话、投影或模组日志。
MenuBindings 将投影意图提交给 structure/ProjectionActions，不复制 LoadedStructure 或 MenuModel。
ProjectionActions 在现有应用控制帧统一执行加载、关闭和恢复，读取玩家坐标并复用 StructureFileLoader / StructureSession；业务模块不依赖 OreUI 协议。旧菜单和快捷键也复用这一入口。
应用控制帧在 ImGui 初始化之前处理请求，操作执行不依赖旧菜单成功初始化。
不为首个功能预建通用服务框架；后续按选定业务扩展绑定。

## 任务文件与投影操作

- TaskBindings 持有独立异步 StructureFilePicker，使用原生任务/文件 ID 解析当前路径。只接受 .mcstructure / .litematic，取消只完成请求应答，不修改任务或文件。
- 选定路径提交任务动作，原生接受后写入 data/tasks.json；文件保持外部引用，不复制到模组目录。
- 世界中选择或新建任务会加载并合成，加载/关闭共用一个按钮，关闭后再次加载可重新读取外部文件；首次加载期间也可关闭，关闭任务投影使迟到结果失效。已接受的动作不因菜单关闭而取消。
- 每文件变换、切片、显隐和顺序先进入合成，再一次性发布结果；加载失败保留原运行投影并显示具体文件错误。
- 任务字段最多 32768 UTF-8 字节；tasks Facet 和任务协议统一接受最多 262144 字节的 JSON 信封（包含转义开销），其他 Facet 保持 4096 字节。前端发送前校验，超长请求显示本地化错误并释放等待状态，不用超时恢复。任务名称输入在 IME 组合期间不响应提交或取消。
- 成功的运行定义与待合成定义分开，失败后旧投影仍可编辑；任务保存由 `TaskStoreWriter` 合并并异步写入，停用时完成最后一份保存。详见 [建造任务](TASKS_DESIGN.md)。
- 材料入口位于任务页，只有当前查看的是激活任务且已选择文件时可用；复用现有材料弹窗和统计模块。
- 所有异步选择器仍由独立 STA 和共用 FileDialog 服务管理；停用时取消并等待线程退出。

## 创建结构行为

- 只提供现有客户端模式。两点可手动输入完整的 int32 XYZ，或记录玩家脚下坐标；整组留空表示该点尚未设置，半组坐标、分数、越界输入禁止提交。
- 创建页沿用旧 UI 的操作集合：两处“使用玩家当前位置”、包含实体、清除选区、导出 .mcstructure，不增加“应用选区”按钮。坐标文本换页保留，有效坐标和实体开关自动提交给原生会话，原有红色线框随原生选区更新；不完整或越界的文本保留对应点的上一次有效值，禁止导出无效输入。重新开屏从原生会话恢复，不写配置。
- 自动同步同一时刻只有一个请求，连续输入合并到最新草稿；较早的应答不覆盖新文本，应答后继续同步最新内容。自动同步期间允许继续编辑，取位置、清除、导出等操作等待当前请求结束；外部选区/世界变化仍以原生快照为准。
- 显示包含两端点的尺寸与体积；可视按钮、输入框和勾选框由 components/LHoloControls.tsx 自行绘制，旧 ImGui 的 src/ui/FluentTheme.cpp 与 MenuPages.cpp 是外观与排版基准。坐标以 XYZ 与取当前位置按钮紧凑横排。platform/native 只暴露原版焦点/激活原语，版本符号只在 runtime_adapter.js 中解析，不再直接使用 Form.TextField、BaseTextField 或 Switch 的可视组件。
- 记录位置、坐标/实体自动同步、清除与导出先进入 CaptureActions，现有应用控制帧统一执行。捕获调用原有 StructureTemplate / exporter，同步读取世界，不把 BlockSource 移到后台线程。
- 导出使用独立 STA 上的现代 IFileSaveDialog，默认 structure.mcstructure，仅允许 .mcstructure，保留覆盖确认。取消不改选区、不触发导出、不自动载入投影。
- 保存期间禁用编辑和重复提交；关闭菜单取消未完成保存选择。已提交应用控制帧的操作由业务层完成。
- 每个请求携带选区 revision 和请求 id；捕获前再次校验世界及 revision。换世界/维度或旧菜单修改选区后，旧保存结果不能导出到新上下文。
- 当前未加载区块拒绝捕获、实体完整性限制、导出错误沿用原有实现及文案。

## 结构变换接入

依赖方向：TransformPage → application/useTransform → platform/bridge → OreUiBridge → TransformBindings / TransformProtocol → structure/TransformActions → StructureSession。页面只读状态、发动作；JSON 和协议校验在 OreUI，业务队列不依赖 UI，TransformTypes 是纯数据与范围规则。IntentFacet 共用类型化字符串 payload 和安全的原生控制块生命周期。

lholo.transform.intent 接收 {id, context, field, expected, value}；lholo:transform 发布 {values:{rotation,mirror,x,y,z}, context, ack, accepted}。旋转 0～3、镜像 0～2、偏移 int32，id/context 为 JS 安全整数，payload 限制 4096 字节。

会话 context 使用现有加载 revision。控制帧执行时持会话锁校验 context，并对单字段做原值比较交换；旧菜单或快捷键改变同字段时拒绝旧编辑，其余字段不受影响。加载、关闭、恢复、退出世界使旧 context 失效，关闭菜单取消未执行的变换请求。成功变更复用 saveSettings 和恢复记录，不改配置格式或投影算法。

前端串行等待应答、合并连续编辑，旧应答不覆盖新输入；冲突或 context 改变以原生为准，不重放旧意图。浏览器模拟相同协议。回归覆盖类型/范围/重复请求、控制帧、同字段冲突、新加载失效和连续点击；实机由用户验证旋转、镜像、三个偏移、快捷键同步、重开、关闭恢复和换世界。

## 渲染设置接入与架构检查

依赖方向：RenderPage → useRenderSettings → platform/bridge → OreUiBridge → RenderSettingsBindings / RenderSettingsProtocol → app/RenderSettingsActions。应用层分别调用 projection 的公开参数接口和 StructureSession 的分层接口；structure/projection/settings 不依赖 app、OreUI 或页面协议。AppKernel::processControlFrame 是控制帧的编排入口；应用启用时向 Overlay 注册原子函数回调，Overlay 只执行回调、不 include AppKernel，关闭时清理回调。它仍在 ImGui 初始化之前执行，成功修改沿用 structure::saveSettings，不修改配置格式或渲染算法。

- 视觉参数仍在 projection；分层轴、范围、当前索引和 Y/X/材料最大索引由 StructureSession 拥有。LayerSettings 是无 UI/协议依赖的纯数据；会话锁内检查上下文和原分层元组后更新，换轴按真实范围钳制。
- lholo.render.intent 接收 {id,context,axis,changes:[{field,expected,value}]}，lholo:render 下发 {values,maximum:[Y,X,材料],context,ack,accepted}。field 为 bounds/axis/mode/layer/fill/outline/correctionSeeThrough/missingSeeThrough；布尔和整数严格区分，百分比 0～100，轴 0～2、范围 0～3、层为非负 int32，id/context 为 JS 安全整数。拒绝重复字段、空批次、超范围、错误类型和超过 4096 字节的 payload。
- 只提交有改动的字段，整批原值校验通过后才执行，避免整页模型覆盖其它参数。样式恢复为同一批次中的填充 15%、描边 100%；不改变其它开关和分层。分层相关编辑额外验证原轴，防止 Y 层输入落到 X 或材料轴。
- 前端保留未完成输入与待应答编辑，连续编辑合并，较早应答不覆盖新输入。原生旧菜单/快捷键更新回显；context 变化或字段冲突回到原生状态；重开菜单恢复真实设置。材料页面编号一基，原生索引零基，转换只在前端编辑边界。浏览器 preview 仅模拟相同协议。
- 关闭菜单取消未执行请求，退出世界或开始/完成新加载使旧 context 失效。无结构范围为 0；视觉设置仍可编辑，符合旧 UI。

检查范围为本次渲染页与已有结构变换/通信通路，未发现业务层反向依赖 OreUI。整理了上次 TransformProtocol 声明混在 TransformBindings 头文件的问题，拆分协议头；将共用 Hook 的 Capture 命名改为 Intent，并将请求 id 更新移出 React state updater。原先 maxLayer=0 和重开恢复默认值属于前端占位，本次改为原生快照，不把占位状态当成真实配置。

回归：三个轴真实范围、四种显示范围、材料一基编号、样式默认值、两个穿透开关、旧菜单/热键同步、连续编辑/旧应答、批次冲突不产生部分写入、换结构/关菜单丢弃旧请求、重开回显。原生测试替换 projection 参数边界，保留真实动作队列/结构会话/协议；不等于游戏内画面验收。

## HUD、快捷键、语言与实验性设置

普通投影 HUD 的“投影方块”图标目前暂时注释停用，保持纯文字，以下通路保留供后续调整。普通投影 HUD 的“投影方块”行复用材料 HUD 的 HudLine 图标字段、测宽和 NativeHudIcons 绘制。该行按本地化文案的值占位符拆成前缀与名称，顺序为“投影方块：图标 名称”；Painter 分别测量和绘制前缀、图标与名称，材料行仍为“图标 名称 数量”。准星查询带回命中单元的不可变蓝图 BlockEntity NBT；游戏 tick 经 block/BlockPlacementRules 共用的原生拾取解析生成完整 ItemStack，保留床色、旗帜颜色及图案，名称与图标在同一加锁快照中发布。只缓存上一个方块/NBT 组合，切换目标后重新解析；无目标、隐藏该字段、离开世界/维度时清空显示。渲染线程不解析物品或访问可变 BlockActor，文案沿用现有四语言键。

快捷键页按菜单、结构移动、分层显示、材料 HUD 分组，组内使用简短动作名。分组元数据与文案键由 ui/HotkeyDefinitions 经 SettingsBindings 下发，前端按 groupKey 展示并以原生 slot 提交捕获/重置/清除，不能使用组内数组下标。Alt+滚轮开关归结构移动组；四种语言同步组标题与动作名称，默认绑定及业务行为不变。

材料 HUD 分页由原生 UI 层持有，标题文案靠左，当前页/总页数单独放在同一行最右侧，与下方材料数量右边缘对齐。现有显示项数改为每页项数；快捷键设置动态增加上一页/下一页，默认不绑定，捕获、去重、重置与保存共用原有通路，正式前端没有按键监听或页码副本。四种语言同步标题格式与新快捷键名称。首尾页停住，文件/分层/页大小变化回首页，缺失项减少时自动收敛到有效页；不恢复底部剩余材料种类提示。

材料 HUD 页提供“堆叠数量换算”开关，默认关闭。`materialHudStackConversion` 通过现有 Settings 协议与应用控制帧保存，UI 只持有编辑状态。原生 HUD 和布局编辑示例复用 `MaterialFormat` 格式化：关闭显示缺失总数，开启只显示 `43x64+34`（整组省略余数，小于一组只显示数量），按实际堆叠上限计算；不改变材料清单、统计、排序或背包计数。四种语言共用新文案键，旧配置缺少字段时默认为关闭。`materialHudMaxItems` 同通路保存，范围 1～50、默认 5；不再显示剩余材料种类提示。数量和名称分别存入 HudLine，Painter 按原生字体测宽并右对齐数量列，材料名称完整显示，不用空格模拟对齐。面板按每行名称与数量的实际合计宽度自适应，名称与数量间隔为字高的 0.8 倍，超过视口宽度时同比例缩小文字、图标与间距；显示项数使用与现有设置一致的紧凑步进器和正文字号。

依赖方向：页面 → 各功能 use*Settings → application/useSettings → platform/settingsBridge → OreUiBridge → SettingsBindings/SettingsProtocol → app/SettingsActions → StructureUiState / i18n / place。应用层只编排具体字段和命令；settings/SettingsStore 仍只负责原有配置读写，下层不依赖 OreUI 或页面 JSON。单个共享队列防止各页争用同一请求通道，四个页面 hook 仅暴露各自控件所需能力；不把整个配置对象回传后直接覆盖。

- SettingsActions 在 AppKernel 控制帧执行，返回是否需要保存；HUD、材料 HUD、Alt+滚轮、语言、半径/冷却和知情确认沿用现有持久化。三个辅助放置模式是实时会话状态，互斥切换，未授权不能开启，单纯启停不请求保存。模式切换先清除原有模式及手动按压状态。context 沿用加载 revision，旧世界或加载前排队的意图失效。
- settings snapshot 发布 values、hotkeys、languages、consent、context、ack、accepted、hotkeysRevision。字段编辑仅传对应 expected/value；动作包括 set/capture/clearHotkey/resetHotkey/resetHotkeys/consent。快捷键动作携带原生 revision 和受限 slot，拒绝旧快照；取消捕获为独立无参 query，可取消尚未执行的 capture。字段和类型均白名单，拒绝分数、越界、未知字段、无效 locale 和超 4096 字节 payload；协议不接受 uiScale。
- input/HotkeyCapture 抽出旧菜单原有捕获规则，两个入口共用，原生正式前端不自行翻译 KeyboardEvent.key。Overlay 在 OreUI 激活捕获期间只把按键/中键/侧键交给捕获器，其余时候仍由原版场景接收；保留 F11 的完整生命周期，认领的重复按下和释放不泄漏给场景。Esc 取消捕获不会同时关闭菜单。切页 query、关闭菜单、原生上下文销毁清理捕获。HotkeyDefinitions 共用于旧菜单和 OreUI 标签，按键名称仍由 HotkeyFormat 格式化；默认键值只在 StructureUiState 原有表中维护。
- 语言代码和列表来自 i18n 动态注册表，不硬编码四个语言索引。OreUiBridge 观察语言变化主动重发 labels，开屏中旧菜单/后端改变语言也能刷新；名称沿用每种语言自己的 displayName。
- OreUI 界面设置仅提供语言选择，已移除缩放占位控件、说明与局部状态，不影响旧 ImGui 缩放。HUD 通过 StructureUiState 的原子单字段 setter 修改目标字段，不经过包含 uiScale 的整份 applyHud 回写。
- 实验性说明继续使用旧 UI 的原文和确认流程；取消无启用请求，确认后等原生授权/模式应答。后端独立校验授权，模式勾选不能由前端草稿自称成功。半径 1～4、冷却 0～60，原生与前端均验证。

本次检查纠正了前端写死语言表/语言索引、复制快捷键默认值、将 Esc/退格/F11 当普通绑定、恢复默认仅改前端、知情授权和模式互斥只存在于草稿的问题。这些状态现在由原生拥有，正式页面只保留 UI 文本/弹窗；没有新增用户操作。检查范围是这些页面与共用设置/输入通路，不宣称完成全仓库审计。

验证：原生测试覆盖字段与 slot/范围/locale 校验、HUD 不覆盖旧缩放、未授权拒绝、模式互斥且不保存、原生默认与快捷键 revision、捕获重复冲突及 Esc/退格/F11/侧键；前端测试覆盖连续编辑、原生回显、动态语言、新世界丢弃旧模式、取消无启用与界面设置无缩放占位控件。浏览器捕获模拟仅在 preview/，不替代游戏 Win32/Gameface 的输入验证。

## 当前协议

| 方向 | 名称 | 内容 |
| --- | --- | --- |
| JS → C++ | lholo.menu.ready | 请求文案、任务、只读投影上下文、选区、变换及设置快照 |
| JS → C++ | lholo.menu.close | 请求关闭菜单 |
| JS → C++ | lholo.materials.request | 请求当前选中文件在有效合成结果中的材料清单 |
| JS → C++ | lholo.tasks.intent（facet） | id / revision / action / taskId / fileId / value / delta |
| C++ → JS | lholo:tasks | 原生任务目录、选择/激活 ID、revision/ack、加载/选择器状态与错误 |
| C++ → JS | lholo:projection | worldAvailable / loading / hasProjection |
| C++ → JS | lholo:materials | ready:boolean / items:[{displayName, nameKey, typeName, iconId, count}] |
| JS → C++ | facet:discard / facet:request | lholo.capture.intent，同名固定 id；payload.request 为 JSON 字符串：id / revision / action / draft |
| C++ → JS | lholo:capture | draft / revision / ack / worldAvailable / busy / status / error |
| JS → C++ | lholo.transform.intent（facet） | id / context / field / expected / value |
| C++ → JS | lholo:transform | values / context / ack / accepted |
| JS → C++ | lholo.render.intent（facet） | id / context / axis / changes |
| C++ → JS | lholo:render | values / maximum / context / ack / accepted |
| JS → C++ | lholo.settings.intent（facet） | 字段编辑或设置命令，详见上节 |
| JS → C++ | lholo.hotkeys.cancel | 无参，取消当前/排队的按键捕获 |
| C++ → JS | lholo:settings | values / hotkeys / languages / consent / hotkeysRevision / context / ack / accepted |
| C++ → JS | lholo:menu-labels | 现有 i18n 翻译字典 JSON |

带参动作复用原版 facet 激活的类型化 payload（IFacet::init 的标量 map），不读取空壳 ArgumentsBinder。桥接为 tasks/capture/transform/render/settings 的固定意图名注册 IntentFacet，只在当前菜单打开时接收，经各自协议校验进入对应业务队列。每次发送先 discard 同名实例再 request，避免累积实例。带参通道和输入控件仍需实机验收，隔离测试不代表游戏内闭环已验证。

桥接回调只登记请求；View update 的当前 LHolo 页面执行绑定和读取快照。
快照仅首次 ready、忙碌状态变化或结果变化时发送。
worker 不接触 View/Scene/ClientInstance，不跨帧保存游戏对象裸指针。
route/query 保存进程级回调，DLL 注册后拒绝热卸载；更新必须退出游戏。
资源标记升级为 `lholo-menu-v7-settings`，页面和 DLL 必须一起更新。

## 创建页闪退：Facet 生命周期修复（2026-10-25）

最新 05:51:24 转储中的 LHolo 基址是 0x7FFBC04D0000，栈地址 0x7FFBC04F3B30 对应 RVA 0x23B30。对照同版本 DLL 和 OreUiBridge.cpp.obj 反汇编，该处位于 CaptureIntentFacet 删除析构函数调用 Bedrock::EnableNonOwnerReferences 析构之后。转储同时记录写空地址的 0xC0000005。

本地 LeviLamina SDK 的 IFacet 继承 EnableNonOwnerReferences；基类持有 shared_ptr<ControlBlock>。生成头文件没有提供初始化它的构造函数，直接 make_unique 自定义 IFacet 会让控制块为空。原实现没有建立控制块，facet:discard、激活失败清理或页面释放时都会经过基类析构，这是确定的生命周期缺陷。该实现现已共用为 IntentFacet，在构造时创建有效控制块，再允许实例交给原版注册表。

CaptureFacetLifetimeTests 使用生产 Facet 类与 SDK 继承布局，替换独立测试中不可调用的游戏基类析构边界：检查 32 次创建/提交/销毁/重建、观察者失效、所有权释放，以及请求拒绝和缺少 payload 的清理路径。它与 React 模拟测试相互补充，仍不替代实机验收。

此前仅凭动态代码地址和 Cohtml 栈帧就怀疑 BigInt，证据不足；后续实测已否定该归因。动态地址也可能来自原生调用跳板，不能据此判定为 JavaScript 故障。已移除的 BigInt 依赖仅作为兼容性约束保留，不再作为本次崩溃原因。

参考：Mojang ore-ui/README.md 明确开源内容是 React/Facet 基础设施；公开 shared-facet 文档不包含 Bedrock 原生基类构造。具体继承和控制块布局来自本地 LeviLamina src/mc/deps/core/utility/EnableNonOwnerReferences.h 及 src-client/mc/client/gui/oreui/interface/IFacet.h。通用绑定生命周期见 [Gameface 官方 C++ 文档](https://docs.coherent-labs.com/cpp-gameface/integration/ui_scripting/javascript_native/)。

## 控件与旧 UI 对齐约束

迁移以旧 ImGui 的 MenuPages.cpp / MenuController.cpp 为行为依据，原有按钮、选项与操作集合保持一致；不得擅自新增确认、应用等步骤。创建页不显示“应用选区”按钮，协议内部 apply 仅用于自动同步，不是额外的用户操作。

菜单滚动条在浏览器预览和 Gameface 中均隐藏，仍支持滚轮、触摸及原版焦点滚动。浏览器只隐藏原生滚动条，不设置 overflow:hidden；Gameface 使用原版 ScrollView 的 floating 和 scrollbarClassName 参数，仅让滚动条不可见且不接收指针，保留其测量和滚动机制，不禁用滚动。

## 前端与资源

React/Facet 和原版输入、滚动、音效复用同一页面运行时，不另建 React root。
视觉统一参照 LHolo 当前旧 ImGui：中性深色按钮/输入框、轻圆角、蓝色状态标记、紧凑横排和普通字体，不继续原版设置页外观试验。
components/LHoloControls 自行定义控件子树和样式；runtime_adapter 的 Interactive 只转接原版无外观焦点/激活与音效。文本输入通过 ref + effect 显式绑定 HTML input 的真实 input 事件（Mojang dom-fiber 不自动转发 JSX onInput，不依赖 React DOM 的合成 onChange），支持受控值、禁用、长度限制与 Enter 结束编辑。输入框、勾选框和模式展示不再带入原版可视表单。
游戏内键盘输入、焦点及排版仍需实机验收。
私有符号只允许在 tools/oreui/runtime_adapter.js 中出现；生成器校验原版 bundle SHA256。
生成的游戏脚本只留在忽略的 build/，不覆盖游戏原版 gameplay/index。
原版主题、字体与图集继续从游戏资源引用。

## 不启动游戏的前端预览

运行 `npm.cmd --prefix frontend run preview`，访问 http://127.0.0.1:4173/。开发入口 frontend/preview/ 使用 Mojang dom-fiber 渲染器和本地模拟 engine，复用正式 App、hooks、可视控件与 CSS，源码变化自动刷新。工具栏提供已加载任务、选区和无世界等模拟状态。浏览器使用系统字体，不需要游戏安装或复制原版资产；浏览只取文件名，导出只显示模拟结果。

正式入口不会引入 preview/；build.mjs 同时检查预览代码和额外 React/Facet 运行时泄漏。开发 preview 独立创建 root，游戏页仍只复用原版 root。浏览器结果仅用于外观与交互开发，不证明原生桥、结构捕获或 Gameface 生命周期安全。详细命令见 frontend/README.md。

## 构建、部署与验证

```powershell
# 日常构建：完整目录输出到 bin/LHolo，不生成 ZIP
xmake
```

首次资源路径配置见 [打包说明](OREUI_PACKAGING.md)。可直接复制 bin/LHolo 到游戏 mods；可选的 `tools/oreui/deploy.ps1` 要求游戏退出后执行。前端测试使用 `npm.cmd --prefix frontend test`，原生逻辑测试使用 `xmake -b LHoloLogicTests` 与 `xmake r LHoloLogicTests`，属于开发验证而非打包前置手工步骤。

游戏由用户自行启动和测试。需验证浏览、中文路径、取消、加载成功与失败、加载中关闭、关闭后恢复原锚点/变换、菜单重开及返回游戏后的输入。创建页另测两点记录、负坐标输入与线框自动更新、清除、包含实体、导出及取消、换维度后旧保存失效，以及导出文件重新加载。
自动测试覆盖前端协议/订阅、路径回显/取消、加载/关闭/恢复协议、重复提交与原生快照同步，以及异步服务的取消、拒绝并发、结果消费和扩展名校验。
创建动作测试使用假世界边界，覆盖协议校验、重复请求、旧 revision 和无世界时拒绝导出；不替代原版结构捕获实测。
原生服务测试替换系统对话框回调，不宣称验证实际 Windows 对话框或游戏内排版。

## 后续迁移

材料清单已接入，OreUI 缩放占位已移除；继续实机回归各功能。
默认 xmake 已生成完整本机安装目录；当前运行时生成仍依赖指定版本游戏资源，面向其他用户的自动资源准备尚未实现。
正式分发目标使用 LeviLamina 内置 `mods/LHolo/resource_packs` 约定；规格、更新策略和待验证边界见 [OreUI 发布与更新方案](OREUI_PACKAGING.md)。资源包挂载及原生资源栈读取已实测确认；原版 Hybrid HTML 入口因包信任限制读取失败，已添加只处理 LHolo HTML/JS 的资源适配，用户已实测页面启动成功。必需的生成页面随完整安装包分发，发行 CI 校验已上传的 ZIP 与 SHA256；本地与 GitHub 使用同一套仓库内版本化构建依赖生成资源，不要求构建机安装游戏。
已存在的 HUD、提示和快捷键维持原路径，菜单迁移后再按实际用途清理旧 UI。
旧原版风格试验的回滚脚本已移除，避免将历史页面与当前 DLL 混用。

文件选择器实现依据：[Common Item Dialog](https://learn.microsoft.com/en-us/windows/win32/shell/common-file-dialog)、[IFileDialog::Close](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialog-close)。

物品图标使用独立 LHoloItemIcon 与运行时 ItemIcon 适配，版本私有组件只放在 tools/oreui/runtime_adapter.js；显示身份通过独立 iconId 传递，不改变材料背包计数语义；命名空间转换仅在版本适配器执行。原生列表按版本读取一致快照，未变化时不复制和序列化。浏览器仅显示占位，详情见 frontend/README.md。

官方约定核对：本地 ore-ui 仅公开 React/Facet 等基础设施，其 useFacetEffect 文档要求在依赖变化和卸载时清理订阅/副作用；本项复用现有页面订阅清理，不引入第二个渲染 root。原生场景栈与路由使用 LeviLamina SDK 暴露的接口，属于游戏版本适配，不宣称是 ore-ui 提供的稳定路由 API。

HUD 布局编辑已迁移到 src/ui/hud/editor 的独立原生 UIScene。OreUI 只保留设置页的打开命令；不再有编辑页面、拖动 Facet、布局状态订阅或浏览器模拟编辑。先关闭 OreUI，再由独立原生场景处理鼠标和绘制，完成/取消并移除原生场景后重开 HUD 页；HUD 内容、边框与按钮均原生绘制，详见 NATIVE_HUD_RENDERING.md。HUD 设置页在前端分为投影 HUD 和材料 HUD 两个标签；投影选项按基本信息、进度、错误统计分组，材料 HUD 保持独立开关。标签仅为页面状态，仍通过现有 settings 字段通路修改设置；布局编辑入口位于页标题旁，继续使用原生场景。

## 写入世界

成功/失败提示为页面局部的请求回执，离开页面、关闭菜单或切换任务/文件/路径/世界即清除；重新进入不展示原生保留的旧结果，旧请求迟到完成也不恢复提示。原生操作及 busy 状态继续独立存在，切页不取消已接受写入，不允许重复提交。

导航入口位于“创建结构”下方，默认单人存档模式，服务器指令模式不可选。页面通过 lholo.write.intent 提交 {id, revision, worldRevision, taskId, fileId, mode:"local", skipAir:boolean}，由 StructureWriteProtocol/Bindings 接入 app/StructureWriteActions；lholo:write 发布 ack、worldRevision、busy、localAvailable 和本地化结果键。前端不提交世界坐标、结构内容或独立默认配置。原生校验当前激活任务、当前选中文件、显隐、维度及上下文，再固定缓存原文件与完整变换；分层仅影响显示。已接受的写入不因菜单关闭取消，停用与世界上下文变化撤销尚未开始的操作；原版放置开始后不提供回滚。

单人写入实现集中在 structure/write/local/，计划为独立 NBT 字节与数据，不跨线程持有客户端世界或 Block 指针。LocalWorldWriter 订阅 LeviLamina ServerLevelTickEvent，在本地服务端 tick 中重新检查维度、世界 revision、目标高度和已加载区块，复用 StructureTemplate::load / getTransformedBounds / placeInWorld。LocalWriteQueue 分开结果邮箱锁与原生执行门：控制帧轮询不等待原版放置，只有取消/停用等待正在执行的调用，并清除其迟到结果；停用先关闭 OreUI 请求入口，再撤销写入和监听。模板尺寸按 format-1 NBT 列表的 int32 长度上限检查，先验证预期目标高度和区块，再分配稠密索引；取得原版变换边界后仍校验实际范围。保存交由游戏正常存档流程；完成状态仅表示原版放置调用返回，不表示已强制刷盘或逐方块验证。原版调用仍同步占用服务端 tick，且控制帧生成独立写入计划有开销，大结构可能导致卡顿；解除邮箱锁等待并不等于实现分批放置。

StructureWritePlan 保留区域、空气、源格式 -1 省略单元、两层方块索引及方块实体 NBT；Litematic 区域间空隙不清空，默认结构覆盖范围内空气会清空旧方块；开启“跳过空气方块”后，空气单元保留 -1 索引，不主动清空对应位置，固体、纯液体和含水副层仍按原数据放置。共享数据不包含模板或指令实现；未来指令写入在 command/ 新增实现，目前不创建空模块。类型检查、隔离测试和编译不能替代单人存档实机验收，特别是旋转镜像、含水、箱子/告示牌、邻接更新和退出重进保存。

“跳过空气方块”复用 LHoloCheckbox，默认关闭，只属于当前页面实例；离页、关闭菜单或目标上下文重建后恢复关闭，不写入配置或 tasks.json。布尔值随单次请求进入 StructureWriteIntent，再固定到 StructureWritePlan，执行期间不可编辑；仅改变空气覆盖行为，不按显示分层裁剪。说明文字随开关变化，沿用统一字色及无句末句号规则。
