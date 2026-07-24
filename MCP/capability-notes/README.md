# MCP 能力边界记录

> **何时读这份文档**：当你用 MCP 遇到"做不了"的操作时，**先看这里，不要立即下结论说 MCP 不支持**。

> **本项目有两个 MCP 可同时使用**：本地 `ue-editor-mcp`（socket 55558）与 UE5.8 官方 `ue58-official-mcp`（HTTP 8000 `/mcp`）。**同时使用时的选型与协同规则见 `dual-mcp-usage-guide.md`（必读）。** 本文件夹下"确认能力不足的流程"同样适用于官方 MCP——官方走 `list_toolsets`→`describe_toolset`→`call_tool` 三步发现能力，下结论前先 `describe` 对应 toolset。

---

## 核心规范：确认 MCP 能力不足的流程

**绝对禁止**未经充分验证就告知用户"MCP 做不了某事"。必须按以下流程操作：

### Step 1: 先查现有踩坑记录
打开本文件夹下相关文档，查看是否已有该能力的记录。如果有，按记录执行。

### Step 2: 多角度搜索尝试
- 用 `ue_actions_search` 至少尝试 3 种不同搜索词
- 用宽泛词搜索（如 `widget`、`component`、`event`），从结果列表里找
- 查看 action 的 schema（`ue_actions_schema`），确认参数支持范围

### Step 3: 实际调用验证
- 不要只看搜索结果就下结论，**必须实际调用 action 验证**
- 比如 `widget.bind_event` 搜到了，要实际调用一次看是否成功
- 比如 `widget.get_tree` 搜到了，要实际调用看返回的数据结构

### Step 4: 三次尝试无果后告知用户
只有 Step 1-3 都失败后，才能告知用户可能的 MCP 能力缺口。告知时使用标准格式：

```
MCP 能力缺口初步判断：
- 需要的功能：[描述]
- 已尝试的搜索词：[词1]、[词2]、[词3]
- 已查阅的 schema：[action_id 列表]
- 实际调用验证：[尝试了什么，返回什么]
- 初步结论：该功能可能当前 MCP 不支持

请用户向负责脚手架的 AI 确认，或查询官方文档。
```

### Step 5: 用户确认后记录
当用户从其他渠道（官方文档、脚手架 AI 等）确认了 MCP 的真实能力边界后，**立即在本文件夹下创建或更新记录文档**，内容包括：
- 能力名称
- 实际能力（支持/部分支持/不支持）
- 对应的 action_id
- 使用示例
- 已知限制
- 确认来源（用户反馈 / 官方文档 / 脚手架 AI 等）

---

## 文档组织

本文件夹下按 MCP 功能域分文档：

| 文档 | 覆盖范围 |
|---|---|
| `dual-mcp-usage-guide.md` | **双 MCP 协同使用规范**：ue-editor-mcp 与官方 MCP 的定位、能力矩阵、决策规则（同时用两者前必读） |
| `mcp_capability_overview.md` | UE5.8 官方 MCP 全部 23 个 toolset 的工具清单（实测汇总，数据附录） |
| `mcp_describe_blueprint.md` | 官方 `BlueprintTools` 完整工具签名与描述（实测，数据附录） |
| `widget-umg-capabilities.md` | UMG Widget Blueprint 操作（创建、属性、事件、布局等） |
| `blueprint-capabilities.md` | Blueprint 蓝图操作（变量、函数、节点、连线等）— 待创建 |
| `editor-capabilities.md` | 编辑器/场景操作（Actor、关卡、资源管理等）— 待创建 |

> 后续发现新的能力域需要记录时，新建对应文档并更新本表。

---

## 踩坑教训汇总

### 教训 1：不要轻言"MCP 做不了"

**典型场景**：操作 UMG Widget Blueprint 时

**错误判断**：
| 操作 | 错误地说 | 实际情况 |
|---|---|---|
| 读取 position/size/anchors | ❌ 读不到 | ✅ `widget.get_tree` 能读完整 slot 信息 |
| 绑定按钮 OnClicked 事件 | ❌ 做不了 | ✅ `widget.bind_event` 完全支持 |

**根本原因**：只用了 `widget.list_components` 这一个 action，看到它只返回 name+class 就下结论。**没有搜索其他相关 action，没有查看 schema，没有实际调用验证**。

**正确做法**：
1. 搜 `widget get tree` → 找到 `widget.get_tree`
2. 搜 `widget bind event on clicked` → 找到 `widget.bind_event`
3. 查看 schema 确认参数
4. 实际调用验证

**记忆教训**：
- MCP 的 action 数量很多（131+ 个），单个 action 能力有限，**必须搜索相关 action 集合**
- **搜索 + schema + 实际调用**，三步缺一不可
- 承认能力不足前，至少要搜 3 个不同关键词

---

## 与 MCP-SEARCH-GUIDE.md 的关系

- `MCP-SEARCH-GUIDE.md`（项目根目录）：搜索词换词表，解决"搜不到"的问题
- `MCP/capability-notes/`（本文件夹）：能力边界确认记录，解决"做不了吗"的问题

**使用顺序**：
1. 搜索不到 → 先看 `MCP-SEARCH-GUIDE.md`
2. 搜到了但不确定能不能做 → 看本文件夹
3. 都没有相关记录 → 按 Step 1-5 流程操作

---

*本文档随项目开发持续更新。发现新的踩坑请补充到对应章节。*
