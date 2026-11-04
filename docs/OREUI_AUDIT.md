# OreUI 迁移收尾审计（2026-10-25）

本文保留 26.51 基线的历史审查依据；当前 26.60 适配状态、运行时哈希和接口维护见 [迁移说明](OREUI_MIGRATION.md) 与 `tools/oreui/runtime/source.json`。文中的旧版地址与哈希不能作为当前版本适配参数。

范围：审查时工作区的 OreUI 页面、应用 hooks、桥接/协议、设置与投影动作队列、文件选择服务、输入捕获、构建和部署边界。核对旧 ImGui 行为和当前目标版本 SDK。没有对投影网格算法、所有游戏 Hook 或全仓库并发行为作完整形式验证。

用户已反馈部署版本的功能实测通过；以下区分代码审查、隔离测试与仍需专项验证的事项。

## 结论

依赖方向和职责基本合理，可以继续按功能逐步迁移。业务模块没有反向依赖 OreUI；前端页面没有越过 platform 调用 engine；缩放仍是明确的本地占位，协议不存在 uiScale 写入入口。本轮找到并修复两个相关的前端同步缺陷，不能把此前实测通过等同于所有异步交错均正确。

当前接入并非完全建立在公开、跨版本稳定的官方插件 API 上：官方基础设施与游戏版本私有适配必须分别维护。正式包包含 DLL、清单与必需的 OreUI 资源，发行 CI 使用独立校验器复核完整附件。

## 官方依据与私有适配的边界

