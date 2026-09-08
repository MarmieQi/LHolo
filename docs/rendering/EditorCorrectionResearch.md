# 编辑器纠错渲染研究与重写准入

日期：2026-09-08。

目标：Minecraft Bedrock Windows 1.26.32、LeviLamina 26.32.0 client。

状态：研究结论已汇总；尚未满足替换生产纠错渲染的条件。本文件不是已实现功能或游戏内验证报告。

## 1. 范围与证据约定

研究对象是纠错填充和描边。保留蓝色未放置、红色类型错误、黄色状态错误、品红色多余方块、正确位置隐藏，以及独立透明度、分层、镜像旋转、遮挡设置和世界生命周期行为。

Windows `Minecraft.Windows.exe` 是 ABI 和实现的最终依据；带符号的 `minecraft-edu` 用于识别共享设施。跨平台符号对应不等于结构布局相同。

本文地址是本次目标二进制中的 VA，只供定位证据，不得直接作为插件固定调用地址。已有研究中的结论分为：已读取的目标代码事实、跨版本语义对应、SDK 声明线索、待验证假设。没有进行游戏内绘制验证。

后续 IDA 研究保持只读，不重分析、不改类型、不重命名、不保存数据库。

## 2. 已确认的调用与数据事实

### 2.1 编辑器插入入口与虚表

- 对象使用的虚表起点为 `0x14DE6B3D0`。
- 构造代码 `0x146B5962E` 将该地址写入对象首字段；析构代码 `0x146CA8370` 同样写回该地址。
- 槽地址 `0x14DE6B708` 的内容是 `sub_146BCE880`，相对上述虚表为 `+0x338`。
- `0x14DE6B390` 是附近其他函数表的起点；相对它的 `+0x378` 不能用于该对象的虚调用。
- `sub_146BCE880` 有 `FrameBuilder - Insert editor` 字符串，组装上下文后调用 `sub_146BCEB40`。
- `0x14E4A98C4` 的 DWORD 为 `0x6BCE880`，已定位的 `__guard_fids_table` 起点是 `0x14E43DDE0`。该 RVA 引用不可当成运行时对象虚表。

`EditorDescription` 在研究中是编辑器 variant 的简称，不能据此声称恢复了完整原始 C++ 类型声明。

### 2.2 编辑器主函数的职责

`sub_146BCEB40` 是多个编辑器描述的分派和绘制数据构建函数，不是专门的体积外壳渲染器。

下表是当前 SDK 的 variant 顺序与目标七个分支的候选对应；具体描述类型名来自 SDK，不是 Windows 中恢复出的类型声明。应继续从生成端确认每个序号，不能仅凭顺序完成 ABI 定义：

| 分支 | 描述 | 后续研究用途 |
| --- | --- | --- |
| 0 | RenderEditorBlockVolumeBoundingBoxDescription | 进入 `sub_146BFFDA0`，优先追生成端 |
| 1 | RenderEditorGizmoHandleArrowDescription | Gizmo，不应误当体积生成器 |
| 2 | RenderEditorGizmoHandleCubeDescription | Gizmo |
| 3 | RenderEditorGizmoHandlePaneDescription | Gizmo |
| 4 | RenderEditorSelectionCursorDescription | 选择光标 |
| 5 | RenderEditorBlockVolumeOutlineDescription | 优先追生成端 |
| 6 | RenderEditorRenderPlaneDescription | 编辑平面 |

`EditorBlockVolumeHull` 被多个分支共用。材质名相同不证明 CPU 几何格式或描述类型相同。特别是目标 case 5 同时获取 `EditorSelectionCursor` 与 `EditorBlockVolumeWireframe`；不能将整个分支简单等同于一次线框绘制。

### 2.3 材质资源获取

`sub_146CB3030` 可观察到：

- 六个机器级参数；不能将反编译器推导的参数类型直接视为源代码 ABI 声明。
- 对 `a1+0xD8` 加锁并访问哈希容器。
- 键涉及 `a3+0x60` 与 `a3+0x68`，包含 FNV 运算和哈希组合，不是仅对单个字段做 FNV。
- 通过输出地址写入带引用计数操作的资源指针对。

完整私有 C++ 类名仍未恢复。不能把此函数当作与旧 `RenderMaterialGroup` 完全等价的公开接口。

