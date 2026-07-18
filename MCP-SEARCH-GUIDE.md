# MCP 搜索踩坑指南

> **何时读这份文档**：当你用 `ue_actions_search` 搜索某个能力，返回结果为空或不符合预期时，**先看这里，不要立即怀疑 MCP 不支持该功能**。

> **相关文档**：
> - 搜到了 action 但不确定能不能做某事 → 看 `MCP/capability-notes/` 文件夹（记录了已验证的能力边界）
> - 想了解蓝图连线专题经验 → 看 `memory-bank/notes-mcp-blueprint-patterns.md`
> - `MCP/capability-notes/` 会记录每次"确认 MCP 能力不足"的完整流程和结果

---

## 核心原则

`ue_actions_search` 是**语义搜索**，不是关键词精确匹配。搜索失败 99% 是搜索词问题，不是 MCP 能力缺失。

**排查顺序：**
1. 换词重搜（见下方换词表）
2. 用宽泛词搜索，再从结果里找
3. 直接查 SKILL.md Part C Action Reference
4. 确认 MCP 连接正常（`ue_ping`）
5. 以上都失败 → 上报用户，说明具体搜索词和返回结果

---

## 常见搜索失败场景与换词表

### 变量相关

| 你搜的词（失败） | 换成这个 | 对应 action_id |
|---|---|---|
| `variable read` / `get variable` | `variable get node` | `add_blueprint_variable_get` |
| `variable write` / `set variable` | `variable set node` | `add_blueprint_variable_set` |
| `declare variable` / `new variable` | `add variable` / `blueprint variable` | `add_blueprint_variable` |
| `variable default value` | `variable default` | `set_blueprint_variable_default` |
| `local variable` | `function local variable` | `add_function_local_variable` |
| `variable tooltip` / `variable category` | `variable metadata` | `set_variable_metadata` |

### 函数调用相关

| 你搜的词（失败） | 换成这个 | 对应 action_id |
|---|---|---|
| `call function` / `function call node` | `add function node` | `add_blueprint_function_node` |
| `math function` / `add vectors` | `function node` + target=`math` | `add_blueprint_function_node` |
| `component transform` / `set location` | `function node` + target=`SceneComponent` | `add_blueprint_function_node` |
| `create new function` | `blueprint function graph` | `create_blueprint_function` |
| `call blueprint function` | `call blueprint` | `call_blueprint_function` |

### 节点连接相关

| 你搜的词（失败） | 换成这个 | 对应 action_id |
|---|---|---|
| `wire nodes` / `link pins` | `connect nodes` | `connect_blueprint_nodes` |
| `disconnect` / `unlink` | `disconnect pin` | `disconnect_blueprint_pin` |
| `pin value` / `set default` | `pin default` | `set_node_pin_default` |

### 事件相关

| 你搜的词（失败） | 换成这个 | 对应 action_id |
|---|---|---|
| `begin play` / `on start` | `event node` / `blueprint event` | `add_blueprint_event_node` |
| `custom event` / `new event` | `custom event` | `add_blueprint_custom_event` |
| `input event` / `key press` | `input action node` | `add_blueprint_input_action_node` |
| `enhanced input` | `enhanced input action` | `add_enhanced_input_action_node` |
| `delegate` / `event dispatcher` | `event dispatcher` | `add_event_dispatcher` |

### 蓝图结构相关

| 你搜的词（失败） | 换成这个 | 对应 action_id |
|---|---|---|
| `new blueprint` / `create class` | `create blueprint` | `create_blueprint` |
| `add component` | `add component to blueprint` | `add_component_to_blueprint` |
| `compile` / `build blueprint` | `compile blueprint` | `compile_blueprint` |
| `parent class` / `reparent` | `blueprint parent class` | `set_blueprint_parent_class` |
| `interface` | `blueprint interface` | `add_blueprint_interface` |

### 图表读取/分析相关