| 部分 | 核对依据 | 审计结果 |
| --- | --- | --- |
| React/Facet 与渲染环境 | [Mojang ore-ui](https://github.com/Mojang/ore-ui)、本地 packages/@react-facet/dom-fiber/src/setupHostConfig.ts | 复用游戏实例，不重复打包 React/Facet，不另建游戏 root。官方仓库明确只开放部分基础设施，不能据此宣称 Bedrock 路由与控件符号是公开 API。 |
| 输入事件 | 本地 Mojang dom-fiber 源码 | onFocus/onBlur/onKeyDown 有显式事件转接；没有等价的 JSX onInput 转接。LHoloTextField 通过 ref/effect 绑定真实 input 并清理，符合该渲染器实际能力。 |
| JS/C++ 通信 | [Gameface 通信文档](https://docs.coherent-labs.com/cpp-gameface/integration/ui_scripting/javascript_native/)、目标 SDK IFacet/FacetRegistry/QueryRegistry | 使用 engine 事件、请求/应答和原版注册机制；Bedrock query/facet 的具体注册 Hook 是版本适配，不是 Gameface 文档直接保证的插件接口。ready 握手后下发状态，不从 worker 调用 View。 |
| Facet 生命周期 | SDK IFacet 与 EnableNonOwnerReferences、既有转储和生命周期测试 | 自建 Facet 明确初始化原生控制块，discard/失败路径覆盖；进程级注册回调仍存在，DLL 拒绝热卸载。生成头文件不是完整官方构造契约，此部分继续纳入版本审核。 |
| Windows 对话框 | [Microsoft Common Item Dialog](https://learn.microsoft.com/en-us/windows/win32/shell/common-file-dialog) | 新异步浏览/保存使用 IFileOpenDialog/IFileSaveDialog、STA COM、RAII 和同线程取消；worker 不持有世界/Scene/View。旧 ImGui 对话框按渐进迁移约束保留。 |
| 私有 JS 符号 | tools/oreui/runtime_adapter.js、build_menu.py、安装版本原版 bundle | 符号集中适配，SHA256 不匹配拒绝生成；核实当前 hash 为 abf73f19e4d7c952d0de4ec366a0669b241560f1af319f3e0c9cbbb23142e5cc。 |

该历史审查的本地构建固定 LeviLamina 26.51.0/client，目标 Minecraft 1.26.51.01；其它版本须重新核对 ABI、Hook、资源和私有符号。

## 本轮修复

1. **P2：旧请求未完成时，“改回原值”可能丢失。** 例如 X 从 23 改到 24，立即改回 23；收到 ack 仍旧的原生快照时，原逻辑把等于原生值的 23 当成干净字段删除。随后 24 应答导致最终停在 24。已经用正式菜单隔离测试复现失败，并修复结构变换、渲染和共用设置三个调度器。
2. **P2：无变化输入可能留下旧草稿。** 原逻辑将与原生相同的数值保留为 dirty，后续原生快捷键/旧菜单更新该字段时，旧草稿可能反向覆盖新值。现在在本地编辑时清理真正无变化的字段，同时保护仍有请求在途的字段。

复用一个纯函数 application/fieldDraft.ts 管理上述清理规则；各功能仍保留独立的协议和编辑行为，没有引入通用服务框架。新增回归覆盖中间快照、快速反向编辑、数值/开关回退及无变化输入后原生更新。

## 架构与逻辑检查

- 页面只接收对应功能 controls，engine/JSON 在 platform；未将预览默认值、模拟捕获或独立 root 打入游戏 bundle。
- AppKernel 编排控制帧；Overlay 执行注入回调，不反向 include AppKernel。设置用例分别调用 StructureUiState、StructureSession、i18n 和 place；持久化仍由原有 SettingsStore 路径完成。
- 原生字段白名单与严格类型/范围验证；id/context/revision 限制在 JS 安全整数，payload 有长度上限。旧会话、同字段冲突与旧快捷键 revision 会被拒绝；不直接覆盖整份配置。
- HUD 原子单字段 setter 不回写 uiScale；语言以稳定 code 保存，列表和按键默认值由原生拥有，字典随原生语言变化刷新。
- 实验性确认由后端校验，模式互斥且会话化；只有知情确认和参数等持久偏好触发保存。取消说明弹窗不发送启用请求。
- 原生捕获与旧菜单共用规则，认领重复按下与释放，保留 F11；切页/关屏取消捕获。该检查不等同于所有键盘布局/输入法/手柄的实测。
- 文件选择线程只处理对话框与路径；业务动作不在选文件 worker 上运行。取消和 shutdown 不持有结果锁等待 worker。
- 材料弹窗与 OreUI 缩放保持已声明的未接入范围，没有借审计迁移额外功能或改变配置格式/模组版本。

## 尚未闭合的审计项

1. **游戏线程归属需专项确认。** 目前控制帧沿用旧 UI 的 DXGI Present 路径；从调用位置不能证明它与客户端 tick 同线程。选区快照也会经 currentContext 读取客户端对象。LeviLamina 提供 ClientThreadExecutor，其本地源码由 ClientLevelTickEvent 驱动。这是后续线程亲和性核验的参考，不能用一把业务 mutex 证明游戏 API 在任意线程安全。本轮未将已通过实机的控制链路直接改成 tick 调度，因为暂停/菜单期间 tick 语义和世界退出清理也必须一起验证。
2. **完整包分发与版本约束。** 2026-10-26 已明确必需页面随模组发布；`xmake` 生成完整目录，发布归档校验文件清单、版本与哈希，CI 复核发行附件。目标游戏构建输入已归档到工具目录并锁定 SHA256，本地和 GitHub 调用同一完整 xmake 流程；多用户干净安装、升级和缓存刷新属于后续实机回归范围，不将打包校验等同于所有环境验收通过。
3. **升级维护仍存在。** SDK/路由/Facet ABI 与游戏 JS 私有符号有版本耦合，当前 hash 门禁只能阻止未经审核的资源组合，不能自动适配升级。后续应保留精确版本审核和用户实机回归。

## 验证与交付

本轮运行正式前端类型检查/打包/菜单测试、浏览器预览构建，以及原生逻辑测试。原生生命周期和游戏边界部分使用独立测试替身，不能替代游戏内多线程/热切世界验证。用户此前实测反馈适用于修复前的部署版本；本轮只修改前端同步规则，不要求据此重新验证未改动的投影算法，但快速编辑回退应列入下一次实机检查。

本报告随 DEVELOPMENT.md 和 OREUI_MIGRATION.md 保存，后续线程与正式分发完成时应更新本节，而不是宣称所有审计项已经关闭。