### 2.4 MeshFilter：纠正之前的材质事务归类

Windows `sub_146C05800` 与教育版 `0x10B45EB20` 的数据流相符。教育版符号为：

```cpp
mce::framebuilder::bgfxbridge::meshutils::makeMeshFilter(
    dragon::frameobject::IntraFrameAllocatorContainer&,
    dragon::mesh::Mesh const&
);
```

教育版在 `0x10B45EC05` 调用带符号的 `MeshFilter::addBuffer`（`0x10B720A70`），参数涉及顶点缓冲资源 variant、偏移和大小。Windows 对应下游为 `sub_14B057530`。

两端都从输入 Mesh 的 `+0x28/+0x30` 遍历资源数组，并读取 `+0x48/+0x50` 的资源指针。Windows 中需要区分：

| 数据 | 大小或步长 |
| --- | --- |
| 共享指针对 | 16 字节 |
| 输入资源数组元素 | 24 字节 |
| 输出顶点缓冲绑定元素 | 40 字节 |

它是几何缓冲绑定构建，不是颜色 setter，也不能归类为 `CreateMaterialTransaction` 构建函数。以上大小均不是 GPU 顶点步长。两个平台输出字段偏移存在差异。

Windows `sub_146B82E50` 遍历顶点缓冲资源，检查索引资源，并要求顶点缓冲数组非空；教育版存在 `dragon::mesh::Mesh::areBuffersValid()` 符号（`0x10B43E160`），可继续用于交叉核对。

### 2.5 MatColor 写入及复制时机

主函数存在 `MatColor`、`CameraDirection` 名称哈希查找与 `sub_1451364A0` 调用。

`sub_1451364A0` 通过内部槽索引取得缓冲偏移，执行：

```cpp
memcpy(destination, source, 16 * count);
```

随后设置修改标志并更新哈希。因此本次传入的颜色数据在调用期间被复制。它仍依赖内部参数索引表和缓冲对象，不是独立可用的 `setMatColor` 接口。

这不证明整份绘制数据或其他资源可以立即销毁，也不证明跨线程、跨帧所有权。

### 2.6 shader 与渲染任务

本机 `EditorBlockVolumeHull.material.bin`、`EditorBlockVolumeWireframe.material.bin` 此前成功反汇编的 DXBC 变体显示：

- Hull 像素着色器直接输出颜色常量，不读取顶点色、不采样纹理。
- Wireframe 从颜色常量取色，另使用 UV 与时间参数执行断续线裁剪。
- 材质中分别存在 `CameraDirection/MatColor` 与 `FrameTime/MatColor`。

这些事实仅覆盖成功解析的变体，不证明当前 D3D12 路径选择了该变体，也不证明完整 blend/depth/cull 状态。

目标代码存在：

```text
sub_146BCE880 -> sub_146BCEB40
sub_146BCEB40 -> 材质资源、uniform、MeshFilter 和绘制数据组合
其中一条组合路径 -> sub_146D1F4E0

sub_146B6F430：存在 End frame wait for render 标记
sub_14B02B9F0 -> sub_14B0238E0：渲染任务执行
```

这些是已知局部关系。不得串成“makeMeshFilter 直接调用 endFrame 再执行渲染”的调用链。帧容器和任务生产/消费关联尚未补齐；任务字符串不证明特定 OS 线程或独立 render-worker。

## 3. 尚不能作为实现依据的结论

1. “Hull 最终 GPU 顶点步长为 16 字节”：缺生成函数、上传、VertexLayout 与 shader 变体的完整对应。CPU 记录不能直接当作 GPU 输入。
2. “使用 Hull 材质就能保留顶点色”：与已解析像素着色器事实不符；需要按 uniform 颜色拆批。
3. “currentShaderColor 自动设置 MatColor”：教育版 fallback 路径的 `CurrentColor` 不等于编辑器 `MatColor`。
4. “debug 材质使用某个消费顶点色的 shader”：缺材质实例到 shader 的关联；单独发现 shader 不能证明。
5. “debug 运行时指针为空已证实”：当前代码有空指针提前返回，不等于已经观察到运行时取值。
6. “深度偏移 100/15 或输出 alpha 可以保证外壳效果”：缺实际状态和绘制验证。
7. “空结构 SDK 可以直接构造描述”：两个编辑器体积描述及 MeshDescription 等为空占位，不能用其 sizeof 推断游戏对象。