| 你搜的词（失败） | 换成这个 | 对应 action_id |
|---|---|---|
| `read graph` / `inspect nodes` | `describe graph` | `describe_graph` |
| `full blueprint info` | `describe blueprint` / `blueprint summary` | `describe_blueprint_full` / `get_blueprint_summary` |
| `find node` / `search node` | `find blueprint nodes` | `find_blueprint_nodes` |
| `node pins` / `pin info` | `get node pins` | `get_node_pins` |
| `selected nodes` | `get selected nodes` | `get_selected_nodes` |

### 编辑器/场景相关

| 你搜的词（失败） | 换成这个 | 对应 action_id |
|---|---|---|
| `list objects` / `scene objects` | `get actors in level` | `get_actors_in_level` |
| `place actor` | `spawn actor` | `spawn_actor` |
| `move actor` / `position` | `set actor transform` | `set_actor_transform` |
| `actor info` / `actor details` | `get actor properties` | `get_actor_properties` |
| `save` / `save project` | `save all` | `save_all` |
| `editor log` / `output log` | `get editor logs` | `get_editor_logs` |

---

## `add_blueprint_function_node` 的 target 写法

这个 action 支持**任意 UE 函数**，不是白名单。搜索失败通常是 `target` 写错了。

```json
// 数学库
{ "target": "math",  "function_name": "Add_VectorVector" }
{ "target": "KismetMathLibrary",  "function_name": "VSize" }

// 系统库
{ "target": "systemlibrary",  "function_name": "PrintString" }

// GameplayStatics
{ "target": "gameplaystatics",  "function_name": "GetPlayerController" }

// 组件方法（UE 内部有 K2_ 前缀，MCP 会自动尝试）
{ "target": "SceneComponent",  "function_name": "SetRelativeLocation" }
{ "target": "Actor",  "function_name": "GetActorLocation" }

// 完整路径（最精确）
{ "target": "/Script/Engine.KismetMathLibrary",  "function_name": "Clamp" }

// 项目自定义库（MCP 会全局扫描 UClass）
{ "target": "MyBlueprintFunctionLibrary",  "function_name": "MyCustomFunc" }
```

**K2_ 前缀**：UE 内部很多 Actor/Component 方法有 `K2_` 前缀（如 `K2_SetActorLocation`），MCP 会自动尝试，你直接写 `SetActorLocation` 即可。

---

## 宽泛词搜索策略

当不确定 action_id 时，用**单个宽泛词**搜索，从结果列表里找：

```
ue_actions_search("variable")     → 所有变量相关 action
ue_actions_search("node")         → 所有节点操作 action
ue_actions_search("blueprint")    → 所有蓝图操作 action
ue_actions_search("graph")        → 所有图表操作 action
ue_actions_search("widget")       → 所有 UMG 相关 action
ue_actions_search("material")     → 所有材质相关 action
ue_actions_search("actor")        → 所有 Actor 操作 action
ue_actions_search("event")        → 所有事件相关 action
ue_actions_search("input")        → 所有输入相关 action
ue_actions_search("layout")       → 所有布局/自动排列 action
```

---

## 确认 MCP 连接正常

搜索结果为空时，先排除连接问题：

```
ue_ping  →  应返回 { "pong": true }
```

如果 `ue_ping` 失败，是连接问题，不是搜索词问题。此时上报用户：**"MCP 连接不可用，请确认 UE 编辑器已启动并加载了 UEEditorMCP 插件"**。

---

## 上报用户的标准格式

只有在以下**所有步骤都失败**后，才上报用户：

1. ✅ 已尝试至少 3 种不同搜索词
2. ✅ 已查阅 SKILL.md Part C Action Reference
3. ✅ `ue_ping` 确认连接正常

上报格式：
```
MCP 能力缺口确认：
- 需要的功能：[描述]
- 已尝试的搜索词：[词1]、[词2]、[词3]
- ue_ping 状态：正常
- 结论：该功能当前 MCP 不支持，需要扩展或手动操作
```

---

*本文档持续更新。发现新的搜索坑请补充到对应表格。*