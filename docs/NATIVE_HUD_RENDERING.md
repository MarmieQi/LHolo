# 原生 HUD

目标环境：Minecraft Bedrock Windows `1.26.51.01`、LeviLamina `26.51.0/client`。普通投影 HUD、材料显示 HUD 与独立写入进度 HUD 使用同一原版渲染入口和游戏字体，菜单内的 HUD 设置继续由 OreUI 提供。2026-10-26 用户确认普通/材料 HUD、图标、间距及多语言留白测试通过；下方清单保留作为后续版本回归范围。

## 接口依据

使用目标版本 SDK 的原生接口声明：

- [ScreenView](https://github.com/LiteLDev/LeviLamina/blob/v26.51.0/src-client/mc/client/gui/screens/ScreenView.h)：`render(UIRenderContext&)` 与 `getScreenName()`。
- [UIRenderContext](https://github.com/LiteLDev/LeviLamina/blob/v26.51.0/src-client/mc/client/gui/controls/UIRenderContext.h)：文字测量、`drawText`、`flushText`、`fillRectangle` 和绘制范围查询。
- [ClientInstance](https://github.com/LiteLDev/LeviLamina/blob/v26.51.0/src-client/mc/client/game/ClientInstance.h)、[FontHandle](https://github.com/LiteLDev/LeviLamina/blob/v26.51.0/src-client/mc/client/gui/FontHandle.h)：获取当前游戏字体；`Font::getWrapHeight` 提供行高基础。
- [TextMeasureData](https://github.com/LiteLDev/LeviLamina/blob/v26.51.0/src-client/mc/client/gui/TextMeasureData.h) 与 `CaretMeasureData`：文本缩放、阴影、对齐及光标参数。

这些是目标版本生成的接口声明，版本更新时须重新核对 ABI 和实机行为。

## 分层

实现集中在 `src/ui/hud/`，命名空间为 `lholo::ui::hud`：

| 文件 | 职责 |
| --- | --- |
| HudContentSource.cpp | 读取会话、设置、投影进度、准心名称、辅助放置模式及材料一致快照；处理菜单和维度暂停可见性 |
| HudContent.h / .cpp | 纯展示模型与本地化文本整理、材料缺口排序；不调用游戏绘制接口 |
| HudLayout.h | 纯布局规则：四角、同角堆叠和可见行数限制 |
| HudText.h | 在 UTF-8 字符边界二分截断，降低长文字的原生测宽次数 |
| NativeHudRenderer.h / .cpp | 普通 HUD 渲染 Hook 启停、上下文与可见性检查，调用共用 Painter |
| NativeHudPainter.h / .cpp | 原生测宽、UTF-8 安全省略、背景与文字绘制、独立透明度/缩放，收集本帧图标和面板几何 |
| NativeHudIcons.h / .cpp | tick 线程准备图标物品、按结构代际缓存、原生物品绘制与状态恢复 |

AppKernel 编排启停、编辑入口/返回设置页和完成后的保存。业务统计仍在 projection / MaterialTracker；渲染线程不查世界、不遍历结构、不扫描背包、不写配置。材料需求和可用数量继续由 `StructureUiState::materialHudSnapshot()` 在同一把锁内复制，避免向量错配。

`ui/MaterialFormat` 为菜单和 HUD 共用的材料名称/组数格式及图标身份规则，独立于 ImGui 和原生渲染 SDK。普通/材料 HUD 的旧 ImGui 绘制函数及叠放缓存已移除；隐藏的旧菜单和动作提示条继续沿用现有实现。正式实现不包含验证文字、计时或屏幕候选日志。

## 显示规则

普通 HUD 沿用文件名、X/Y 层或材料筛选范围、整图/可见范围进度、朝向错误、类型错误、多余方块、准心名称和辅助放置模式。零错误项不显示；各项开关继续生效，材料编号在展示层转为一基。

材料 HUD 独立于普通 HUD 开关。沿用当前可见范围的材料缺口，扣除对应库存，只列出仍缺少的材料；按缺口降序显示，最多 14 项及剩余类型数。数量按每种物品的实际堆叠上限格式化。统计未就绪和材料齐全分别显示原有本地化提示。

四角编码保持原配置：0 左上、1 左下、2 右上、3 右下。同角时普通 HUD 靠角，材料 HUD 向屏幕内侧堆叠；普通 HUD 行间及两 HUD 之间保留旧 ImGui 风格的间隔；缺失材料 HUD 的行间距单独采用原版式 2 个 GUI 单位，内边距、图标大小和图标到文字的距离不变。窗口不足时限制可见行并在末行显示省略标记；长文件名、材料名等按实际字体测宽，在 UTF-8 字符边界省略，不换行覆盖下一行。

普通/材料 HUD 在有已加载结构且主客户端为 Playing、有本地玩家时才显示；LHolo 菜单覆盖或投影因维度切换暂停时隐藏。任务在当前维度没有可见且已加载的文件时，普通与材料 HUD 一起隐藏并重置临时材料页码；复用底部生命周期提示的文件可见性判定，不依赖分层后的方块数量。切回有可见文件的维度后自动恢复，不改 HUD 开关或文件锚点；布局编辑仍提供示例。退出/重进世界和返回投影所在维度后重新读取现有快照，不缓存跨帧世界对象或布局。

## 旧 ImGui 布局风格与原生绘制边界

外观按用户提供的旧 ImGui HUD 图恢复：整块深灰半透明背景、轻微圆角、较宽的内边距和行间距。布局比例直接来自 `ui/FluentTheme.cpp` 与旧 HUD 的 WindowPadding/ItemSpacing：逻辑字体 18、水平内边距 12、垂直内边距 8、行间距 8、圆角 4×0.7；这些比例按当前游戏字体行高转换为原生 GUI 单位，不引入 ImGui 主题或缩放状态。背景 RGB 为 0.075，alpha 默认保留用户选择的 **0.6**，两个 HUD 可在原生编辑器中分别调整。普通文字与缺失材料行均使用默认白色；错误计数及统计中/齐全提示保留状态颜色。

UIRenderContext 没有圆角矩形接口，圆角通过不重叠的水平矩形带近似，避免半透明叠加加深，也不新增纹理、页面或渲染后端。字体继续使用游戏字体（含阴影），不会与截图中的旧 ImGui 字形完全相同。字体缩放为 1，跟随游戏 GUI 坐标空间，不读取旧 ImGui uiScale。


Hook 先执行原版 `ScreenView::render`，只在 `hud_screen` 的渲染调用中追加绘制。字体来自本次调用的 FontHandle，文字追加后立即 `flushText(0, nullopt)`，不额外推进混淆文字时间。不创建页面/JSON 控件，不进入场景栈，不修改输入分发、GUI 缩放或裁剪状态；物品绘制期间通过配对的矩阵栈 push/pop 和 ScopedShaderColor 恢复共享矩阵与颜色状态；字体、ScreenView、渲染上下文均不跨调用保留。

日志前缀 `[native-hud]`：安装时记录启用状态；C++ 绘制异常记录一次并停止本次启用周期的追加绘制。首次提交等验证期日志已清理。

## 材料 HUD 分页

标题左侧使用 MaterialHudTitle 本地化文案，右侧通过 MaterialHudPage 显示“1/4”，复用数量列的右对齐绘制，与下方材料数量共用右边缘；不把标题和页码拼成单段文本。显示项数作为每页项数，默认 5；没有缺失项或等待统计时显示 1/1，并保留原有齐全/计算中提示。ui/hud/MaterialHudPaging 只管理当前页、总页数及上下文，不写入配置。输入层 MaterialHudPageRequest 仅提交翻页方向，readHudContent 在渲染侧消费；材料/投影业务不依赖该页码。新快捷键默认均未绑定，沿用 HotkeyId、原生捕获与 SettingsStore，不在前端监听键盘。

每页按缺口从大到小、相同缺口按原材料索引排序。nth_element 分离前页，仅对当前页 partial_sort，避免每帧排序全部材料。数量减少自动钳制有效页；切换文件、分层或每页项数回到首页；首尾不循环。翻页只重新选择已有快照内容，不查询世界或重复计算背包。菜单/编辑期间不应用翻页请求，退出世界丢弃待处理请求。页码、两个动作标签均覆盖四种语言。

## 缺失材料图标

每条缺失材料行携带独立的可选 `iconId`；标题、统计中/齐全提示不携带图标。`ui/MaterialFormat::materialIconId` 同时供 OreUI 材料清单与 HUD 使用：优先采用真实物品 `itemId`，没有时采用 `typeName`，保留完整命名空间；图标身份不改变库存计数、缺口或放置规则。HUD 不引入 OreUI 页面，不请求 `geo-item://`，不复制贴图包。

原生材料解析保留已解析物品的 aux 附加值和完整物品数据，经 MaterialRequirement → HudLine → HudIconDraw 传入绘制。蓝图有方块实体 NBT 时，每个现有调色板身份只保留一份显示样本，在游戏线程通过 VanillaBlockActorFactory 创建未注册到世界的临时实体并调用原生 load，再交给 BlockType::asItemInstance(block, actor)。BedBlock 和 BannerBlock 均提供该虚函数覆盖；不自行维护染料映射或旗帜图案协议。ItemInstance 通过原生 save(SaveContext{Clone}) / ItemStack::fromTag 转为只读共享 ItemStack，保留原生返回的颜色、图案及其他用户数据，HUD 直接绘制该物品，不重新按裸 ID 构造。样本和物品随原有快照清理，不改变材料分组、库存计数或放置规则。仅开发示例、液体等没有原生物品的回退通路使用原版 `reinit(id, 1, 0)` 在主本地玩家的 tickWorld 回调中准备，缓存和待处理队列仅服务无原生物品的回退通路，以完整物品 ID 标识；正常材料直接使用完整物品，不再保留附加值转发和独立变体缓存两套状态。渲染线程只提交可见图标 ID 请求并读取准备好的只读对象，不访问物品注册表；缓存按结构代际失效，额外 epoch 拒绝清理前尚未完成的结果。缓存最多保留约 64 个身份，超过时整体更新；HUD 无图标、无世界、暂停显示或卸载时清理，不持有跨帧 ClientInstance、LocalPlayer 或渲染上下文。

图标绘制依据目标 SDK 的 [ItemRenderer::renderGuiItemNew](https://github.com/LiteLDev/LeviLamina/blob/v26.51.0/src-client/mc/client/renderer/actor/ItemRenderer.h)、[BaseActorRenderContext](https://github.com/LiteLDev/LeviLamina/blob/v26.51.0/src-client/mc/client/renderer/BaseActorRenderContext.h) 与 [MinecraftUIRenderContext](https://github.com/LiteLDev/LeviLamina/blob/v26.51.0/src-client/mc/client/renderer/screen/MinecraftUIRenderContext.h)。HUD 的同步文字 flush 建立只在本次调用有效的请求范围；匹配的具体 MinecraftUIRenderContext::flushText Hook 先提交文字，再使用该上下文的 ScreenContext 绘制图标，避免将通用 UIRenderContext 强制向下转换。其他文字刷新只调用原版。

图标高度随现有字体行高，名称列增加图标宽度和间隔，参与面板测宽，材料名称完整保留，超出视口宽度时整体缩放；不增加行高。窄窗口放不下图标时优先保留文字。首次等待 tick 准备、未知物品或没有原生库存图标的方块（包括可能无法解析的水/岩浆身份）显示灰色占位，不以水桶替换材料身份。图标 Hook 不可用或 C++ 绘制异常时保留文字 HUD。

日志前缀 `[native-hud.icons]`：只保留 Hook 不可用、图标准备/绘制异常及上下文回调缺失的有界诊断；首次提交和未知身份等验证期日志已清理。用户已确认当前图标显示通过，特殊材质及后续游戏版本仍需实机回归。

## 原生布局编辑（位置、透明度与缩放）

入口保留在 OreUI HUD 设置页，仅世界内可用。app/SettingsActions 传递打开意图，AppKernel 请求原生编辑场景；场景创建、推入/弹出及失去世界后的取消在主 ClientInstance::update 上执行，保存结果交回 AppKernel 的既有保存通路。进入编辑先关闭 OreUI 菜单，等待场景栈完成退出后，在游戏场景上打开独立原生编辑页。编辑完成/取消后等待原生场景真正弹出，再重新打开 OreUI HUD 设置页。排队中的 push/pop 仍属于切换过程，不能把“尚未入栈”当作“场景已被移除”。编辑期间不依赖 OreUI 页面或后方 hud_screen 的渲染回调。

| 文件 | 职责 |
| --- | --- |
| NativeHudPainter.* | 唯一的 HUD 测量、背景、文字与物品图标绘制；游玩和编辑共同调用 |
| NativeHudRenderer.* | 仅接正常游玩 hud_screen 的绘制入口；编辑期间跳过，避免两份 HUD |
| editor/HudEditor.* | 草稿、选择、拖动、点击命中、按钮操作与本地化示例；不依赖 SDK 场景或 OreUI |
| editor/HudEditorScene.* | 创建原生 UIScene，连接 ScreenView 的指针位置和按钮事件，通过原生取消按钮映射处理 Esc，并管理焦点及场景生命周期 |
| editor/HudEditorView.* | 调用共用 Painter，基于本帧实际面板几何绘制选框和按钮，并发布命中区域 |

使用本地 SDK 的 SceneFactory::createUIScene、ScreenController、ScreenView::_handlePointerLocation/handleButtonEvent 和 UIScene::onFocusLost；Esc 经原生全局 button.menu_cancel 映射交给 ScreenController 的按下回调。ScreenView 提供 GUI 坐标，按当前 clientUIScreenSize 归一化后传入纯编辑模型；直接使用该 ScreenView 初始化的左键按钮 ID。重复 Down 不重新开始拖动，失去焦点释放拖动；无需 ShowCursor、WndProc 拖动或独立鼠标接管。既有 OreUI Escape 钩子在编辑期间将按下传给原生场景，并保留按键对归属到松开，避免长按 Esc 在编辑退出后继续关闭下面的菜单。

编辑场景的可见性、输入属性和游戏自己的 cursor_renderer 定义由 `HudEditorScene` 在内存中提供给 `UIDefRepository::findDef`，仅匹配 `lholo.hud_editor`；其他定义查询交回原函数。场景创建失败会报告取消，让应用恢复 HUD 设置页；关闭时待场景离开栈再报告完成。渲染与输入 Hook 仅按 `SceneFactory` 返回的固定编辑场景名识别目标，不跨线程读取可变场景名。HUD、选框和所有按钮仍通过 C++ 原生绘制，HUD 在场景游标之前提交。2026-10-27 用户已在 Minecraft 1.26.51.01 实测无 JSON 注册的编辑场景可打开并操作。资源包仍用于 OreUI 菜单，但不再包含 HUD 编辑器的 JSON UI 文件。

有真实结构时显示实际 HUD 内容；进入编辑会暂时展示两个面板，即使其普通显示开关关闭。没有结构或当前维度暂停时使用 demo.mcstructure 和三种材料示例，材料统计尚未就绪时也用材料示例保持可定位；普通 HUD 全部字段关闭时用示例保证可定位。示例只在 UI 层生成，不写入会话、材料统计或配置。字体、颜色、图标、独立背景 alpha、内边距和行距始终通过同一个 Painter。编辑期间也在客户端更新线程处理已有图标准备队列，避免依赖暂停世界的 LocalPlayer tick。

初始不选中；点击面板选择并拖动，仅选中项显示红色边框，未选中时不画边框，不额外绘制面板名称，选中后才在屏幕底部居中显示单项重置，右侧为“透明度”按钮，点击后在按钮上方展开滑条和白色百分比，再次点击收起，点击空白或切换选中项关闭。滑条浮层与所有操作按钮共用深灰圆角背景和默认 alpha 0.5，悬停 0.6，按下 0.7。点击空白取消选中。顶部居中固定为“全部重置、取消、完成”。重置只改草稿；完成写入偏好并保存，取消/Esc/失去世界或场景被移除丢弃草稿。单项重置会固定另一个面板，全部重置恢复四角预设及自动堆叠。“重置”恢复选中 HUD 的位置、默认不透明度 60% 与缩放 100%；全部重置对两个 HUD 生效。不透明度范围为 0～100%，滑动精度 1%，仅修改选中 HUD，草稿即时预览；完成保存，取消丢弃。配置在 hudLayout.projection/materials 中分别保存可选 backgroundOpacity 整数；旧配置缺少时使用 60，读取时约束到 0～100。缩放范围 50～200%，精度 1%，默认 100%；文字、图标、背景和内边距/行距同步缩放，边缘位置按实际几何约束。缩放与透明度复用同一个浮层，通过按钮切换。可选 scale 整数分别写入 hudLayout.projection/materials，旧配置缺省 100。保持配置版本号。

已移除 OreUI 编辑组件、布局 Facet/状态订阅、浏览器模拟编辑画面及借用后方 UIScene 的临时绘制。浏览器预览仅保留设置页入口，并提示原生编辑需要在游戏内验证。

自动检查覆盖真实指针操作、选择/单项重置、全部重置及取消、拖动边界、松手位置、焦点丢失、原配置兼容和完整资源清单。2026-10-26 用户已确认原生编辑器的位置、透明度、缩放和重置实机测试通过；后续样式变更、游戏 GUI 缩放、世界切换和输入回归仍需按实际改动检查。

## 收尾审计

- 原 ImGui 普通/材料 HUD 函数、声明和跨帧叠放缓存已移除；隐藏旧菜单及动作提示按项目要求保留。
- 业务层没有增加对 UI/OreUI 的依赖；菜单和原生 HUD 通过独立 MaterialFormat 共享展示规则，不通过 MenuWidgets 间接依赖 ImGui。
- 渲染不增加世界/结构/背包扫描。材料默认每页 5 项，先分离前页再只排序当前页（nth_element + partial_sort，相同缺口以原索引稳定排序），不排序未显示的其余材料。
- 超长文字在 UTF-8 边界二分查找，测宽次数从逐字删除降为对数级；一万字符的测试将原生测宽调用限制在 18 次以内。
- 图标准备在处理队列前检查已发布缓存，避免 tick 处理期间渲染重复入队造成二次构造；epoch 检查保留，用于拒绝清理前的异步完成结果。锁、匹配回调作用域及矩阵/颜色恢复均为具体线程和渲染状态要求，不添加未使用的通用防御框架。

## 验证

`xmake` 构建完整 `bin/LHolo`，包含原生编辑场景的两份 UI JSON。`LHoloLogicTests` 中的 `NativeHudTests` 覆盖字段文本、X/Y/材料范围、进度区别、错误颜色、开关独立性、材料缺口与堆叠格式、默认每页 5 项与分页边界、图标/文本排序对应、显示身份回退、命名空间与无图标状态行、四角及同角堆叠、窗口不足和无效尺寸。

部署后由用户自行启动游戏，加载结构并检查：

- 缺失材料的方块、工具等图标与清单一致，缺口排序变化后对应正确；切层、关 HUD、重载和退世界后没有旧图标残留；水/岩浆及未知 ID 的占位不改变文字与数量。
- 普通 HUD 各信息、单项开关、四角和语言切换；材料 HUD 独立开关、当前层变化、背包数量变化、扫描中和齐全提示。
- 两 HUD 选择同角时无重叠；小窗口、F11、游戏 GUI 缩放和长名称显示。
- 快捷栏滚轮/数字键、移动、左右键，以及聊天、背包、暂停和 LHolo 菜单。
- 维度往返、关闭/重载结构、退回主界面后重进世界；暂停维度隐藏，返回后自动恢复。

原生场景通过 SceneFactory::createUIScene 后交由 _createScreen 完成工厂初始化；在自有 UIScene::render 内配对 setup/cleanup 并构造本帧 MinecraftUIRenderContext，调用共用 Painter。该路径已获用户实测成功确认。试验期的请求、入栈、首次渲染和出栈日志已移除，保留实际创建失败的错误信息。

顶部操作按钮与 HUD 共用 paintHudBackground 的颜色和圆角，不单独维护矩形底色。全部按钮默认背景不透明度为 0.5，悬停 0.6，按下 0.7，文字统一使用白色阴影。

## 水图标的显式着色

水和流动水的灰度纹理在 HUD 中由 ItemRenderer::iconBlit 直接绘制：复用原版地形 atlas 与水的 UV，将原生 Water TessellationPolicy 默认水色显式传入颜色参数；原生默认色不可用、为黑或白时使用标准蓝 #3F76E4。仍使用现有图标坐标、尺寸及渲染作用域；物品/atlas 未就绪时保留原来的物品绘制回退。不新增 Hook、跨帧颜色状态或纹理缓存，不修改物品 NBT 和材料计数。岩浆及其他图标继续使用 renderGuiItemNew。iconBlit 直接接收 HUD 实际宽高，xscale/yscale 均为 1，避免套用物品模型的 16 单位缩放导致错位。2026-10-28 用户实机确认水色与位置测试通过。

## 放大图标的深度

三维物品图标的 renderGuiItemNew 使用 ceil(图标实际 GUI 尺寸) 作为 zOrder，替代固定的 0，让深度随 HUD 缩放增长，避免方块前角出现三角形缺口。XY 坐标、模型缩放、相机投影与深度测试保持原样，仍由已有 ScreenState 作用域恢复绘制现场；水图标继续走二维 iconBlit 分支。2026-10-28 用户实机确认放大 HUD 后缺口消失。

## 独立写入进度 HUD

写入 HUD 是第三个原生面板，默认启用、顶部居中，显示写入文件、完成数量/百分比与当前状态，沿用 NativeHudPainter 的字体、行距、文字颜色、圆角和背景。默认定位新增 topCenter（位置编码 4），按测量后的实际面板宽度计算中心，窗口变化和文本长度变化仍保持居中；原投影/材料四角编码及堆叠行为不变。

业务边界为 app/StructureWriteActions 的只读快照：operation 标识实际接受的写入操作，finishedAt 保存单调时钟的真实结束时间，与页面的请求 ack 分离。继续、取消等控制应答不更换操作身份；新写入清除旧反馈，换世界和停用清理身份。ui/hud/HudContentSource 适配该快照，HudContent 负责格式与 2.5 秒结束提示的显示规则，不轮询世界、不参与命令队列、不持有原生指针。结构方块模式为原版原子放置，不伪造逐块进度，只显示文件与真实阶段；指令模式使用已确认或显式跳过的任务进度。状态本地化映射由 structure/write/StructureWriteText 共供 OreUI 与 HUD，不复制错误状态分支。

活动任务始终保留 HUD，等待区块或暂停时保留原因；完成/取消后的 2.5 秒按业务结束时间计算，不因关着菜单、禁用 HUD 或投影暂停而延后启动计时。退出世界隐藏。正常游玩继续只在 hud_screen 绘制，LHolo 菜单遮挡时隐藏，关闭菜单即继续显示。写入面板独立于投影加载、维度暂停和原有两个 HUD 的开关，不会被投影可见性条件误隐藏。

HUD 信息显示增加独立“写入进度 HUD”页签和启用开关，位置、透明度、缩放共用原生布局编辑器。settings/HudPlacement 集中声明面板数量与默认参数，Painter 的几何和编辑器命中数组按同一数量扩展，不创建第二套绘制/编辑场景。编辑器在没有任务时提供同布局的本地化示例；第三个面板的拖动和重置不冻结另外两个面板的位置，原两个同角面板的冻结规则保留。

配置在现有 hud 分组内增量增加 write，保存 enabled、position、scalePercent、backgroundOpacityPercent；缺少该节的旧配置使用顶部居中的默认值，无需修改既有投影/材料偏好。此变更不增加模组版本、JSON 场景、ImGui HUD 或线程。