## 3A. 体积子路径的新增证据

### 3A.1 研究实例身份

后续读取确认当前教育版输入路径已变为 `D:\Downloads\edu2632x64\minecraft-edu`，Image base 仍为 `0x100000000`，处理器为 `metapc`。Windows 仍为 `D:\Downloads\pc2632\Minecraft.Windows.exe`，Image base 为 `0x140000000`。

教育版保留了下述符号和机器码，但部分符号地址没有已定义的 IDA 函数，反编译返回空。为遵守只读约束，使用 `decode_insn` 读取现有机器码，没有创建函数或触发分析。不得把“有名称但没有函数记录”解释成实现不存在。

### 3A.2 sub_146BFFDA0 描述字段消费表

这是目标主函数 case 0 的直接被调用函数。以下偏移相对其第二参数，只描述已观察到的消费行为，不是可直接编译的完整结构体：

| 偏移（十进制） | 已观察行为 | 结论边界 |
| --- | --- | --- |
| +0、+12 | 各读取三个 float，参与两组坐标相减、矩阵构造 | 坐标空间及生成来源仍需确认 |
| +24 | 读取三个 DWORD，补 float 1 后写 CameraDirection | 原始方向数据来源待确认 |
| +36 | 读取 BYTE，其值加 1 后进入绘制描述字段 | 不能直接命名为深度测试或图元枚举 |
| +40 | DWORD 复制到四个通道后写 FrameTime | 原始时间单位及更新线程待确认 |
| +48 / +136 | Mesh 输入 / BYTE 有效检查；使用 Wireframe | 是一组可选几何，不能仅据布局宣布 std::optional ABI |
| +144 / +232 | Mesh 输入 / BYTE 有效检查；使用 Hull | 独立于两组线框几何 |
| +240 / +328 | Mesh 输入 / BYTE 有效检查；使用 Wireframe | 第二组可选线框几何 |
| +336 | 第二组线框颜色四个分量 | 常规提交直接复制；附加分支改 alpha |
| +352 | 第一组线框颜色四个分量 | 常规提交直接复制；附加分支改 alpha |
| +368 / +380 | Hull RGB / alpha 来源 | 写 MatColor 时 alpha 乘以 0.9 |
| +384 | WORD 进入提交参数 | 类型与枚举含义未定 |
| +388 | int 乘以约 0.0001，沿两坐标差的归一化方向调整；也进入其他提交字段 | 不应直接命名为 rasterizer depth bias |
| +392 | BYTE 等于 1 时，两组线框会执行附加提交分支 | 该分支 MatColor alpha 为 0.1；未证明就是 X-ray |

颜色写入在同一个 `sub_146BFFDA0` 中可见：

```cpp
// 第一组线框的常规提交
MatColor = four_components_at(description + 352);

// +392 开启的附加提交：同组 RGB 保留，alpha 改为 0.1
MatColor = {rgb_at(description + 352), 0.1f};

// 第二组线框对应 description + 336，同样存在上述两种提交。

// Hull
MatColor = {rgb_at(description + 368), alpha_at(description + 380) * 0.9f};
```

这段伪代码仅概括已观察的参数数据流，不表达完整控制流或实际像素透明度。MatColor alpha、混合状态和最终屏幕透明度是三个不同层次。

**实现影响：** 高层入口带有内建显示策略，不能假设 LHolo 配置的填充/描边 alpha 原样生效；也不能把高层开关直接映射到现有透视配置。需要先确定是否采用该高层策略，或改用可独立控制的底层插入路径。

### 3A.3 mce::Mesh 转换的新增依据

教育版在 `0x10B6EABA0` 存在符号和可解码实现：

```cpp
mce::Mesh::operator dragon::mesh::Mesh() const;
```

已读机器码包含：

- 根据已有计数字段与布局字段构造缓冲描述记录。
- 按某个标志选择单组或附加组描述。
- 读取现有资源字段，执行共享资源相关操作。
- 当前观察到的正常返回在 `0x10B6EAECF`。

这使转换接口从“只有 SDK 声明”提升为“教育版确有实现”。仍未完成 Windows 对应定位、链接可用性、资源类型分支、是否需要先准备 HAL 数据以及布局匹配验证，不能据此宣称现有 LHolo Mesh 可直接接入编辑器。

