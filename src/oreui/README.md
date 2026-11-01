# LHolo OreUI 原生接入

- OreUiScreen（主客户端更新调度、当前场景栈/路由配对）：聊天命令、路由、等待聊天退出后的开屏、ESC 和延迟关闭。
- OreUiBridge：查询请求登记、View update 传输；不持有选中文件、不调用 Win32 对话框。
- MenuBindings：菜单文案、文件草稿、浏览与投影用例绑定、材料清单请求与快照发布、增量文件/投影/材料状态；投影操作提交 structure/ProjectionActions，材料统计复用 structure/MaterialTracker 与 StructureUiState。
- MaterialIconData：在当前 View 客户端的有效 ScopedItemRegistry 作用域内，从只读原生物品提取缩略图 PropertyBag 和视觉属性的 tile 身份，交给版本适配器构造 geo-item 请求；不参与材料计数、库存匹配或投影业务。
- CaptureBindings / CaptureProtocol：创建动作协议校验、导出选择与应答；复用 capture/CaptureActions 和原有捕获器。
- ui/StructureFilePicker：独立异步系统打开/另存为服务；自身不依赖 OreUI 或业务模块。

- structure/ProjectionActions：现有应用控制帧执行意图，复用结构加载器与会话；不依赖 OreUI。

创建动作通过同名 facet 的类型化 payload 传入，native revision 防止旧保存结果跨世界/维度使用。实际捕获在应用控制帧执行，保存对话框在线程自己的 STA 中运行。

详见 ../../docs/OREUI_MIGRATION.md。AppKernel 统一启停，禁用时取消并等待选择器结束。
注册后的路由和查询包含进程级回调，拒绝热卸载 DLL。
保留已确认文件草稿，关闭菜单时取消未完成的选择，避免结果落入重开的页面。

IntentFacet 显式初始化原生基类 EnableNonOwnerReferences 的控制块，覆盖激活失败与 discard/销毁生命周期；不能直接依赖 SDK 生成头文件的隐式默认构造。

- TransformBindings / TransformProtocol：变换 JSON 校验与快照发布，提交独立 structure/TransformActions；会话与队列只共享纯 TransformTypes。控制帧校验加载 context 和单字段原值后修改会话，沿用现有保存和投影刷新。

- RenderSettingsBindings / RenderSettingsProtocol：渲染字段批次校验和快照发布；进入 app/RenderSettingsActions，在 AppKernel 控制帧调用 projection 与 StructureSession。结构层不依赖应用层。

- SettingsBindings / SettingsProtocol：HUD、原生快捷键捕获/重置、动态语言、实验性参数与知情确认；只下发/校验数据，app/SettingsActions 在控制帧编排。缩放没有后端写入字段。
