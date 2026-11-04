# OreUI 发布与更新方案

当前适配目标为 Minecraft `1.26.60.05` / LeviLamina `26.60.0 client`，运行时输入与资源包最低引擎版本已同步；原生编译、链接和完整安装目录组装已通过，用户已反馈 26.60 适配测试通过。末次空材质对象生命周期修复仍需补测方块实体，完整专项回归继续沿用开发手册。OreUI 页面与运行时为必需资源，正式包随 DLL 一起分发。

当前统一构建流程为默认 `xmake`：生成前端、编译原生模块、组装并校验 `bin/LHolo/`，不生成 ZIP。可直接复制完整目录到目标 `mods/`；可选 `tools/oreui/deploy.ps1` 也只从 bin 读取，不重新构建或解压。旧独立打包和 ZIP 测试分支已移除。

资源包挂载、限定 URL 读取与菜单启动已由用户在目标版本实测；跨版本升级、缓存刷新及完整安装升级矩阵仍需验收。

## 一键构建完整目录（本机开发）

本地与 GitHub 使用相同的默认 `xmake` 流程。目标版本构建输入已固定在 `tools/oreui/runtime/`，不要求构建机安装游戏：

```powershell
xmake
```

`runtime/source.json` 记录游戏版本、归档哈希和两份原始输入的 SHA256。`prepare_runtime.py` 先验证整个归档及每个成员，再将 `gameplay.html` 与对应运行时脚本准备到 `build/oreui/runtime/`。这些是未经修改的版本化构建依赖，不是 LHolo 生成页面或另一套业务实现；归档不会进入安装包，也不新增 Release 附件。

需要核对本机游戏资源时，可显式配置 `xmake f --oreui_hbui="<目标游戏 data/gui/dist/hbui>" -y`，或用环境变量 `LHOLO_OREUI_HBUI` 覆盖。它们只作为可选构建输入，不写入源码或安装包；版本与哈希检查仍会执行。

构建机需要 C++ 工具链、Node/npm 和 Python；首次缺少前端依赖时自动运行锁定版本的 `npm ci`。构建钩子验证源资源、生成前端、检查 JS 语法，随后编译原生模块，最后组装完整 `bin/LHolo/`。该目录可直接复制到 `mods`；默认构建不生成 ZIP，不自动部署或操作游戏进程。已移除 `oreui_native_only` 分支，本地与远程都生成包含 UI 的完整安装目录。

`lholo.oreui.package` 构建规则依赖原生 modpacker，`tools/oreui/package.lua` 提供前置资源生成和后置目录组装钩子；`build_menu.py` 管理版本适配与前端生成；`assemble_package.py` 管理安装文件清单、版本/UUID/协议标记校验和目录哈希。校验报告在 `build/oreui/package-validation.json`，不进入模组目录。该流水线始终重新生成页面，不使用旧的 build 页面作为成功回退。UUID 固定，版本来自原生 manifest 并与 tooth 校验；不会删除模组目录或覆盖配置。

这是完整安装目录，也作为正式发行的唯一输入。构建机使用仓库内经过 SHA256 审核的目标版本依赖生成必需页面，安装者无需安装 Node、Python 或再次生成资源。GitHub 构建安装相同工具链，运行同一 `xmake` 并上传包含 `LHolo/resource_packs/LHoloUI` 的完整目录。

## 已确认的加载约定

LeviLamina 的 Mod 将资源目录定义为 `mods/<模组名>/resource_packs`。客户端初始化时将各模组资源目录注册为资源包来源，并将这些包加入客户端资源栈。因此 LHolo 使用随模组分发的资源包，不要求用户另外导入 `.mcpack` 或修改全局资源包配置。