## 4. 与现有架构一致的重写边界

遵守 DEVELOPMENT.md 的依赖方向，不增加第二套状态扫描、Worker、会话或 UI 配置。

| 模块 | 保留职责 | 预期调整 |
| --- | --- | --- |
| correction/ProjectionCorrectionTracker | 纠错状态、进度、六邻居失效 | 不调用渲染接口 |
| core/ProjectionInternalTypes、ProjectionState | 纯类型、资源所有权声明 | 用明确颜色类别与样式组织纠错资源，禁止依赖 mesh/runtime |
| mesh/ProjectionSectionBuilder | CPU 几何构建 | 沿用可见性、变换、面剔除；按颜色及填充/描边分批 |
| mesh/ProjectionMeshWorker | 单线程任务和结果队列 | 结果携带相同批次，不捕获活动渲染上下文 |
| mesh/ProjectionMeshUpload | generation/revision 校验及预算上传 | 批次作为完整结果替换，失败不发布半成品 |
| mesh/ProjectionRenderer | 已上传资源的 pass 提交 | 委托独立纠错后端，保持自身不生成几何 |
| runtime/ProjectionInvalidation | 设置和 section 失效 | 覆盖全部新批次，确保过期结果不能覆盖最新状态 |
| runtime/ProjectionLifecycle | 会话资源准备与释放 | 统一释放全部批次和版本适配资源 |
| hooks、UI、settings、world | 原职责 | 不承载内部 ABI 或颜色绑定逻辑 |

建议新增的 `mesh/ProjectionCorrectionRenderer.*` 仅在具备可验证后端后实现。版本专用结构和调用封装限制在该实现或其内部适配文件中，禁止散落到 core、tracker、hooks、UI。

SDK 已声明 `mce::Mesh::operator dragon::mesh::Mesh() const`。这是保留现有 Tessellator/Worker/上传管线的重要候选，需要先验证目标符号可解析、转换后的布局、图元和资源所有权，再决定是否采用。

## 5. 长期维护要求

- 使用版本可解析的符号或经验证的定位方式；本文 VA 不进入通用算法。
- 类型大小、对齐、关键偏移和 variant 序号集中记录；SDK 空占位不能伪装成完整类型。
- 明确区分同帧颜色复制与 GPU 资源生命周期。
- 不依赖修改共享原版材质后“立即恢复”来推断异步消费状态。
- 初始化失败输出可定位且不刷屏的原因，不静默当作成功；不得发布未经验证的 fallback。
- 维持 Worker generation、结构 generation、section revision、上传预算、停止次序及跨世界资源隔离。
- 保留原配置格式、默认值、HUD 统计和放置模块行为。
- 不顺带重构普通方块、液体、方块实体、整体选区线框。

## 6. 生产替换的准入检查

以下条件全部满足后，再切换当前纠错渲染：

1. 确认 BoundingBox/Outline 描述生成端、完整布局、分支语义与初始化条件。
2. 确认 Mesh 创建/转换/上传到实际 shader 顶点布局的对应。
3. 确认填充和描边独立透明度、深度测试/写入、剔除、透视设置的实际控制点。
4. 确认帧插入入口、提交对象所有权及消费时机。
5. 编译和链接验证通过，不仅是头文件能够解析。
6. 游戏内验证单立方体、多颜色批次、透明度 0/默认/100、遮挡、相机移动、远坐标。
7. 回归相邻错误面、section 边界、旋转镜像、切层、快速改设置、增量重建、切维度、退出世界和大结构性能。

---

## 7. 第二轮研究新增证据（2026-09-08，同日续）

本节为后续只读研究的增量结论，不改变上述准入状态。编号沿用本文件证据约定。

### 7.1 SDK 声明的稳定插入入口（准入 4 的关键进展）

26.32 客户端 SDK 头文件给出两条可链接的稳定入口，均不依赖本文件 VA：

- `mc/deps/minecraft_renderer/framebuilder/FrameBuilder.h` 声明了纯虚
  `_insert(std::variant<std::reference_wrapper<RenderEditorBlockVolumeBoundingBoxDescription const>, …共 7 个编辑器描述…>)`。
  variant 顺序与 `sub_146BCEB40` 的 7 个分支一一对应（BoundingBox=0 … RenderPlane=6），
  证实 §2.2 的序号映射来自真实接口签名，而非仅凭分支顺序。
