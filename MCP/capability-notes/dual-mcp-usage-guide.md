# 双 MCP 协同使用规范（ue-editor-mcp + UE5.8 官方 MCP）

> 归档日期：2026-07-24　环境：Demo 项目已升级 **UE5.8**
> 面向：所有在本项目工作的 AI —— 明确「同时挂载两个 MCP 时，什么任务用哪个」
> 实测证据：`MCP/capability-notes/mcp_capability_overview.md`、`mcp_describe_blueprint.md`

---

## 0. 两个 MCP 的定位

| MCP | 通道 | 规模 | 最擅长 |
|---|---|---|---|
| **ue-editor-mcp** | 本地 socket **55558**（Python server + C++ 桥，插件在 `Plugins/UEEditorMCP`） | 151 个细粒度 action | 蓝图图表**微操与重构**、PIE 精细控制、日志断言、depot diff / 资产历史 |
| **ue58-official-mcp**（UE5.8 官方） | HTTP **8000** `/mcp`（Streamable HTTP + SSE） | 23 toolset / ~180+ 工具 | 引擎**子系统广度**：物理资产、语义搜索、自动化测试、各类资产 CRUD、属性/类发现、AgentSkill、Python 编排 |

**两者端口/协议独立，可同时挂载、互不干扰。**

---

## 1. 官方 MCP 的调用范式（tool-search 模式）

官方 server 开了 `bEnableToolSearch=true`，`tools/list` 只暴露 3 个入口，能力靠按需发现：

1. `list_toolsets()` — 列出全部 23 个 toolset 及职责
2. `describe_toolset(toolset_name)` — 看某 toolset 的所有工具签名+schema
3. `call_tool(toolset_name, tool_name, arguments)` — 实际调用

**三步走**：`list_toolsets` 找方向 → `describe_toolset` 看参数 → `call_tool` 执行。

- 连接：CodeBuddy 全局 `mcp.json` 已配 `ue58-official-mcp` → `http://localhost:8000/mcp`（改配置后需重载会话）。
- 启动：项目 `Config/DefaultEditorPerProjectUserSettings.ini` 已设 `bAutoStartServer=True`，**开编辑器即自动起 server**。
- 手动兜底：编辑器控制台 `ModelContextProtocol.StartServer`。

---

## 2. 能力矩阵（谁强 / 谁独有）

| 领域 | ue-editor-mcp | 官方 MCP | 首选 |
|---|---|---|---|
| 蓝图创建/变量/函数/组件/接口/编译 | ✅ blueprint.* | ✅ BlueprintTools | 两者皆可 |
| 蓝图逐节点建图+连线 | ✅ node.add_*/graph.connect_nodes | ✅ add_node/connect_pins/set_node_pin_default | 两者皆可 |
| 蓝图**专门节点**(Event/CustomEvent/Dispatcher/Macro:ForEach·Gate·DoOnce/Cast/EnhancedInput) | ✅ 细分便捷 | ⚠ 靠通用 add_node(node_type) | **ue-editor-mcp** |
| 蓝图**图重构**(collapse→function/macro、自动排版、批量选中操作) | ✅ graph.collapse_*/layout.auto_*/batch_select_and_act | ❌ | **ue-editor-mcp** |
| EnhancedInput 节点/映射 | ✅ input.* | ❌ | **ue-editor-mcp** |
| PIE 精细控制 + 日志断言 + depot diff + 资产历史 | ✅ editor.assert_log/diff_against_depot/get_asset_history | ⚠ EditorApp 有基础 | **ue-editor-mcp** |
| UMG 控件树/属性/事件绑定 | ✅ widget.* | ✅ UMGToolSet(配合 ObjectTools) | 两者皆可 |
| 材质图编辑 | ✅ material.* | ✅ MaterialTools/MaterialInstanceTools | 两者皆可 |
| 通用属性 get/set/list + 类/子类发现 | ⚠ 分散 | ✅ ObjectTools（统一入口） | **官方** |
| 语义搜索(向量+BM25 搜资产/源码) | ❌ | ✅ SemanticSearch / AgentSkill.search_* | **官方** |
| 自动化测试(发现/运行/取结果) | ❌ | ✅ AutomationTest | **官方** |
| 物理资产创建/管理 | ❌ | ✅ PhysicsAssetToolset | **官方** |
| 各类资产 CRUD(DataTable/CurveTable/StringTable/Texture/Static·SkeletalMesh) | ⚠ 部分 | ✅ 专门 toolset | **官方** |
| Actor/场景/Outliner/相机 | ✅ editor.* | ✅ ActorTools/SceneTools | 两者皆可 |
| AgentSkill(多工具打包成技能) | ❌ | ✅ AgentSkillToolset | **官方** |
| Python 编排(沙盒脚本串联多工具批处理) | ❌ | ✅ ProgrammaticToolset | **官方** |