依据：[Mod.cpp](https://github.com/LiteLDev/LeviLamina/blob/main/src/ll/api/mod/Mod.cpp)、[客户端初始化与资源栈挂载](https://github.com/LiteLDev/LeviLamina/blob/main/src-client/ll/core/main_win.cpp)。本机 LeviLamina 源码也已对照。

完整本机安装目录（正式发行沿用此结构）：

```text
LHolo/
├─ LHolo.dll
├─ manifest.json                 # LeviLamina 原生模组清单
├─ LICENSE
└─ resource_packs/
   └─ LHoloUI/
      ├─ manifest.json           # Mojang 资源包清单
      └─ hbui/
         ├─ lholo-menu.html
         └─ lholo-menu.js
```

两个 manifest 的格式和作用不同，不得共用。`tooth.json` 仍描述发布资产、安装位置和 LeviLamina 依赖，`bin/LHolo/` 安装到 `mods/LHolo/`。

## Mojang 清单与版本规则

采用稳定版资源包清单 `format_version: 2`、模块 `type: resources`，不为菜单打包引入预览版 manifest 3。填写名称、描述、header UUID、module UUID、三段整数版本和经目标游戏版本核对的 `min_engine_version`。

- header UUID 和 module UUID 互不相同，首次确定后提交源码，升级时保持不变；不得每次构建随机生成。
- 发布新版时提高包版本，header 与资源模块版本保持一致。官方说明同 UUID 包导入时，高版本替换旧版本；这不等同于证明内置包更新和 Gameface 缓存已正确刷新。
- `min_engine_version` 表示最低引擎与兼容处理依据，不是可运行于全部未来版本的承诺。私有 OreUI 运行时适配仍须锁定并验证目标游戏版本。
- 不仅凭 `experimental_custom_ui` 的名称就认为它能接通当前菜单；是否需要该能力声明应由实际加载结果确认。

依据：[Mojang 资源包 manifest 规范](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/addonsreference/packmanifest?view=minecraft-bedrock-stable)。

## 分层与构建职责

- `frontend/`：LHolo 页面、控件与应用界面状态，独立浏览器预览继续使用开发入口。
- `src/oreui/`：页面生命周期、通信、业务绑定及 UI 资源兼容检查；不承担下载、安装或升级职责。
- `tools/oreui/`：目标游戏版本的运行时适配、资源生成、清单校验和打包。生成内容进入构建目录，最终汇入 `bin/LHolo/`。
- xmake 与发布 CI：先完成受控的前端/运行时资源构建，再统一收集 DLL、清单及资源包。保持一个正式发布目录，不另建一套不同结构的发行物。
- lip/启动器：安装和更新完整模组包。运行中的 DLL 不自更新，也不在启动时修改游戏安装目录来补资源。

普通 CI 与本地都通过 `xmake` 生成完整安装目录；正式发布将经过校验的完整目录归档，只上传 ZIP 到草稿后发布。发行 CI 下载已发布附件，复查六项文件清单、整包哈希、原生/资源包版本、固定 UUID 与 UI 协议；不会用缺少资源的包覆盖完整发行附件，也不重写已确认的更新日志。

## 升级、保留与回滚

1. DLL、页面和桥接协议作为一个发行单元构建、安装及回滚。资源包 UUID 不变，正式发行版本递增；发布前校验原生 manifest、tooth、资源包清单与发行标签一致，避免手工维护的版本漂移。发行版本由 xmake 与 tooth 同步维护，资源包版本从原生 manifest 派生。
2. 正式发行计划对 LHolo 自有 JS/CSS 采用内容散列文件名作为缓存隔离方案；当前测试资源仍使用固定文件名，适配响应设置 `Cache-Control: no-store`。仍须实测新版替换后的 Gameface 缓存行为，不能仅靠资源包版本号宣称已解决。
3. 配置和用户数据不进入发行物，也不参与资源目录清理。已有 `tooth.json` 保留 `mods/LHolo/config/config.json`；若增加持久化文件，应按真实路径补充保留策略。lip 的 `preserve_files` 是卸载阶段保留规则，并不是通用的配置备份或合并机制。
4. 新版移除的自有资源必须有明确的旧文件清理策略。lip 管理安装和手动 ZIP 覆盖应分别验证；手动覆盖不能假定会删除旧文件。清理限于可确认归属 LHolo 的发行文件，不能清空整个模组目录。
5. 更新需要退出游戏后完成，不承诺热更新。回滚恢复匹配的 DLL 和完整 UI 资源；v26.51.7 使用配置 v13，升级会保留 config.json.legacy.bak；回滚到旧版本时应先恢复相应旧配置备份。
6. 从现有开发部署迁移时，仅处理已确认属于 LHolo 的游戏目录测试文件；不得删除或改写原版 `gameplay` bundle、其他模组资源或用户资源包设置。

依据：[lip 包清单与文件保留规则](https://lip.levimc.org/concepts/package_manifest.html)、[LeviLamina 发布教程](https://docs.levimc.org/LeviLamina/developer_guides/tutorials/create_your_first_mod/)。

## 当前技术边界与下一步

实测定位：资源栈读取 `hbui/lholo-menu.html` 返回 true、协议标记匹配，Hybrid 处理器对 `ui://./hbui/lholo-menu.html` 却返回 Failure。对照本机 1.26.51 客户端二进制：Hybrid `onResourceRequest`（RVA `0x42f21d0`）调用查找函数（`0x42f27e0`），其路径辅助函数（`0x42ef270`）遍历包前检查 `PackAccessStrategy::isTrusted()`（虚表偏移 `0x28`），未受信任的包被跳过。SDK `PackAccessStrategy.h` 的虚表顺序已对照。以上地址仅为本机分析记录，运行代码不硬编码这些地址；普通资源管理器能读取不等于 Hybrid HTML 入口允许读取。

适配集中在 `src/oreui/OreUiMenuResources.*`：只匹配 `ui://./hbui/lholo-menu.html` 和 `.js` 两个完整 URL，并且只在 LHoloUI 包清单存在时接管。经原生 `ResourcePackManager::load(UserPackage)` 读取包内固定路径，校验 v7 标记；通过原版 `IAsyncResourceResponse::GetSpace` 复制数据、设置内容类型，返回处理状态，由 broker 完成响应。缺失/不兼容返回 Failure，不回退到游戏目录掩盖问题；其他 URL 保持原处理，不修改全局信任策略或原版文件。接口沿用当前游戏版本私有 ABI，需要版本适配与实机验证，不属于 Mojang 稳定脚本 API。

资源包挂载已实测确认；原版 Hybrid 入口不能直接读取本次模组包，需要上述限定 URL 适配。加入适配后，用户已实测确认页面成功启动。

当前 `OreUiScreen.cpp` 检查模组包内页面协议标记。`build_menu.py` 以指定游戏版本的原版 bundle 为输入，通过严格 SHA256 和替换锚点校验后生成菜单资源。必需的 HTML/JS 与资源包清单整体进入安装包；生成文件不提交源码仓库，随正式版本作为二进制附件分发。此方案仍与目标版本的私有运行时绑定，不宣称自动兼容其他游戏版本。

## 正式发布流程

日常本地构建仍使用 `xmake`，供实机测试；正式 Release 必须使用 GitHub Actions 对发行标签对应提交编译的原始 artifact ZIP，不上传本机编译包。

1. 核对 `tooth.json.version`、xmake modVersion 与 `v<version>` 标签，并按对应 lip 官方 Schema 检查清单。v3 使用固定 format_version/format_uuid、client/win-x64、实际依赖范围及安装目录；`preserve_files` 是卸载保留规则，不是打包清单。当前保留配置、旧配置备份和任务数据。
2. 等待 `.github/workflows/build.yml` 对发行标签相同 SHA 的 Windows 构建成功。获取该 run 的 `LHolo-<SHA>-client-windows-x64` artifact，核对未过期及 GitHub digest，下载其原始 ZIP，不在本地重新编译或重新压缩。
3. 将该原始 ZIP 命名为 `LHolo-client-windows-x64.zip`。通过 `tools/oreui/release_package.py --verify <ZIP> --tag v<version>` 验证六项必需文件、版本、固定 UUID 与 UI 协议；同名 `.zip.sha256` 只作本地校验输入，不上传。ZIP 顶层为 `LHolo/`，与 tooth 安装规则一致，不含配置、PDB 或本机文件。
4. 草稿 target、发行标签和已验证 artifact 的源码提交必须一致，只上传该原始 ZIP 和已确认的更新日志，然后发布草稿。已存在的版本不能静默移动标签或改用其他提交的二进制。
5. `.github/workflows/release.yml` 下载正式附件，除核对整包哈希和安装清单，还要求其 digest 与同标签提交的成功 build.yml artifact 一致，从而拒绝本机构建包。可用 workflow_dispatch 对指定标签重验；发布校验不重编译、不改写更新日志。

v26.51.7 的远程构建来源为 Actions run 40665777019，提交 68bf0f11971944e92e18394d66b3018989ff059d。包格式参照官方模板 d0a888ec116c9a00fe4dc8f28ccead4dfc6b6760；lip v3 Schema 使用 LiteLDev/lip 的 v0.34.7 版本核对，避免把当前 v4 格式直接套到本版本。

仍须持续进行干净安装、同 UUID 升级、缓存刷新、配置保留与不兼容版本提示的实机回归。

用户补充的完整日志及 14:48 第二次日志均确认资源栈可读、Hybrid 请求失败；已据此增加限定 URL 的资源读取适配，用户随后确认菜单启动成功。

## 实测后的诊断清理

资源包读取成功后已移除 `inspectMenuResourcePack` 探测、资源读取成功/路由过程日志、`diagnostics.js` 注入和浮层、`__lholoOreUiReport` 全局函数及 `MenuContentLog` 转发钩子。前端异常使用标准 `console.error`，不再经自定义诊断通道转发。`OreUiMenuResources` 的固定 URL 读取适配、版本标记检查及正常失败日志继续保留；`lholo.menu.ready` 是初始化标签/业务快照的正常协议，不能随诊断删除。