- `mc/common/Globals.h:1226` 声明 `MCAPI mce::framebuilder::FrameBuilder* renderDragonFrameBuilder()`。
  教育版 `mce::Mesh::_renderMesh` 内部正是经同一全局获取帧构建器后插入
  `RenderMeshFallbackDescription`，说明该入口在立即渲染窗口（`$renderBlockEntities` 后）可用。

也就是说：模组侧"获得 FrameBuilder 实例 + 调用编辑器 variant 插入"全部有 SDK 导出符号支撑，
不需要任何固定偏移。此前"vtable+0x338 无常规间接调用"的现象与该结论一致：编辑器描述的分发
经由 `std::visit`/variant 机制完成，不以 `[reg+0x338]` 形式出现。

### 7.2 mce::Mesh → dragon::mesh::Mesh 转换（准入 2 的关键进展）

- `Mesh.h:119` 声明 `MCAPI explicit operator ::dragon::mesh::Mesh() const;`，为导出符号。
  保留现有 Tessellator/Worker/上传管线、在提交时转换，是可行候选。
- `dragon::mesh::Mesh` SDK 布局为 88 字节不透明存储；其拷贝构造/析构标记为 MCNAPI
  （仍为导入符号、仅弃用警告）。**按值持有该类型存在链接风险，准入 5 必须先做最小链接实验**
  （单独编译一个持有/拷贝该类型的翻译单元），不能等整体重写完成才暴露 LNK2019。
- 描述内 Mesh 字段间距与 88 字节布局吻合：`+48(88)+flag@+136(+pad)`、`+144(88)+flag@+232`、
  `+240(88)+flag@+328`，即三个"值类型 Mesh + 有效标志"槽位；这与 §3A.2 表格互相印证。

### 7.3 case 0 提交语义补充（准入 1、3 的部分推进）

对 `sub_146BFFDA0`（BoundingBox 分支）提交段的复读确认并细化 §3A.2：

- Hull `MatColor = {rgb@+368(12B), alpha@+380 * 0.9f}`（提交期固定乘 0.9，不可配置；
  若要求 LHolo 透明度设置原样生效，需在生成侧预除 0.9 并钳制，文档需明示该变换）。
- `CameraDirection = {xyz@+24, 1.0f}`（w 固定 1.0）。
- `+388` 为双用途：`float(+388) * 0.0001` 沿盒体对角线归一化方向平移几何（膨胀量）；
  同值 `-40` 后进入绘制提交字段（语义仍待生成端确认，暂按"深度偏置原始值"对待）。
- `+36`（BYTE）提交前 `+1`，进入 Transform 类组件字段；`+384`（WORD）进入提交参数。
  两者的枚举含义仍未从生成端证实。
- `+392 == 1` 触发的 alpha=0.1 附加提交**仅存在于两组 Wireframe 分支，Hull 分支没有**
  （case0.cpp 中 +392 只出现在 +48 与 +240 两组线框路径）。它不是通用的"透视"开关；
  LHolo 的穿透显示不能直接映射到该位。
- 盒体变换由 `min@+0`、`max@+12` 相减求归一化对角向量构造；Hull 提交位置额外叠加
  `对角线单位向量 * 0.25`（提示 Hull 网格可能是单位盒、经变换缩放定位；顶点布局匹配仍以准入 2 为准）。

### 7.4 高层 vs 底层路径的选型结论（供实现方案讨论）

- **高层描述路径**（自行构造 BoundingBox 描述 + `_insert`）：
  优点：只依赖 SDK 导出符号；颜色、双线框、Hull 的组装逻辑全部复用原版提交器；
  缺点：继承内建显示策略（alpha×0.9、+392 仅线框、+36/+384/+388 语义未完全定名），
  且描述必须携带三个 dragon::mesh::Mesh 值类型（7.2 链接风险）。
- **继续等待生成端**：生成端经虚工厂注册（`sub_14092FE80` 挂在 .rdata 函数表 0x14dbe1218），
  静态追踪成本高；对"自行构造描述"方案而言，生成端主要用于字段语义定名，
  布局已由消费端逐字段证实，可降级为"验证性证据"而非阻塞项。
