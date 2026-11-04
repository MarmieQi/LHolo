# LHolo OreUI 构建工具

日常构建只运行 `xmake`，完整输出为 `bin/LHolo/`，不生成 ZIP。
默认使用仓库内经过哈希校验的目标版本运行时。核对本机游戏资源时才显式配置 `oreui_hbui` 或 `LHOLO_OREUI_HBUI`；机器路径不进入源码。

## 职责

- `package.lua`：默认 xmake 构建钩子，前置生成页面，原生 modpacker 完成后组装目录。
- `build_menu.py`：核验目标游戏 bundle SHA256，构建 LHolo 前端，生成 `build/oreui/` 页面。
- `runtime_adapter.js`：目标版本私有 React/Facet、输入、滚动、图标与音效适配。
- `resource_pack.json`：固定资源包 UUID 与引擎要求；版本来自原生 manifest。
- `assemble_package.py`：组装 `bin/LHolo` 的六项必需文件，校验版本、UUID、协议和哈希，报告写入 `build/oreui/package-validation.json`。
- `release_package.py`：仅在正式发布阶段从完整目录生成六文件 ZIP 和本地 SHA256；只上传 ZIP，CI 使用 GitHub 附件摘要校验下载结果。
- `deploy.ps1`：可选的测试部署工具，校验报告和 bin 文件后复制到目标 mods；不参与编译，不操作游戏进程，不生成 ZIP，不处理用户配置。
- `tests/`：验证目录完整性、重复组装、版本冲突及资源不兼容时拒绝发布。

构建成功后可直接复制 `bin/LHolo` 到游戏 `mods`。需要脚本部署时，在游戏退出后运行 `tools/oreui/deploy.ps1`。旧独立打包入口、ZIP 测试包和旧游戏目录部署入口已移除。

不覆盖原版 gameplay/index。生成页面依赖经过哈希审核的目标版本运行时；这些页面为必需资源，随正式安装包一起分发，生成文件不提交源码。默认 GitHub 构建使用同一份内置运行时生成完整目录并上传 artifact；正式发布使用对应提交的原始 artifact ZIP，发行 CI 校验已上传附件。内置资源包挂载及限定 HTML/JS 读取通路已由用户实测。

详见 [打包说明](../../docs/OREUI_PACKAGING.md) 与 [迁移说明](../../docs/OREUI_MIGRATION.md)。