> 重要：官方 `BlueprintTools` 同样有逐节点 `add_node`/`connect_pins`/`set_node_pin_default`，**蓝图连线并非 ue-editor-mcp 独有**；ue-editor-mcp 的优势在「专门节点 + 图重构 + 排版 + 工程化审计」这一层。

> **2026-07-28 实测补充（AnimGraph / 动画蓝图节点）**：往 **AnimGraph** 加动画节点（如蒙太奇槽 `AnimGraphNode_Slot`、BlendSpace 播放器等）**只能用官方 MCP**。本地 ue-editor-mcp 的 `graph.apply_patch` 的 `add_node` 仅支持 Event/FunctionCall/Cast 等通用蓝图类型，`node.add_function_call` 只能建函数调用节点，均无动画图节点类；官方 `BlueprintTools` 提供了完整路径：`get_graph(blueprint, "AnimGraph")` → `find_node_types(graph, "slot", [])`（中文界面下 Slot 的 type_id 为 `Animation|Montage|Slot'DefaultSlot'`）→ `create_node(graph, type_id, pos)` → `connect_pins`（按 `EGPD_Output/EGPD_Input` + `index_id` 定位引脚，Slot 输入名 `Source`、输出名 `Pose`、Root 输入名 `Result`）→ `compile_blueprint`。本次用此路径给 `ABP_Pursuer_Locomotion` 插入 DefaultSlot（BlendSpace→Slot→Root）成功。**对象路径须带资产后缀**：`/Game/.../X.X`，仅 `/Game/.../X` 会报 "not a valid object path"。

---

## 3. 决策规则（按序匹配，选定 MCP）

1. **语义搜资产/源码** → 官方 `SemanticSearchToolset` / `AgentSkillToolset.search_*`
2. **跑/查自动化测试**（如 `Demo.PCG` 回归）→ 官方 `AutomationTestToolset`
3. **物理资产（PhysicsAsset）** → 官方 `PhysicsAssetToolset`
4. **通用对象属性 get/set/list、类/子类发现** → 官方 `ObjectTools`
5. **DataTable/CurveTable/StringTable/Texture/Mesh/MaterialInstance 等资产 CRUD** → 官方对应 toolset
6. **批量、需自定义逻辑串联多工具** → 官方 `ProgrammaticToolset`
7. **蓝图图表微操**（专门节点/collapse 重构/自动排版/批量选中）→ **ue-editor-mcp**
8. **PIE 精细控制 / 日志断言 / depot diff / 资产历史** → **ue-editor-mcp**
9. **蓝图基础 CRUD、UMG、材质、Actor/场景** → 两者皆可，**优先当前已连通、上下文已在用的那个**
10. 拿不准官方有没有某能力 → 先 `list_toolsets` + `describe_toolset` 查，别臆断"不支持"

---

## 4. 使用注意与已知坑

**官方 MCP（HTTP/SSE）**
- 响应是 SSE（`event: message` / `data: {json}`）；解析要拼接同一 message 的多个 `data:` 行再 `json.loads`，`text` 里换行是 JSON 转义 `\n`。
- **同一 session 连续多请求可能中途中断**（尤其首次触及 Python toolset）；批量枚举时**每个请求用独立 session**（重新 `initialize`）最稳。
- `editor_toolset.toolsets.*` 是 Python toolset，首次调用有初始化延迟。
- server 随编辑器进程存在；编辑器关了 server 就没了。

**ue-editor-mcp**
- 通过 `ue_actions_run` 执行；先 `ue_actions_search`（换多个关键词）再 `ue_actions_schema`，避免臆断"做不到"（见本目录 README 的确认流程）。
- 操作前先 `graph.describe` / `widget.get_tree` / `editor.list_assets` 读现状，只改缺的。

---

## 5. 官方 23 toolset 速览
- 顶层(C++)：AgentSkill / AutomationTest / EditorApp / Logs / PhysicsAsset / SemanticSearch / UMG
- EditorToolset 内(Python)：Actor / Asset / Blueprint / CurveTable / DataAsset / DataTable / Material / MaterialInstance / Object / Primitive / Scene / SkeletalMesh / StaticMesh / StringTable / Programmatic / Texture

完整签名见 `mcp_capability_overview.md`；BlueprintTools 全清单见 `mcp_describe_blueprint.md`（均在本目录）。