- 结论倾向：**高层描述路径 + 最小链接实验先行**；`+36/+384/+388` 采用生成端样本值
  （若后续在实机 dump 出编辑器描述实例，可回填校验）。

### 7.5 更新后的准入缺口清单

1. （准入 5 前置）dragon::mesh::Mesh 拷贝/析构链接实验；variant 构造与虚调用 ABI 冒烟。
2. （准入 2）Tessellator 顶点流（Position+UV+Color[+Normal]）经转换后与 Hull/Wireframe
   VS 输入（NORMAL/POSITION/TEXCOORD）的匹配验证；不足字段的补齐方式。
3. （准入 3）Hull alpha×0.9 预除方案、线框虚线动画（FrameTime）对 LHolo 视觉的取舍；
   穿透显示在新路径的替代实现（不能映射 +392）。
4. （准入 6/7）实验版构建后按 §6 逐项游戏内验证，先单立方体多色，后全功能回归。

## 8. 实施进展记录（2026-09-08 同日）

### 8.1 准入 5 前置：链接实验结果

按 §7.5 第 1 项完成最小实现并整库链接，结果：

- `mce::Mesh::operator dragon::mesh::Mesh()`（MCAPI）与 `renderDragonFrameBuilder()`（MCAPI）链接通过。
- `FrameBuilder::_insert`（编辑器 7-type variant 虚函数）经 SDK 抽象接口编译链接通过。
- `dragon::mesh::Mesh` 拷贝构造/析构按预测产生 LNK2019（MCNAPI 未进导入库）。
- 拷贝构造需求已消除：C++ 保证省略（prvalue 直接在描述槽位构造，项目沿用既有 C++20 设置，
  未改任何语言标准）。
- 析构改经 LeviLamina 运行时符号服务按修饰名 `??1Mesh@mesh@dragon@@QEAA@XZ` 解析；
  解析失败时本会话禁用纠错外壳并输出单条原因日志。运行时是否可解析待游戏内确认。
- 备选方案（按 SDK `ResourcePointer = shared_ptr` 复刻析构语义）已被否决：教育版
  `~Mesh` 机器码展示 24 字节链表虚调用释放链，跨构建语义不可等价，符合 §1 的警告。

### 8.3 实测结果与路线切换（同日末）

编辑器描述路径实机首测：`??1Mesh@mesh@dragon@@QEAA@XZ` 运行时解析返回空
（日志 "Correction overlay disabled"），按 8.1 的兜底禁用了外壳，未崩溃。

处置：编辑器描述路线整体搁置（准入 1-4 的成果保留在本文件），生产切换到
原版立即绘制路径：几何按颜色分批，每批绘制前写 `MeshContext::currentShaderColor`
（正式版 `makeMaterialFilter` 反编译证实五种旧版颜色着色器接收 `CurrentColor`，
原版选框即以此机制着色），pass 结束恢复。26.32 闪烁/变色的最终解释：
借用 selection 系材质却从未设置该全局颜色，颜色随全场其他绘制反复改写。

编辑器路线若要复活，前置条件是拿到可验证的 retail `~Mesh` 调用方式
（导出、运行时可解析、或经模式定位并按版本门禁），本文件不再把它列为默认方案。

后续实测补充：立即绘制路径的颜色机制工作正常（逐批颜色生效、pass 恢复正常），
但 26.32 的实际屏幕颜色相对设定常量存在色相偏移，同一机制在 26.20 无此现象。
初步怀疑 26.32 回退管线对 `CurrentColor` 附加了色彩空间或材质层面的额外变换；
列为待适配项（标定常量或定位变换），不影响其余功能的回归。

### 8.2 已落地的管线改造（保持原架构边界）

- `core/ProjectionInternalTypes.h` 新增 `CorrectionColor`（Missing/WrongType/WrongState/Extra）。
- `ProjectionState`、`AsyncSectionBuildResult`、调度快照、上传、生命周期、失效全部改为
  按颜色 × 样式的 4+4 批次；Worker/上传/generation/revision 语义不变。
- `mesh/ProjectionSectionBuilder` 纠错几何改为按颜色分批、顶点不带颜色、中性 UV；
  保留面剔除、2mm 收缩/外扩、空网格 `UploadMode::Never` 收尾（§11.3 修复不回退）。
- 新增 `mesh/ProjectionCorrectionRenderer`：描述布局镜像（400B，字段级 static_assert）、
  逐(分区,颜色)构造描述并插入帧；版本专用结构全部隔离在该翻译单元。
