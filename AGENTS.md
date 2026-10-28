# LHolo 开发约束

LHolo 是当前开发项目。同级 OreUIProbe 仅供参考，ore-ui 提供官方基础设施源码参考。
修改前阅读 DEVELOPMENT.md 的相关章节；OreUI 工作另读 docs/OREUI_MIGRATION.md。

- 保持高内聚、低耦合、职责清楚；按实际功能增加模块，不预建通用框架或空实现。
- 菜单与 HUD 设置使用 OreUI，普通/材料 HUD 画面使用原生绘制；旧 ImGui 保留隐藏菜单与动作提示。快捷键与输入行为继续有效。
- 前端在 frontend/；原生页面生命周期、通信和业务绑定集中在 src/oreui/；
  游戏版本私有符号及生成工具集中在 tools/oreui/。
- 页面不访问 engine 或原版私有符号。业务状态使用现有 C++ 模块，前端只保留界面状态。
- 原版 React、Facet 和渲染器复用同一页面的运行时，不重复打包，不额外 createRoot。
- 新界面以 LHolo 旧 ImGui 的自有深色样式为准，可视控件集中在 frontend/src/components/LHoloControls.tsx；原版只提供无外观的焦点/激活原语与运行环境，不套用原版表单、输入框或开关外观。
- structure/projection/place/settings/i18n 不得依赖 OreUI 或其协议。
- HUD 布局编辑使用独立原生 UIScene，入口仍在 OreUI HUD 设置页；状态和交互位于 src/ui/hud/editor/，内容复用原生 HUD Painter，不新增 ImGui 编辑器或独立光标接管。
- 原生 HUD 绘制属于 UI 层，集中在 src/ui/hud/，使用 lholo::ui::hud 命名空间；业务计算保持在原有模块。
- 不复制 Probe 演示数据、测试协议或游戏脚本生成件作为正式实现。
- 每次架构或迁移状态变化同步更新文档。类型检查和隔离测试不等于游戏内验证。
- 文档维护当前架构、行为、约束和维护方法，不新增一次性审计报告或排障流水账。审计结论在对话反馈，临时报告与证据放在忽略版本管理的 build/ 下；长期有效的结论并入现有功能文档。
- 不为此次 UI 迁移顺带修改投影算法、配置格式或模组版本号。

- 当前保留自有菜单样式；命令和菜单快捷键均打开 OreUI，旧 ImGui 菜单不再提供打开入口。游戏启动和实机测试由用户自行进行，不自动操作或结束游戏。
- 浏览保持页面 → 应用状态 → 平台桥接 → 原生桥 → MenuBindings → 独立文件选择服务分层；选择路径不自动加载或写配置。
- 投影动作通过 structure/ProjectionActions 进入现有应用控制帧；前端以原生投影快照为准，不复制加载逻辑。

- 创建结构使用独立 CaptureBindings / CaptureProtocol；capture/CaptureActions 在应用控制帧执行，保存对话框异步，revision 防止旧结果跨上下文导出。

- 浏览器开发预览在 frontend/preview/，复用 src/ 页面和样式；只在预览中提供模拟 engine 和独立 dom-fiber root，正式游戏构建不得引入预览代码或模拟数据。

- UI 迁移逐项对照旧 ImGui 的控件和操作，不擅自增加按钮、确认或应用步骤；创建选区使用自动同步。菜单隐藏滚动条但保留滚动。

- 渲染设置经 app/RenderSettingsActions 协调 projection 参数与 StructureSession 分层，AppKernel 控制帧执行；低层不反向依赖 app/OreUI。分层范围来自原生快照，材料编号仅在 UI 做一基转换，字段批次须检查原值/context/原轴。

- HUD/快捷键/语言/实验性设置通过 app/SettingsActions 的字段/命令通路，不复制完整配置；正式前端不得硬编码原生语言表、快捷键默认值或自行捕获按键。input/HotkeyCapture 共享旧规则，模式是会话态。OreUI 不提供缩放控件，协议不接受 uiScale。

- 文件预览使用独立 ui/preview 原生场景；通过 PreviewBindings/Protocol → app/StructurePreviewActions → StructureFileLoader 只读通道进入，不激活任务或修改选中文件。私有区块及原版渲染适配归预览渲染器所有，不依赖 projection/detail；见 docs/STRUCTURE_PREVIEW.md。原版捕获参数的 SDK 声明不完整，不直接调用 _render 或猜测其 ABI。
- 预览解析使用 StructureReadQueue 独立 future/结果/取消通道，不计入任务 loading、不改任务缓存或投影会话。关闭先撤销结果发布，迟到结果丢弃；重开最多排队一个新请求，旧 async future 留到完成后回收，禁止在 UI 取消路径析构仍运行的 future。仅原生解析互斥及 Level 生命周期屏障与任务加载共享。

- 文件预览保留原先相机与滚轮输入，不重设取景视口。缩放仅在预览几何绘制期间作用于拟合后的 GUI 空间世界矩阵，同时同步 GameRenderer 的模型变换快照，退出作用域恢复两者；不得只修改投影栈。窗口复用主菜单实测尺寸；模型绘制裁切到缩放前的 canvas，作用域退出恢复原裁切状态。界面保持简约：深色背景、文件名、小型文字关闭和弱化提示，不使用厚边框、旋转箭头或额外面板装饰。光标通过原生平台接口，仅在预览位于栈顶时切换 Grab/Grabbing/Hand/Arrow。保留独立区块根源修复。
