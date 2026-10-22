# Issue tracker：本地 Markdown

本项目的 issue 和规格存放在 `.scratch/`，正文使用中文。

## 文件约定

- 每项功能一个目录：`.scratch/<feature-slug>/`。
- 规格：`.scratch/<feature-slug>/spec.md`。
- 实施 issue：`.scratch/<feature-slug>/issues/<NN>-<slug>.md`，从 `01` 编号，每个 issue 一个文件。
- 在 issue 顶部附近用 `Status:` 记录分诊状态，具体值见 `triage-labels.md`。
- 讨论和历史记录追加至文件底部的 `## Comments` 标题下。

## 技能操作映射

技能要求“发布到 issue tracker”时，在相应功能目录下创建规格或独立 issue 文件，并按需创建目录。

技能要求“获取相关 ticket”时，读取指定路径。若用户仅提供编号，在功能目录内查找；编号存在歧义时确认具体文件。

## Wayfinder 操作

- 地图：`.scratch/<effort>/map.md`，记录 Notes、Decisions-so-far 和 Fog。
- 子 ticket：`.scratch/<effort>/issues/NN-<slug>.md`，从 `01` 编号，正文记录问题。
- 类型：顶部的 `Type:` 使用 `research`、`prototype`、`grilling` 或 `task`。
- 状态：Wayfinder ticket 使用 `Status: open`、`Status: claimed` 或 `Status: resolved`；普通实施 issue 使用分诊标签。
- 依赖：顶部的 `Blocked by: NN, NN` 记录前置 ticket；全部依赖均为 `resolved` 时解除阻塞。
- 下一项：扫描 issue 目录，选择尚未解决、未被认领且未阻塞的 ticket，按编号优先。
- 认领：开始工作前将状态设为 `claimed` 并保存。
- 解决：在 `## Answer` 下追加答案，设为 `resolved`，并向地图的 Decisions-so-far 追加摘要和文件链接。
