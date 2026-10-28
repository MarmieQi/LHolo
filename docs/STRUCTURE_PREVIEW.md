# 文件结构预览

## 当前行为

世界内可预览任务中的任意文件，包括隐藏文件，不要求激活任务。读取原文件完整内容，不应用任务的坐标、旋转、镜像、切片或文件优先级；不改变选中文件、活动投影、材料统计或配置。

预览使用独立原生 UIScene。窗口复用 OreUI 主菜单实测尺寸，背景调用 HUD 原生 Painter；标题为“预览”，右上角关闭，顶部细分隔线，左下角依次显示文件名、XxYxZ 尺寸和非空气占用位置数量，底部显示操作提示。文字为白色，长文件名按 UTF-8 边界省略。实体不参与预览；方块实体复用原版渲染，纯液体写主层、含水方块的液体写附加层。

拖动旋转、滚轮缩放、Esc/关闭返回任务页。光标在模型上为 Grab，拖动为 Grabbing，关闭按钮为 Hand。预览不在栈顶时不覆盖其他界面的光标或滚轮。无世界禁用入口；高度超过当前维度跨度时报告错误，不截断结构。

## 模块与依赖

```text
TasksPage → useStructurePreview → previewBridge
  → oreui/PreviewBindings + PreviewProtocol
  → app/StructurePreviewActions
      → structure/StructureFileLoader → StructureReadQueue → 原文件解析
      → ui/preview/StructurePreviewScene
          → StructurePreviewView / Definition（外观与输入）
          → StructurePreviewRenderer（原生适配、私有源、区块与方块实体）
```

页面只发送文件身份、revision 和窗口实测比例，不发送路径。原生协议限制 JSON 字段、大小、数值及身份长度；应用层重新校验任务 revision 和文件身份，再查真实路径。

app 层协调任务快照、只读读取、原生场景与 OreUI 关闭/返回。ui/preview 不依赖 OreUI、任务目录或 projection 私有状态。structure 不依赖 UI、app 或 OreUI；诊断日志是独立叶模块。

## 后台读取与取消

任务加载和预览读取分别拥有 future、结果及发布状态。任务的 isStructureFileLoading 不包含预览，预览不调用 StructureSession 的 begin/complete、不改任务缓存和 loading hint。

StructureReadQueue 最多持有一个正在执行的读取和一个取消后重新打开的排队请求。取消立即撤销结果发布，保留未完成的 async future；控制帧只在 ready 后 get，不在普通关闭路径等待。新预览等待旧读取安全退出后启动，旧结果不会交给新窗口。失败结果只交付一次，后续请求不继承旧错误。

原生模板/调色板访问仍由解析互斥串行保护，这属于底层资源安全约束，不是共享任务加载状态。正在执行的原生解析不会被强杀；显式发起任务加载时，任务自己的工作可能等待解析互斥。

应用层先消费场景关闭通知并取消请求，再收取结果，避免同帧关闭/完成竞争。发布还要求请求 ID、world revision、场景及 Level 有效。打开失败、关闭、停用均撤销请求。世界销毁/停用取消排队，并等待两个后台通道退出后再释放注册表或 DLL。

## 原生资源与绘制边界

私有区块根源使用 NetworkChunkSource(Dimension&)；禁止 ChunkSource(nullptr)。源没有真实世界父源，边界外不回退世界，仅开放私有区块视图、不启动世界生成任务。

原生 StructureVolumeRenderer 的公开 render wrapper 会重新取得维度主方块源。仅在本线程的 LHolo 自有渲染器作用域、且维度一致时，将这一入口定向到私有源；方块网格与方块实体遍历也限定在同一源。作用域外、其他渲染器和其他线程沿用 origin。关闭原版真实方块监听，退出调用清理临时源指针。

不直接调用 _render 或离屏捕获接口：当前 SDK 的捕获请求声明不完整，不能猜测 optional 参数 ABI。私有符号适配集中在 Renderer，游戏版本升级需重新核对。

区块构建器使用独立 Tessellator。资源释放顺序：原生 renderer/builder → pipeline → tessellator → BlockSource → chunks → 文件引用。原生渲染器经虚拟基类析构，避免生成的派生析构遗漏游戏资源。LevelListener 在世界注册表销毁前清理预览渲染资源。

旋转使用原生相机。缩放只在几何绘制期间调整拟合后的 GUI 空间世界矩阵，同时同步 GameRenderer 快照，作用域退出恢复两者；不缩放 UIControl 或坐标轴。裁切固定在模型区域并恢复调用前状态。异常后停止后续绘制，资源保留至正常场景释放，不在当前批次内立即销毁。

## 验证与维护

隔离回归覆盖协议、静态场景 JSON、指针/关闭/缩放规则、液体层映射，以及真实 async 读取的取消、重开、排队撤销、迟到结果、异常恢复及 drain。前端回归覆盖逐文件入口、请求去重、错误恢复、无世界限制和订阅清理，并运行现有菜单业务回归。

实机回归应覆盖重复打开/关闭、加载中取消、切维度、退出世界、模组停用，以及原投影和任务操作是否保持正常。

同步诊断文件为 mods/LHolo/logs/structure-preview.log，保留阶段与失败信息，不逐普通方块、逐方块实体或每帧写文件。PDB 是可关闭的本地诊断构建选项；必须与 DLL 的 GUID/Age 匹配，六文件正式安装清单不包含 PDB。
