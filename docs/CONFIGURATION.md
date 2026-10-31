# 配置文件

配置路径：`mods/LHolo/config/config.json`，当前格式版本为 **13**。
完整默认配置见 [config.example.json](config.example.json)。字段顺序固定，缩进为 4 个空格。

| 分组 | 内容 |
| --- | --- |
| `interface` | 界面语言 locale 代码，默认 `zh_CN` |
| `rendering` | 结构边框、纠错填充/描边、穿透显示 |
| `hud.projection` | 普通投影 HUD 开关、内容与外观 |
| `hud.materials` | 材料 HUD 开关、堆叠换算、每页项数与外观 |
| `input` | Alt+滚轮开关和快捷键 |
| `placement` | 知情确认、放置半径、破坏后冷却 |

## 数值与位置

- 后缀 `Percent` 明确表示百分比。透明度 0～100，HUD 缩放 50～200；默认背景不透明度 60%、缩放 100%。
- 材料 `convertToStacks` 默认 `false`，`itemsPerPage` 范围 1～50、默认 5。
- `placement.radius` 范围 1～4，`breakCooldownSeconds` 范围 0～60、默认 10 秒。
- `position.anchor` 使用 `topLeft`、`bottomLeft`、`topRight`、`bottomRight` 四个稳定名称。
- 使用原生 HUD 编辑器拖动后，位置中会增加 `custom: {"x": 0.2, "y": 0.3}`，坐标相对视口归一化到 0～1。不使用自定义位置时省略整个 `custom` 对象，避免保存无效坐标。

## 快捷键

快捷键按组合字符串保存，例如 `"Alt+M"`、`"Ctrl+Left"`、`"Ctrl+Shift+F8"`，`null` 表示未绑定。大小写及分隔符两侧空格不影响读取；保存时统一为 Ctrl、Alt、Shift 的顺序。

支持字母、数字、F1～F24、Left/Right/Up/Down、PageUp/PageDown、Home/End、Insert/Delete、Enter/Tab/Space/Escape、MouseLeft/MouseRight/MouseMiddle/MouseSide1/MouseSide2，以及 Numpad0～Numpad9 等常见按键。少见按键使用 `VK_186` 这种可往返的 Windows 虚拟键码表示。

材料 HUD 上一页和下一页默认均为 `null`。建议在游戏快捷键页面绑定，沿用原有冲突处理及按键捕获规则。

## 旧配置迁移

1. 可以读取版本 12 及更早的平铺配置，将仍有效的语言、渲染、HUD、布局、快捷键和放置参数映射到新分组。
2. 成功应用后写回规范格式。第一次覆盖旧配置前，将原文件逐字节备份为 `config.json.legacy.bak`，以后保存不会覆盖该备份。
3. 新配置不再保存旧单文件最近路径、`saved*` 恢复记录、旧 ImGui `uiScale`、退役的整体透明度或旧放置模式快捷键。任务目录、每文件维度/绝对坐标继续由 `data/tasks.json` 独立持久化。
4. 旧世界轴快捷键名、旧 HUD 标签可在迁移时读取，写入时只使用当前规范字段。

辅助放置模式本身、材料页码、投影运行状态均为会话状态，不写入偏好文件。隐藏旧 ImGui 的代码按项目约束保留，其缩放、路径与恢复快照只保留在内存中。

## 校验与保存

缺失字段使用默认值，数值在读取边界限制到有效范围。错误类型、非法快捷键、未知位置锚点、损坏的 JSON 或不支持的未来版本会拒绝读取；不会部分应用，也不会被后续保存覆盖。修复配置后重启游戏再加载。

保存先写临时文件并关闭，再通过系统原子替换正式文件，避免直接截断当前配置。读写与迁移集中在 `src/settings/SettingsStore`；游戏状态的应用仍由原调用方负责，菜单和业务模块不感知 JSON 分组。

### 写入进度 HUD

`hud.write` 与现有投影/材料 HUD 独立：`enabled` 默认 true，`position.anchor` 默认 `topCenter`，`scalePercent` 默认 100，`backgroundOpacityPercent` 默认 60。自定义拖动位置沿用 `position.custom` 的归一化 x/y；缩放范围 50～200，透明度范围 0～100。该节缺省时按默认值加载，原有两个 HUD 的配置不变；顶中锚点随面板实测宽度保持居中。布局与外观仍通过游戏内原生 HUD 编辑器保存。