- `mesh/ProjectionRenderer` 移除 `debug` 材质路径与穿透显示（含 `ScopedNoDepthTest`）。
- 穿透显示功能整体移除（配置键、会话状态、菜单开关、文档），配置版本升至 12。
- 待游戏内验证的语义参数集中在描述填充处：`hullTransformFlag=0`、`submitFlags=0`、
  `inflateDepthBias=0`、体积角点=分区世界包围盒；单立方体实测后校准。

尚未完成以上准入；不能把研究地址和伪代码直接包装成已验证实现。

## 9. 跨平台研究偏差（2026-09-09 补充）

**教育版 IDB 为 macOS 构建**（Mach-O 格式，System V ABI，Itanium 符号），Windows 版为 PE
（Microsoft x64 ABI，MSVC 符号）。两者编译器不同，struct padding 和寄存器分配不同。

本文件 §2-§3A 中标注为教育版分析的结论，其偏移和代码模式**不可直接套用到 Windows 版**。
Windows 版消费者（`sub_146BFFDA0`）的字段偏移是从 Windows IDB 独立读取的，可信。
但教育版内部的 `makeMaterialFilter`（五着色器白名单、MeshFallback 材质路由）等分析
基于 macOS EDU，对 Windows 版仅为方向性参考。

后续研究如需精确字段偏移和代码逻辑，必须在 Windows IDB（`Minecraft.Windows.exe.i64`）
中进行。教育版仅用于确认共享设施存在性和接口签名格式。

---

## 10. 编辑器路线最终状态（2026-09-09 结案）

编辑器方块体积描述路径经历了完整的"研究→实现→部署→实测→诊断→分析"闭环。
以下为最终状态记录。

### 10.1 已实现并确认可工作的部分

- 描述布局镜像（400B）通过 Windows 消费者函数逐字段确认。
- 转换运算符（MCAPI）成功将 mce::Mesh 转为 dragon::mesh::Mesh。
- 持久缓存持有转换结果，每帧字节拷贝进描述槽位。
- `renderDragonFrameBuilder()->_insert()` 虚调用成功执行，无崩溃。
- 诊断确认：`inserts=1, convertedCache=2, validFills=1, validOutlines=1`。

### 10.2 不可逾越的障碍

**编辑器描述在普通（非 Editor）游戏会话中被管线静默丢弃。**

全链路诊断确认：
- 转换运算符正常执行（convertedCache 有值）。
- 字节拷贝进描述槽位正常。
- `FrameBuilder::_insert` 虚调用无崩溃。
- Windows 版消费者门控仅有存在标志 + `areBuffersValid`，无编辑器模式检查。
- **但屏幕无输出**。

最合理解释：编辑器渲染 pass（`EditorBlockVolumeHull`/`Wireframe` 材质对应的 pass bucket）
在普通游戏会话中不被创建或激活。这是游戏管线架构设计，非 mod 可修。

### 10.3 已否决的子方案

| 子方案 | 否决原因 |
| --- | --- |
| 按值持有 + 显式调 `~Mesh()` | `~Mesh` 为 MCNAPI，导入库无此符号（LNK2019），运行时按修饰名解析返回空 |
| 字节复刻 `~Mesh` 逻辑 | EDU（macOS/clang）机器码不可安全映射到 Windows（MSVC）；涉及 shared_ptr 控制块和 MemoryTracker 分配链 |
| 直接调 `makeMeshFilter` | 需 `dragon::mesh::Mesh const&` 参数，仍回到持有/析构问题 |
| 修改 `MeshFallbackPosUVNormalColor` 材质 blend state | 材质状态改写是否被异步帧消费不可控（研究文档 §5 明确警告） |

### 10.4 复活条件

当以下任一条件满足时，编辑器路线可复活（实现代码已在 git 历史中）：

1. LiteLDev 上游将 `dragon::mesh::Mesh::~Mesh` 加入 Windows 符号表（mcapi-requests）。
2. 找到 Windows 版 `~Mesh` 的稳定运行时定位方式（签名扫描 + 多版本门禁验证）。
3. 确认编辑器渲染 pass 的激活条件并从 mod 侧启用。

在此之前，生产使用 currentShaderColor 立即绘制路径。
