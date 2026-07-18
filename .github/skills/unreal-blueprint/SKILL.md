---
name: ue-asset-generation
description: "Workflow for generating Blueprints, Widgets, Materials, Input Assets, and other UE assets via MCP. Covers requirement analysis, capability check, generation, layout cleanup, and post-generation best practices."
---

# Unreal Engine 5.7+ Asset Generation & Blueprint MCP Skill

> **上下文导航**
> - 仓库级 Copilot 指令：[`.github/copilot-instructions.md`](../../copilot-instructions.md)
> - AI Coding 唯一权威源：[`DOC/AI_Coding_Guide.md`](../../../DOC/AI_Coding_Guide.md)
> - 相关 Skill：[C++ Workflow](../unreal-cpp/SKILL.md) · [Material MCP](../unreal-material/SKILL.md)

This skill provides **everything needed** to generate Unreal Engine assets programmatically via **UEEditorMCP** — a Model Context Protocol plugin with persistent TCP connection and unified action-based architecture.

It combines:
- **Part A** — Structured 5-phase asset generation workflow
- **Part B** — Unified MCP architecture (7 tools + Action Registry)
- **Part C** — Complete action reference, pin names, API quirks

---

# Part A: Asset Generation Workflow

## When to Use

- User requests creation of a new Blueprint, Widget, Material, or Input asset
- User wants to set up a gameplay system requiring multiple interconnected assets
- User needs a complex Blueprint with event graphs, variables, components, and node chains
- User asks to prototype a UI, material shader, or input configuration

## Mandatory Workflow (5 Phases)

Every asset generation task MUST follow these phases in order.

---

### Phase 1: Requirement Analysis

Before touching any MCP tool, fully decompose what the user needs.

**Checklist:**
1. **Asset type** — Blueprint (Actor/Pawn/Character/GameMode/GameState/PlayerController)? Widget Blueprint? Material? Input Action/Mapping Context?
2. **Components** — What components does this asset need? (StaticMesh, Skeletal, Camera, Collision, Scene, etc.)
3. **Variables** — What state does it track? (Name, Type, Default, Exposed?)
4. **Event graph logic** — What happens on BeginPlay, Tick, or custom events? Map the execution flow as a chain.
5. **Function graphs** — Any reusable functions? (inputs/outputs/pure?)
  - If creating new methods/functions, define the final method names up front (clear, intent-driven, and consistent with existing project naming).
6. **Cross-asset dependencies** — Does it reference other Blueprints, Widgets, Materials, Data Assets? List them.
7. **Widget tree** (UMG only) — Canvas > Containers > Leaf widgets. Plan the hierarchy before creating.
8. **Material graph** (Materials only) — Which expressions connect to which outputs? Sketch the node graph.
9. **Input bindings** (Input only) — Which actions, keys, and modifiers?

**Output:** A structured plan listing every asset, component, variable, node, and connection to create.

---

### Phase 2: Capability Check

Cross-reference the plan against available actions.

**Use `ue_actions_search` to verify each capability exists.** If unsure, search first.

> ⚠️ **搜索失败时的强制流程**：如果 ue_actions_search 返回空或不符合预期，**禁止立即怀疑 MCP 不支持**。
> 必须先查阅 [MCP-SEARCH-GUIDE.md](../../../MCP-SEARCH-GUIDE.md)（换词表 + target 写法 + 宽泛词策略），
> 尝试至少 3 种不同搜索词后仍失败，再上报用户。


#### Supported Asset Types

| Asset Type | Status |
|------------|--------|
| Blueprint (Actor, Pawn, GameMode, PlayerController, etc.) | ✅ |
| Widget Blueprint (UMG) | ✅ |
| Material / Material Instance | ✅ |
| Input Action / Mapping Context | ✅ |
| Post-Process Volume | ✅ |
| Data Table | ❌ Manual |
| Animation Blueprint | ❌ Manual |
| Behavior Tree / Blackboard | ❌ Manual |
| Enum / Struct asset | ❌ Manual |
| Level Sequence | ❌ Manual |

#### Supported Operations (Summary)

- **Blueprint:** Components (6 types), Variables (5 types + defaults), Events, Custom Events, Function Calls, Function Graphs (+ local variables), Branch, Cast, Spawn Actor, Macros (ForLoop/DoOnce/etc.), Event Dispatchers, Component Event Binding, Enhanced Input, Make/Break Structs, Switch nodes, Comments, Parent Class, Interfaces
- **Connections:** Exec + data wiring, Pin defaults (including object refs via StaticLoadObject)
- **Patch System:** Declarative graph editing — `graph.apply_patch` (8 op types: add_node/remove_node/connect/disconnect/set_pin_default/add_variable/set_variable_default/set_node_property), `graph.validate_patch` (dry-run), `graph.describe_enhanced` (full PinType/variable refs/function signatures)
- **Introspection:** `graph.get_selected_nodes` (read selected nodes in focused editor — no args required), `graph.describe` (full graph topology), `graph.get_node_pins`, `graph.find_nodes`, `blueprint.get_summary`, `blueprint.describe_full` (single-call global Blueprint snapshot)
- **Selection Write:** `graph.set_selected_nodes` (programmatically set selection by node GUIDs, supports append mode), `graph.batch_select_and_act` (batch grouped selection + per-group action execution — enables fully automated split-by-purpose then collapse/comment workflows)
- **Refactor:** `graph.collapse_selection_to_function` (collapse current selected nodes into a new function using UE native flow), `graph.collapse_selection_to_macro` (collapse current selected nodes into a new macro — supports latent nodes like Delay, not for AnimGraph), `function.rename` (rename a custom function graph with full call-site reference update via engine-native `FBlueprintEditorUtils::RenameGraph`), `macro.rename` (rename a custom macro graph with instance reference update via `FBlueprintEditorUtils::RenameGraph`)
- **Cross-Graph Transfer:** `graph.export_nodes` (serialize selected nodes to text via `FEdGraphUtilities::ExportNodesToText`), `graph.import_nodes` (paste nodes from text into any compatible graph via `FEdGraphUtilities::ImportNodesFromText` + `PostProcessPastedNodes`) — enables node migration between EventGraph and function graphs
- **Widget (UMG):** 24 widget types, text/style, layout/slot, slider, combo box, reparent, event binding, text binding, image brush, button states, widget switching, background blur
- **MVVM:** ViewModel association (`mvvm_add_viewmodel`), property binding (`mvvm_add_binding` with 5 binding modes + 4 execution modes), binding introspection (`mvvm_get_bindings`), binding cleanup (`mvvm_remove_binding`), ViewModel cleanup (`mvvm_remove_viewmodel`)
- **Material:** 56+ expression types (incl. TextureParameter, StaticSwitch, MaterialFunction), properties, connections, output wiring, graph summary, auto-layout, auto-comment, remove expression
- **Layout:** Auto-layout by graph, subtree, or selection
- **Validation:** Compile Blueprint, Compile Material, Save All
- **Diagnostics:** For detailed Blueprint compiler messages (especially UMG/MVVM), use `editor.get_logs` or `ue_logs_tail(source="editor")` with `category="LogBlueprint"`, `min_verbosity="Error"`

#### Decision Gate

If any required capability is missing (❌):
1. Document the gap clearly
2. Propose adding the MCP command (see "Extending the MCP" in Part C)
3. **Request user authorization** before implementing
4. If authorized: declare action → implement → register → add Python tool → build → restart editor
5. Continue generation after the new tool is available

#### Blueprint Global Snapshot Gate (Mandatory)

Before any Blueprint node/variable/component/function/interface modification, you MUST fetch the Blueprint global snapshot first:

1. Call `blueprint.describe_full` for the target Blueprint.
2. Confirm response includes current summary + graph topology.
3. Only then continue with subsequent generation/patch logic.

Required behavior by scenario:
- **Existing Blueprint**: call `blueprint.describe_full` before any edit.
- **New Blueprint**: create + compile first, then call `blueprint.describe_full` as baseline, then continue graph logic.

This gate is non-optional for Blueprint tasks and prevents editing with stale graph/context assumptions.

---

### Phase 3: Asset Generation

Execute the plan in dependency order.

#### Execution Order (Critical)

```
1. Create assets (Blueprints, Widgets, Materials, Input Assets)
2. For every involved Blueprint, call `blueprint.describe_full` first (new BP: compile once before this baseline call)
3. Add components to Blueprints
4. Add variables to Blueprints / widgets to Widget Blueprints
5. Set component and widget properties
6. Add event/function graph nodes (top-to-bottom in exec order)
7. Connect exec pins (establish flow)
8. Connect data pins (wire values)
9. Set pin default values
10. Run scoped auto-layout only on selected or task-affected nodes/subtrees. Do not run full-graph/all-graphs layout on an existing Blueprint unless the task explicitly made graph-wide structural edits.
11. Add/adjust Blueprint comments via graph.auto_comment (preferred) or graph.add_comment (mandatory)
12. Compile all assets
```

**Per-change loop (mandatory):**
- Any time you modify nodes/wires/pins in a Blueprint graph, immediately repeat: **scoped auto-layout (changed selection/subtree) → re-create/adjust comments (prefer `graph.auto_comment` with node IDs) → compile**.
- Do not assume previous comments are still aligned after edits.

**Method naming rule (mandatory):**
- If you create a new method/function graph, assign a correct and descriptive name at creation time.
- If the initial name is temporary or ambiguous, rename it before delivery using `function.rename` or `macro.rename`.

**Why this order matters:**
- Components must exist before you can reference them in nodes
- Variables must exist before you can create Get/Set nodes
- Nodes must exist before you can connect their pins
- Target Blueprints must be compiled before `function.call` works
- Widget components must exist (and be variables) before `widget.bind_event` works

#### AI Workflow Pattern

**Fast Path (preferred — skip search/schema for known actions):**

**Blueprint preflight rule (mandatory):** before continuing Blueprint logic, run `blueprint.describe_full` to obtain global Blueprint information.

This SKILL.md contains the complete action reference in Part C with all action IDs, required params, and pin names. When you know the action ID and its parameters from Part C, **go directly to `ue_batch`**. This saves 2 MCP round-trips per operation.

```
# PREFERRED: Direct batch call — 1 MCP round-trip, 1 TCP round-trip
ue_batch([
  {"action_id": "blueprint.create", "params": {"name": "BP_X", "parent_class": "Actor"}},
  {"action_id": "blueprint.add_component", "params": {"blueprint_name": "BP_X", "component_type": "SceneComponent", "component_name": "Root"}},
  {"action_id": "variable.create", "params": {"blueprint_name": "BP_X", "variable_name": "Speed", "variable_type": "Float"}},
  {"action_id": "node.add_event", "params": {"blueprint_name": "BP_X", "event_name": "ReceiveBeginPlay"}},
  {"action_id": "blueprint.compile", "params": {"blueprint_name": "BP_X"}}
])
```

`ue_batch` now routes through C++ `batch_execute` — all actions execute in a **single TCP round-trip** (max 50 per batch). This is dramatically faster than individual `ue_actions_run` calls.

**Discovery Path (only when action is unknown):**
```
ue_actions_search("create blueprint")       → find action IDs
ue_actions_schema("blueprint.create")        → learn parameters
ue_actions_run("blueprint.create", {...})    → execute
```

**When to use which:**
- **Known action** (listed in Part C of this SKILL) → Fast Path via `ue_batch`
- **Unknown action** (new capability, not in Part C) → Discovery Path via `search → schema → run`
- **Single quick operation** → `ue_actions_run` directly (no search/schema needed if you know the params)

**Batch strategies for maximum efficiency:**
- Group all independent operations into a single `ue_batch` call
- Use `graph.apply_patch` (max 100 ops) for complex graph edits (add nodes + connect + set defaults in one call)
- Combine batch + patch: use `ue_batch` wrapping multiple `graph.apply_patch` calls for multi-graph operations

#### Request & Token Optimization Rules (Mandatory)

Use the following rules for all non-trivial Blueprint/UMG tasks to reduce request count, token cost, and retry loops.

1. **Run one Blueprint baseline per edit session**
  - Call `blueprint.describe_full` once before the first mutation of a target Blueprint.
  - Re-run it only when context changes materially (different Blueprint, graph, or unresolved drift after failures).
  - Do not repeatedly call `describe_full` between small edits.

2. **Use discovery only when action is unknown**
  - Known action + known params from Part C: call `ue_batch` / `ue_actions_run` directly.
  - Unknown action only: `ue_actions_search` → `ue_actions_schema` → execute.

3. **Prefer batched writes and declarative patches**
  - Combine related writes into one `ue_batch` call whenever possible.
  - For multi-node graph changes, prefer `graph.validate_patch` + `graph.apply_patch` over many one-off node/connect calls.

4. **Use token-light introspection by default**
  - Prefer targeted reads (`graph.get_node_pins`, `graph.find_nodes`, `blueprint.get_summary`) for local checks.
  - If enhanced graph dump is needed, use `graph.describe_enhanced(compact=true)` first.
  - Escalate to full pin details only when type ambiguity remains.

5. **Constrain asset scans**
  - For `editor.list_assets`, always set narrow `path` and filters (`class_filter`, `name_contains`).
  - Always set `max_results` to a bounded value (recommended: `<= 50` unless explicitly required).

6. **Front-load known validation pitfalls**
  - Use fully-qualified class paths where applicable (for example subsystem classes under `/Script/...`) to avoid avoidable retries.
  - For patch-heavy edits, run `graph.validate_patch` before `graph.apply_patch`.

7. **Keep verification scoped to changed logic**
  - Validate only affected nodes/wires with targeted `get_node_pins` checks, then compile and save.
  - Avoid broad re-introspection of unrelated graphs after a localized edit.

8. **Retry with a bounded loop**
  - After a failed action: inspect error → inspect pins/state → retry with corrected params.
  - If still failing after 2 focused retries, switch strategy (for example from one-off calls to patch, or vice versa) instead of repeating the same call pattern.

#### Node Positioning Strategy

Use a consistent grid before the layout pass:
- **Horizontal:** 300-400 px per exec step
- **Vertical:** 150-200 px per parallel branch
- Example chain: `[0,0]` → `[400,0]` → `[800,0]` → `[1200,0]`

#### Error Recovery

If a tool call fails:
1. Read the error message carefully
2. Use `ue_actions_run("graph.get_node_pins", ...)` to inspect actual pin names
3. Use `ue_actions_run("graph.find_nodes", ...)` to verify current graph state
4. Use `ue_actions_run("graph.get_selected_nodes", ...)` to inspect nodes currently selected in the editor (supports no-args fallback to focused editor)
5. Use `ue_actions_run("blueprint.get_summary", ...)` to check asset state
6. If `blueprint.compile` returns `status=Error` with low-detail output, immediately fetch `editor.get_logs(count=200, category="LogBlueprint", min_verbosity="Error")`
7. Retry with corrected parameters
8. If the failure is due to a missing MCP capability, go back to Phase 2

#### Workflow Templates

Use compact templates and avoid repeating large examples here.

- For concrete JSON snippets, use **Quick Reference: Minimal Asset Templates** in this file.
- For multi-node logic, prefer declarative patching:
  - `graph.validate_patch({ops:[...]})` for dry-run validation
  - `graph.apply_patch({blueprint_name, ops:[...]})` for atomic apply + auto-compile
- Use individual `node.*` / `graph.*` actions for one-off edits and inspections.

---

### Phase 4: Layout & Organization

After all nodes are created and connected, clean up the visual layout.

**Auto-Layout Scope Guard (mandatory, single source of truth):**
- Use `layout.auto_selected` with `mode: "selected"` (preferred) or `layout.auto_subtree` for normal edits.
- Only use `layout.auto_selected` with `mode: "graph"` or `mode: "all"` when the task explicitly changes graph-wide structure.
- Goal: preserve untouched regions and avoid accidental whole-Blueprint layout disruption.

**Mandatory rules:**
1. every generated or modified Blueprint graph must run auto-layout **BEFORE** adding any comment boxes, following the **Auto-Layout Scope Guard** above;
2. comment boxes **SHOULD** be created using `graph.auto_comment` (preferred) which auto-calculates bounding box from node IDs, or `graph.add_comment` (manual) with explicit position/size;
3. **every completed Blueprint** (new or modified) must contain comment boxes for ALL major logic blocks — this is a **non-optional deliverable**;
4. after **every subsequent graph edit** (node add/delete/move, reconnect/disconnect, pin default change), run auto-layout again and then re-adjust existing comment boxes (or add new ones) to match the latest node positions;
5. **comment boxes MUST NOT overlap** each other — maintain at least 60px gap between adjacent comments;
6. **comment boxes MUST be color-coded** by functional category (see Color Scheme below).
7. **comment_text MUST be Chinese** for all generated comments.
8. **each comment_text MUST be concise and no longer than one sentence**.

> **CRITICAL ORDER: Layout first, then comments.**
> Comment nodes (`EdGraphNode_Comment`) are NOT moved by auto-layout. If comments are placed before layout, they will be stranded at their original coordinates while logic nodes are repositioned — causing a mismatch. Always follow:
> 1. Apply the **Auto-Layout Scope Guard** (smallest valid scope first; graph/all modes only for explicit graph-wide edits)
> 2. Use `graph.auto_comment` with `node_ids` — automatically reads node positions/sizes and creates a perfectly-sized comment (recommended)
> 3. Or use `graph.add_comment` with manually calculated position/size (fallback)

#### Recommended: `graph.auto_comment` (auto-sized comments)

```
# Provide node GUIDs → comment auto-calculates bounding box + padding:
ue_actions_run("graph.auto_comment", {
  blueprint_name: "BP_X",
  graph_name: "EventGraph",
  node_ids: ["GUID_A", "GUID_B", "GUID_C"],
  comment_text: "初始化流程：BeginPlay 到 Setup",
  color: [0.15, 0.55, 0.25, 1],   // Forest Green for initialization
  padding: 40,                     // optional, default 40px
  title_height: 36                 // optional, default 36px
})
→ Returns: {node_id, position, size, nodes_wrapped}
```

This eliminates the need to peek node positions via `graph.move_node` trick. The comment will precisely wrap all specified nodes with configurable padding.

#### Fallback: Manual `graph.add_comment`

When you need exact control over position/size, use `graph.add_comment` directly:

**How to read node positions (move_node trick):**
```
# "Peek" a node's position without actually moving it:
ue_actions_run("graph.move_node", {node_id, node_position: [99999, 99999]})  → returns old_position
ue_actions_run("graph.move_node", {node_id, node_position: old_position})    → move it back
# Then use old_position to compute comment box placement.
```

**Comment box sizing guide:**

| Nodes covered | Recommended size |
|---------------|-----------------|
| 1-2 nodes (single chain) | `[600, 240]` |
| 3-5 nodes (one exec chain) | `[1200, 300]` |
| Branching logic block | `[1600, 520]` |

Comment `node_position` should be offset `[-32, -80]` from the top-left node to leave room for the title text.

#### Comment Color Scheme (Mandatory)

Every comment box **MUST** have a `color` parameter set according to its functional category. Use RGBA format `[R, G, B, A]` with values in 0-1 range.

| Functional Category | Color Name | RGBA Value | Usage |
|---------------------|------------|------------|-------|
| Tick / Update logic | Steel Blue | `[0.15, 0.35, 0.65, 1]` | Event Tick, timer callbacks, per-frame updates |
| Initialization | Forest Green | `[0.15, 0.55, 0.25, 1]` | BeginPlay, Construction Script, setup logic |
| Input / Interaction | Dark Gray | `[0.45, 0.45, 0.45, 1]` | Overlap events, input handling, collision |
| Calculation / Sampling | Amber Gold | `[0.75, 0.6, 0.1, 1]` | Math, random, data generation, pure logic blocks |
| Movement / Transform | Coral Orange | `[0.8, 0.35, 0.15, 1]` | SetActorLocation, interpolation, physics |
| AI / Facing / Targeting | Cyan | `[0.1, 0.55, 0.55, 1]` | LookAt, AI decision, targeting logic |
| UI / HUD | Purple | `[0.5, 0.2, 0.7, 1]` | Widget creation, HUD updates, UI events |
| Combat / Damage | Crimson Red | `[0.7, 0.15, 0.15, 1]` | Damage dealing, health, death, combat flow |
| Audio / VFX | Teal | `[0.2, 0.5, 0.45, 1]` | Sound playback, particle effects, visual feedback |
| Spawning / Lifecycle | Slate Blue | `[0.3, 0.35, 0.55, 1]` | Actor spawning, pooling, destroy logic |

> If a logic block doesn't fit any category above, pick the closest match. Never leave a comment without a color.

#### Comment Non-Overlap Rules

- **Minimum gap:** 60px between adjacent comment boxes (both X and Y axes)
- **Overlap check:** Before placing a new comment, verify no existing comment's bounding box intersects
- **If overlap detected:** Shift the new comment or resize existing ones to maintain the minimum gap
- **Read actual positions:** Use `graph.move_node` peek trick on boundary nodes to get real coordinates before computing comment placement

#### Recommended Comment Granularity

- Event entry block (BeginPlay/Tick/Input)
- Core gameplay logic block
- Utility/calculation block
- Output/apply-effects block

Comment text style rule:
- Use short Chinese labels only (for example: "输入门控", "伤害结算", "UI刷新").
- Keep each comment to one sentence maximum.

For menu-entry Blueprints (especially `BP_GameStartMenuGameMode`), apply graph-mode layout only when the task made graph-wide edits; otherwise keep scoped layout (selected/subtree):

```
ue_actions_run("layout.auto_selected", {
  "blueprint_name": "BP_GameStartMenuGameMode",
  "mode": "graph",
  "graph_name": "EventGraph",
  "layer_spacing": 600,
  "row_spacing": 180
})
```

| Scope | Action |
|-------|--------|
| Created/modified nodes (preferred default) | `layout.auto_selected` with `mode: "selected"` and explicit `node_ids` |
| Affected exec chain / subtree | `layout.auto_subtree` with `root_node_id` |
| Entire event graph | `layout.auto_selected` with `mode: "graph"` |
| All graphs in Blueprint | `layout.auto_selected` with `mode: "all"` |
| Dense graphs | Add `layer_spacing: 600, row_spacing: 180` |

Layout algorithm: exec nodes arranged horizontally along chains, branches create new rows, pure nodes placed left of consumers, collision detection prevents overlap. All operations support Undo (Ctrl+Z).

---

### Phase 5: Validation & Delivery

#### Compile & Save

```
ue_batch([
  {"action_id": "blueprint.compile", "params": {"blueprint_name": "BP_MyActor"}},
  {"action_id": "material.compile",  "params": {"material_name": "M_MyMaterial"}},
  {"action_id": "editor.save_all",   "params": {}}
])
```

- `error_count == 0` → proceed
- `error_count > 0` → read error details, fix, recompile

#### Verification Checklist

- [ ] All assets created and visible in Content Browser
- [ ] Components attached with correct properties
- [ ] Variables defined with correct types
- [ ] Exec chains fully connected (no dangling pins)
- [ ] Data wires carry correct types
- [ ] Pin defaults set for literals and object references
- [ ] Widget hierarchy matches design (verify with `widget.get_tree`)
- [ ] All assets compile without errors
- [ ] Graph layout is clean and readable
- [ ] Auto-layout was applied only to created/modified nodes (or affected subtree) unless a full-graph pass was explicitly required
- [ ] After the **last** Blueprint edit, auto-layout has been rerun
- [ ] Comment boxes were re-positioned or added **after** that last auto-layout pass
- [ ] Any newly created method/function has a correct, descriptive final name
- [ ] **Every logic block** in every graph has a comment box (no uncommented logic blocks)
- [ ] **All comment boxes have a color** set per the Color Scheme table
- [ ] **All comment texts are in Chinese**
- [ ] **Each comment text is one sentence maximum and concise**
- [ ] **No comment boxes overlap** each other (minimum 60px gap maintained)
- [ ] `BP_GameStartMenuGameMode` has been auto-laid out after node edits (when involved in the task)

#### Report to User

1. **Summary** — What was created (assets, components, variables, nodes)
2. **Asset locations** — Content paths
3. **How to use** — Steps to test in editor (PIE, assign to level, etc.)
4. **Manual steps** — Things that couldn't be automated
5. **Known limitations** — Features MCP doesn't support yet

---

## Post-Generation Best Practices

1. **Compile in editor** — Verify compile status in the Blueprint toolbar after MCP compilation
2. **Test in PIE** — Play In Editor to verify runtime behavior
3. **Save the level** — If actors were spawned, save the level separately
4. **SVN caution** — Generated Blueprints produce large binary diffs; review before committing
5. **Widget Z-Order** — Set appropriate Z-Order if multiple widgets overlap
6. **Input Mode** — When showing interactive UI, set Input Mode (UI Only / Game And UI) and show mouse cursor
7. **GC Protection** — Store widget references in UPROPERTY variables to prevent garbage collection

## Common Pitfalls

| Pitfall | Prevention |
|---------|------------|
| CreateWidget returns None | Widget Blueprint must exist and be compiled first |
| Function call shows "Error" | Compile target Blueprint before creating call nodes |
| Cast always fails | Verify the object is actually the target type at runtime |
| Dispatcher doesn't fire | Ensure Bind runs before Call (typically in BeginPlay) |
| ComboBox dropdown empty | Populate options in Event Construct via AddOption |
| Enhanced Input not responding | Add IMC in BeginPlay via AddMappingContext |
| Material appears black | All required material outputs must be connected |
| Widget not visible | Verify AddToViewport called and Visibility = Visible |
| Nodes overlap | Run auto_layout after all nodes are placed |
| Pin connection fails | Use `graph.get_node_pins` to verify exact pin names |
| ScrollBox not scrolling | Ensure children are added via reparent/add_widget_child |
| WidgetSwitcher wrong page | Set active_widget_index via widget.set_properties |
| BackgroundBlur invisible | Set blur_strength > 0 and place behind content in Z-order |
| Button no hover feedback | Set button_normal/hovered/pressed_color via widget.set_properties |
| Image blank | Set brush_texture via widget.set_properties with valid asset path |
| Comments no longer wrap logic blocks after edits | Re-run auto-layout, then use `graph.auto_comment` with node IDs to re-create well-sized comments |
| Comment boxes overlap each other | Ensure 60px minimum gap; use `graph.auto_comment` for precise sizing; shift/resize as needed |
| Comments all same color (no visual distinction) | Apply Color Scheme from Phase 4 — each functional category gets a distinct color |
| Comment doesn't fully cover all nodes | Use `graph.auto_comment` instead of manual sizing — it reads actual node sizes via Slate widget |

## Quick Reference: Minimal Asset Templates

### Actor Blueprint
```json
{"actions": [
  {"action_id": "blueprint.create", "params": {"name": "BP_X", "parent_class": "Actor"}},
  {"action_id": "blueprint.add_component", "params": {"blueprint_name": "BP_X", "component_type": "SceneComponent", "component_name": "Root"}},
  {"action_id": "node.add_event", "params": {"blueprint_name": "BP_X", "event_name": "ReceiveBeginPlay"}},
  {"action_id": "blueprint.compile", "params": {"blueprint_name": "BP_X"}},
  {"action_id": "layout.auto_selected", "params": {"blueprint_name": "BP_X", "mode": "graph"}},
  {"action_id": "graph.auto_comment", "params": {"blueprint_name": "BP_X", "graph_name": "EventGraph", "node_ids": ["<BeginPlay_GUID>"], "comment_text": "初始化流程", "color": [0.15, 0.55, 0.25, 1]}}
]}
```

### Widget Blueprint
```json
{"actions": [
  {"action_id": "widget.create", "params": {"widget_name": "WBP_X"}},
  {"action_id": "widget.add_component", "params": {"widget_name": "WBP_X", "component_type": "CanvasPanel", "component_name": "Root"}},
  {"action_id": "widget.add_component", "params": {"widget_name": "WBP_X", "component_type": "TextBlock", "component_name": "Title", "text": "Hello"}},
  {"action_id": "widget.reparent", "params": {"widget_name": "WBP_X", "target_container_name": "Root", "children": ["Title"]}},
  {"action_id": "blueprint.compile", "params": {"blueprint_name": "WBP_X"}}
]}
```

### Material
```json
{"actions": [
  {"action_id": "material.create", "params": {"material_name": "M_X"}},
  {"action_id": "material.add_expression", "params": {"material_name": "M_X", "expression_class": "VectorParameter", "node_name": "Color", "properties": {"DefaultValue": {"R":1, "G":0, "B":0, "A":1}}}},
  {"action_id": "material.connect_to_output", "params": {"material_name": "M_X", "source_node": "Color", "material_property": "BaseColor"}},
  {"action_id": "material.compile", "params": {"material_name": "M_X"}}
]}
```

### Enhanced Input
```json
{"actions": [
  {"action_id": "input.create_action", "params": {"name": "IA_Jump", "value_type": "Boolean"}},
  {"action_id": "input.create_mapping_context", "params": {"name": "IMC_Default"}},
  {"action_id": "input.add_key_mapping", "params": {"context_name": "IMC_Default", "action_name": "IA_Jump", "key": "SpaceBar"}}
]}
```

---

# Part B: Unified MCP Architecture (7 Fixed Tools + Optional Logs Server)

## Overview

One MCP server (`ue-editor-mcp`) exposes exactly **7 tools** that never change. All UE actions are accessed through an **Action Registry** using a `search → schema → run` workflow.

Additionally, the repository supports an **optional parallel server** `ue-editor-mcp-logs` with three tools: `unreal.logs.get`, `unreal.asset_thumbnail.get`, and `unreal.asset_diff.get`. This server is independent of the 7-tool contract and does not change Action Registry behavior.

**Why this design:**
- VS Code has a global tool slot budget (~128). With 6 servers × ~18 tools = ~108 MCP tools, many became unreachable.
- A single server with 7 fixed tools stays well within limits, while supporting unlimited underlying actions.
- AI discovers actions dynamically — no need to memorize tool names.

## The 7 Tools

| # | Tool | Purpose |
|---|------|---------|
| 1 | `ue_ping` | Test connection to Unreal Engine. Returns `{pong: true}` if alive. |
| 2 | `ue_actions_search` | Search actions by keyword/tags. Returns ranked list of action IDs. |
| 3 | `ue_actions_schema` | Get full input schema, examples, metadata for one action. |
| 4 | `ue_actions_run` | Execute a single action with parameters. |
| 5 | `ue_batch` | Execute multiple actions in a **single TCP round-trip** (max 50). Uses C++ `batch_execute` internally. |
| 6 | `ue_resources_read` | Read embedded docs: `conventions.md`, `error_codes.md`, `patch_spec.md`. |
| 7 | `ue_logs_tail` | Tail recent command log (action, success/fail, timing). |

## Optional Parallel Logs Server

When tasks need stable, token-controlled log context (especially when UE may be offline), use `ue-editor-mcp-logs`:

| Server | Tool | Purpose |
|---|---|---|
| `ue-editor-mcp-logs` | `unreal.logs.get` | Unified log retrieval with `mode=auto|live|saved`, cursor incremental reads, byte/line limits, and `offline_saved` fallback when UE is unreachable. |
| `ue-editor-mcp-logs` | `unreal.asset_thumbnail.get` | Return selected/specified asset thumbnails as `image/png` base64; supports `assetPath/assetPaths/assetIds/ids`, returns `thumbnails[]`, size clamped to `<=256`. |
| `ue-editor-mcp-logs` | `unreal.asset_diff.get` | Return structured source-control diffs for assets (Blueprint node-level changes and generic property-level changes). |

Recommended defaults:
- `tailLines=200`
- `maxBytes=65536`
- Persist returned `cursor` and pass it in subsequent calls to avoid repeated context flooding.

## AI Workflow

Use **Part A / Phase 3** as the single workflow source of truth:
- Follow **AI Workflow Pattern** for Fast Path vs Discovery Path decisions.
- Follow **Request & Token Optimization Rules** for request budgeting and retries.
- Use **Part C** for complete, non-duplicated action/category reference.

## Architecture

```
AI (Copilot)
  │  7 fixed MCP tools via stdio
    ▼
server_unified.py (Action Registry + dispatcher)
    │  JSON over TCP (port 55558)
    ▼
MCPBridge.cpp (command router → FEditorAction handlers; count evolves with registry)
    │
    ▼
Validation → Execution → Post-Validation → Auto-Save

Optional parallel path:

AI (Copilot)
  │  `unreal.logs.get` / `unreal.asset_thumbnail.get` / `unreal.asset_diff.get`
  ▼
server_unreal_logs.py (`ue-editor-mcp-logs`)
  ├─ logs live: `get_unreal_logs` (C++ ring buffer)
  ├─ logs saved/offline_saved: tail `Saved/Logs` from disk
  ├─ thumbnails: `get_selected_asset_thumbnail` (C++ action)
  └─ asset diff: `diff_against_depot` (C++ action, Source Control)
```

### Key Files

| File | Purpose |
|------|---------|
| `server_unified.py` | Single MCP server, 7 tools, action dispatch |
| `server_unreal_logs.py` | Standalone logs + thumbnail + diff MCP server (`unreal.logs.get`, `unreal.asset_thumbnail.get`, `unreal.asset_diff.get`) |
| `registry/__init__.py` | ActionRegistry class, keyword search engine |
| `registry/actions.py` | ActionDef registry with schemas/tags/examples (count evolves) |
| `resources/*.md` | Embedded documentation for `ue_resources_read` |
| `connection.py` | Persistent TCP connection with heartbeat/reconnect |

---

# Part C: Complete Action Reference & API Details

## Action Categories — Full Reference

### Blueprint Management (11 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|-----------------|
| `blueprint.create` | `name`, `parent_class` | `path` |
| `blueprint.compile` | `blueprint_name` | — |
| `blueprint.set_property` | `blueprint_name`, `property_name`, `property_value` | — |
| `blueprint.spawn_actor` | `blueprint_name`, `actor_name` | `location`[xyz], `rotation`[pyr] |
| `blueprint.set_parent_class` | `blueprint_name`, `parent_class` | — |
| `blueprint.add_interface` | `blueprint_name`, `interface_name` | — |
| `blueprint.remove_interface` | `blueprint_name`, `interface_name` | — |
| `blueprint.add_component` | `blueprint_name`, `component_type`, `component_name` | `location`, `rotation`, `scale`, `component_properties`{} |
| `blueprint.get_summary` | — | `blueprint_name`, `asset_path` |
| `blueprint.describe_full` | — | `blueprint_name`, `asset_path`, `include_pin_details`(bool), `include_function_signatures`(bool) |
| `blueprint.create_colored_material` | `material_name` | `color`[RGB], `path` |

> **`blueprint.describe_full`** — Single-call comprehensive snapshot. Returns summary (variables, components, interfaces, compile status) + ALL graph topologies (nodes, pins, edges) in one response. Default compact mode omits full PinType details to reduce token usage. Set `include_pin_details=true` for full type info. **Replaces: 1× `blueprint.get_summary` + N× `graph.describe`.**

> This is the MCP global Blueprint information endpoint and is sufficient to output one Blueprint's full structure (including variables, functions/method graphs, interfaces, components, and node topology).

> **Quirk:** `blueprint.add_component` uses param **`component_type`** (not `component_class`).

### Component Properties (3 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|-----------------|
| `component.set_property` | `blueprint_name`, `component_name`, `property_name`, `property_value` | — |
| `component.set_static_mesh` | `blueprint_name`, `component_name` | `static_mesh`, `material`, `overlay_material` |
| `component.set_physics` | `blueprint_name`, `component_name` | `simulate_physics`, `gravity_enabled`, `mass`, `linear_damping`, `angular_damping` |

### Editor / Level (12 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|-----------------|
| `editor.get_actors` | — | — |
| `editor.find_actors` | `pattern` | — |
| `editor.spawn_actor` | `name`, `type` | `location`[xyz], `rotation`[pyr] |
| `editor.delete_actor` | `name` | — |
| `editor.set_actor_transform` | `name` | `location`[xyz], `rotation`[pyr], `scale`[xyz] |
| `editor.get_actor_properties` | `name` | — |
| `editor.set_actor_property` | `name`, `property_name`, `property_value` | — |
| `editor.focus_viewport` | — | `target`, `location`[xyz], `distance`, `orientation`[pyr] |
| `editor.get_viewport_transform` | — | — |
| `editor.set_viewport_transform` | — | `location`[xyz], `rotation`[pyr] |
| `editor.save_all` | — | — |
| `editor.list_assets` | `path` | `recursive`, `class_filter`, `name_contains`, `max_results` |

### Layout (2 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|-----------------|
| `layout.auto_selected` | — | `mode`(selected/graph/all), `blueprint_name`, `graph_name`, `node_ids`[], `layer_spacing`, `row_spacing` |
| `layout.auto_subtree` | — | `root_node_id`, `blueprint_name`, `graph_name`, `max_pure_depth`, `layer_spacing`, `row_spacing` |

### Node — Events (4 actions)

| Action ID | Required Params | Key Params |
|-----------|----------------|------------|
| `node.add_event` | `blueprint_name`, `event_name` | ReceiveBeginPlay, ReceiveTick, etc. |
| `node.add_custom_event` | `blueprint_name`, `event_name` | `parameters`[{name,type}] |
| `node.add_input_action` | `blueprint_name`, `action_name` | Legacy input |
| `node.add_enhanced_input_action` | `blueprint_name`, `action_name` | `action_path`; provides Started/Triggered/Ongoing/Canceled/Completed exec pins |

> **Quirk:** `node.add_event` uses param **`event_name`** (not `event_type`).

### Node — Event Dispatchers & Component Events (5 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|------------------|
| `dispatcher.create` | `blueprint_name`, `dispatcher_name` | `parameters`[{name,type}] |
| `dispatcher.call` | `blueprint_name`, `dispatcher_name` | `node_position`[XY], `graph_name` |
| `dispatcher.bind` | `blueprint_name`, `dispatcher_name` | `target_blueprint`, `node_position`[XY], `graph_name` |
| `dispatcher.create_event` | `blueprint_name`, `function_name` | `connect_to_node_id`, `connect_to_pin`, `node_position`[XY], `graph_name` |
| `component.bind_event` | `blueprint_name`, `component_name`, `event_name` | `node_position`[XY], `graph_name` |

> **`component.bind_event`:** Creates a `UK2Node_ComponentBoundEvent` for any ActorComponent delegate (e.g., `OnComponentBeginOverlap`, `OnTTSEnvelope`). The component must exist as a UPROPERTY on the Blueprint (SCS-added or C++ parent).

### Node — Functions (6 actions)

| Action ID | Required Params | Notes |
|-----------|----------------|-------|
| `node.add_function_call` | `blueprint_name`, `target`, `function_name` | `target`: class name for statics (KismetSystemLibrary, GameplayStatics) or "self" |
| `node.add_spawn_actor` | `blueprint_name`, `class_to_spawn` | `node_position`[XY], `graph_name` |
| `node.set_pin_default` | `blueprint_name`, `node_id`, `pin_name`, `default_value` | Object pins auto-call StaticLoadObject |
| `node.set_object_property` | `blueprint_name`, `owner_class`, `property_name` | `node_position`, `graph_name` |
| `node.add_get_subsystem` | `blueprint_name`, `subsystem_class` | `node_position`, `graph_name` |
| `function.create` | `blueprint_name`, `function_name` | `inputs`/`outputs`[{name,type}], `is_pure`; returns entry_node_id + result_node_id |
| `function.call` | `blueprint_name`, `target_blueprint`, `function_name` | Target BP must be compiled first |
| `function.delete` | `blueprint_name`, `function_name` | — |

### Node — Variables (4 actions)

| Action ID | Required Params |
|-----------|----------------|
| `variable.create` | `blueprint_name`, `variable_name`, `variable_type` | `is_exposed` bool; types: Boolean, Integer, Float, Vector, String |
| `variable.add_getter` | `blueprint_name`, `variable_name` |
| `variable.add_setter` | `blueprint_name`, `variable_name` |
| `variable.add_local` | `blueprint_name`, `function_name`, `variable_name`, `variable_type` |

### Node — References & Casting (3 actions)

| Action ID | Required Params |
|-----------|----------------|
| `node.add_self_reference` | `blueprint_name` |
| `node.add_component_reference` | `blueprint_name`, `component_name` |
| `node.add_cast` | `blueprint_name`, `target_class` | `pure_cast` bool |

### Node — Flow Control (3 actions)

| Action ID | Required Params |
|-----------|----------------|
| `node.add_branch` | `blueprint_name` |
| `node.add_sequence` | `blueprint_name` | Native K2 Execution Sequence node (NOT a macro) |
| `node.add_macro` | `blueprint_name`, `macro_name` | ForEachLoop, ForLoop, WhileLoop, DoOnce, Gate |

### Struct & Switch (4 actions)

| Action ID | Required Params |
|-----------|----------------|
| `node.add_make_struct` | `blueprint_name`, `struct_type` | `pin_defaults`{}, `node_position`[XY], `graph_name` |
| `node.add_break_struct` | `blueprint_name`, `struct_type` | `node_position`[XY], `graph_name` |
| `node.add_switch_string` | `blueprint_name` | `cases`[], `node_position`[XY], `graph_name` |
| `node.add_switch_int` | `blueprint_name` | `start_index`, `cases`[], `node_position`[XY], `graph_name` |

Supported struct types: IntPoint, Vector, Vector2D, Rotator, Transform, LinearColor, Color.

### Graph Operations (13 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|------------------|
| `graph.connect_nodes` | `blueprint_name`, `source_node_id`, `source_pin`, `target_node_id`, `target_pin` | `graph_name` |
| `graph.find_nodes` | `blueprint_name` | `graph_name`, `node_type`, `event_type` |
| `graph.delete_node` | `blueprint_name`, `node_id` | `graph_name` |
| `graph.get_node_pins` | `blueprint_name`, `node_id` | `graph_name` |
| `graph.collapse_selection_to_function` | — | `blueprint_name` — Collapse currently selected nodes in focused Blueprint graph into a new function (uses native UE command path). |
| `graph.collapse_selection_to_macro` | — | `blueprint_name` — Collapse currently selected nodes in focused Blueprint graph into a new macro (uses native UE `CollapseSelectionToMacro` command). Returns `created_macro`, `macro_count_before`, `macro_count_after`, `created_macros`[]. Macros support latent nodes (Delay etc.); not supported in AnimGraphs. |
| `graph.set_selected_nodes` | `node_ids`[] | `blueprint_name`, `graph_name`, `append`(bool, default false) — Programmatically set graph editor selection by node GUIDs. Use `append: true` to add to existing selection. |
| `graph.batch_select_and_act` | `groups`[] | `blueprint_name`, `graph_name` — Batch grouped selection + per-group action. Each group: `{node_ids[], action, action_params?}`. Enables fully automated split-by-purpose workflows (e.g. collapse each group to function, or wrap each group with auto_comment). |
| `graph.disconnect_pin` | `blueprint_name`, `node_id`, `pin_name` | `graph_name` |
| `graph.move_node` | `blueprint_name`, `node_id`, `node_position`[XY] | `graph_name` |
| `graph.add_reroute` | `blueprint_name` | `node_position`[XY], `graph_name` |
| `graph.add_comment` | `blueprint_name`, `comment_text` | `graph_name`, `node_position`[XY], `size`[WH], `color`[RGBA 0-1] — **MUST set color per Color Scheme** |
| `graph.auto_comment` | `blueprint_name`, `node_ids`[], `comment_text` | `graph_name`, `color`[RGBA 0-1], `padding`(default 40), `title_height`(default 36) — **Preferred: auto-calculates bounding box from node positions/sizes** |
| `graph.describe_enhanced` | `blueprint_name` | `graph_name`, `compact`(bool, default false) — Enhanced graph dump: full `FEdGraphPinType` serialization, variable reference tracking, function signature inline, node metadata. **Use `compact: true` to omit metadata/function_signature/variable_references and use lightweight pin serialization (50-100KB → 10-20KB for large graphs).** |
| `graph.apply_patch` | `blueprint_name`, `ops`[] | `graph_name`, `continue_on_error` — Declarative patch: 8 op types (add_node/remove_node/set_node_property/connect/disconnect/add_variable/set_variable_default/set_pin_default). Supports temp IDs, auto-compile. Max 100 ops. |
| `graph.validate_patch` | `blueprint_name`, `ops`[] | `graph_name` — Dry-run validation of a patch document without modifying the graph. Returns per-op results. |
| `graph.export_nodes` | `blueprint_name`, `node_ids`[] | `graph_name` — Serialize nodes to text (read-only). Returns `exported_text` string for later import. Uses `FEdGraphUtilities::ExportNodesToText`. |
| `graph.import_nodes` | `blueprint_name`, `exported_text` | `graph_name`, `offset_x`, `offset_y` — Import (paste) nodes from text into target graph. Uses `FEdGraphUtilities::ImportNodesFromText` + `PostProcessPastedNodes`. Returns array of imported node IDs/classes/positions. |

### Variable & Function Management (4 actions)

| Action ID | Required Params |
|-----------|----------------|
| `variable.set_default` | `blueprint_name`, `variable_name`, `default_value` |
| `variable.delete` | `blueprint_name`, `variable_name` |
| `variable.rename` | `blueprint_name`, `old_name`, `new_name` |
| `variable.set_metadata` | `blueprint_name`, `variable_name` | `category`, `tooltip`, `instance_editable`, `blueprint_read_only`, `expose_on_spawn`, `replicated`, `private` |
| `function.rename` | `blueprint_name`, `function_name`, `new_name` | Rename a custom function graph. Uses engine-native `FBlueprintEditorUtils::RenameGraph` — automatically updates all `K2Node_CallFunction` call-site references, local variable scopes, and function entry/result nodes. Returns `old_name`, `new_name`, `exact_match`. |
| `macro.rename` | `blueprint_name`, `macro_name`, `new_name` | Rename a custom macro graph. Uses engine-native `FBlueprintEditorUtils::RenameGraph` — automatically updates all macro instance node references. Returns `old_name`, `new_name`, `exact_match`. |

### Materials (13 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|-----------------|
| `material.create` | `material_name` | `path`, `domain`, `blend_mode` |
| `material.add_expression` | `material_name`, `expression_class`, `node_name` | `position`[XY], `properties`{} |
| `material.connect_expressions` | `material_name`, `source_node`, `target_node`, `target_input` | `source_output_index` |
| `material.connect_to_output` | `material_name`, `source_node`, `material_property` | `source_output_index` |
| `material.set_expression_property` | `material_name`, `node_name`, `property_name`, `property_value` | — |
| `material.compile` | `material_name` | — |
| `material.create_instance` | `instance_name`, `parent_material` | `path`, `scalar_parameters`{}, `vector_parameters`{}, `texture_parameters`{}, `static_switch_parameters`{} |
| `material.set_property` | `material_name`, `property_name`, `property_value` | — |
| `material.create_post_process_volume` | `name` | `location`[XYZ], `infinite_extent`, `priority`, `post_process_materials`[] |
| `material.get_summary` | `material_name` | — |
| `material.remove_expression` | `material_name` | `node_name`(single), `node_names`[](batch) |
| `material.auto_layout` | `material_name` | `layer_spacing`, `row_spacing` |
| `material.auto_comment` | `material_name`, `comment_text` | `node_names`[], `color`[RGBA 0-1], `padding`(default 40) |

> **`material.get_summary`** — Single-call material graph snapshot. Returns all expressions (name/type/position/properties), all connections, material-level properties (Domain/BlendMode/ShadingModel), and comments. Use for debugging and AI self-inspection.

> **`material.remove_expression`** — Destructive. Disconnects all connections (including material outputs), removes from ExpressionCollection, unregisters from context. Supports single or batch removal.

> **`material.auto_layout` + `material.auto_comment`** — Material graph equivalents of Blueprint `layout.auto_selected` + `graph.auto_comment`. Always run `auto_layout` first, then `auto_comment`.

**Expression classes (56+):** SceneTexture, SceneDepth, ScreenPosition, TextureCoordinate, TextureSample, WorldPosition, CameraPosition, VertexNormalWS · Add, Subtract, Multiply, Divide, Power, SquareRoot, Abs, Min, Max, Clamp, Saturate, Floor, Ceil, Frac, OneMinus, Step, SmoothStep · Sin, Cos · DotProduct, CrossProduct, Normalize, AppendVector, ComponentMask · Constant, Constant2/3/4Vector · ScalarParameter, VectorParameter, **TextureParameter**, **TextureObjectParameter**, **TextureSampleParameter2D**, **StaticSwitchParameter**, **StaticComponentMaskParameter** · **MaterialFunctionCall** · Noise, Time, Panner · DDX, DDY · If, Lerp · Custom (HLSL)

**Expression pin names:**

| Expression | Inputs |
|------------|--------|
| Add/Subtract/Multiply/Divide/DotProduct/Min/Max | A, B |
| Power | Base, Exponent |
| Lerp | A, B, Alpha |
| Clamp | Input, Min, Max |
| If | A, B, AGreaterThanB, AEqualsB, ALessThanB |
| ComponentMask/SquareRoot/Abs | Input |
| Noise | Position, FilterWidth |
| Panner | Coordinate, Time, Speed |
| Custom | Dynamic (matches Inputs array names) |

**Material outputs:** BaseColor, EmissiveColor, Metallic, Roughness, Specular, Normal, Opacity, OpacityMask, AmbientOcclusion, WorldPositionOffset, Refraction

**ShadingModel values:** Unlit, DefaultLit, Subsurface, PreintegratedSkin, ClearCoat, SubsurfaceProfile, TwoSidedFoliage, Hair, Cloth, Eye

### UMG Widgets (19 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|-----------------|
| `widget.create` | `widget_name` | `parent_class`, `path` |
| `widget.delete` | `widget_name` | — |
| `widget.add_component` | `widget_name`, `component_type`, `component_name` | `text`, `font_size`, `color`[RGBA], `position`[XY], `size`[WH] |
| `widget.bind_event` | `widget_name`, `widget_component_name`, `event_name` | — |
| `widget.add_to_viewport` | `widget_name` | `z_order` |
| `widget.set_text_binding` | `widget_name`, `text_block_name`, `binding_property` | `binding_type` |
| `widget.list_components` | `widget_name` | — |
| `widget.get_tree` | `widget_name` | — |
| `widget.set_properties` | `widget_name`, `target` | `position`[XY], `size`[WH], `visibility`, `is_enabled`, `h_align`, `v_align`, `padding`[LTRB] |
| `widget.set_text` | `widget_name`, `target` | `text`, `font_size`, `color`[RGBA], `justification` |
| `widget.set_combo_options` | `widget_name`, `target` | `mode`(replace/add/remove/clear), `options`[], `selected_option` |
| `widget.set_slider` | `widget_name`, `target` | `value`, `min_value`, `max_value`, `step_size`, `locked` |
| `widget.reparent` | `widget_name`, `target_container_name` | `container_type`, `children`[], `filter_class` |
| `widget.add_child` | `widget_name`, `child`, `parent` | — |
| `widget.delete_component` | `widget_name`, `target` | — |
| `widget.rename_component` | `widget_name`, `target`, `new_name` | — |

### MVVM Actions (3 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|-----------------|
| `widget.mvvm_add_viewmodel` | `widget_name`, `viewmodel_class` | `viewmodel_name`, `creation_type`(CreateInstance/Manual/GlobalViewModelCollection/PropertyPath/Resolver), `create_setter`, `create_getter` |
| `widget.mvvm_add_binding` | `widget_name`, `viewmodel_name`, `source_property`, `destination_widget`, `destination_property` | `binding_mode`(OneTimeToDestination/OneWayToDestination/TwoWay/OneTimeToSource/OneWayToSource), `execution_mode`(Immediate/Delayed/Tick/Auto) |
| `widget.mvvm_get_bindings` | `widget_name` | — |

**MVVM Workflow (typical):**
```
# 1. Create Widget Blueprint with UI elements
widget.create → widget.add_component (TextBlock, ProgressBar, etc.)

# 2. Associate ViewModel class (must implement INotifyFieldValueChanged)
widget.mvvm_add_viewmodel(widget_name="WBP_HUD", viewmodel_class="StatusViewModel", viewmodel_name="StatusVM")

# 3. Add property bindings
widget.mvvm_add_binding(widget_name="WBP_HUD", viewmodel_name="StatusVM",
    source_property="HealthPercent", destination_widget="HealthBar",
    destination_property="Percent", binding_mode="OneWayToDestination")

# 4. Inspect existing bindings
widget.mvvm_get_bindings(widget_name="WBP_HUD")

# 5. Compile
blueprint.compile(blueprint_name="WBP_HUD")
```

**Prerequisites:**
- The ModelViewViewModel plugin must be enabled in the project
- The ViewModel C++ class must derive from `UMVVMViewModelBase` or implement `INotifyFieldValueChanged`
- The ViewModel class must be compiled before it can be associated
- Widget components must exist in the Widget Tree before binding destinations

**Supported component_type values (24):**
TextBlock, Button, Image, Border, Overlay, HorizontalBox, VerticalBox, Slider, ProgressBar, SizeBox, ScaleBox, CanvasPanel, ComboBox, CheckBox, SpinBox, EditableTextBox, ScrollBox, WidgetSwitcher, BackgroundBlur, UniformGridPanel, Spacer, RichTextBlock, WrapBox, CircularThrobber

**Widget property detail (set_widget_properties):**
- **Slot (CanvasPanel parent):** `position`[XY], `size`[WH], `anchors`[minX,minY,maxX,maxY], `alignment`[XY], `z_order`, `auto_size`
- **Slot (VBox/HBox parent):** `padding`[LTRB], `h_align`(Fill/Left/Center/Right), `v_align`(Fill/Top/Center/Bottom), `size_rule`(Auto/Fill)
- **Slot (Overlay parent):** `padding`, `h_align`, `v_align`
- **Render:** `render_scale`[XY], `render_angle`, `render_shear`[XY], `render_translation`[XY], `render_pivot`[XY]
- **General:** `visibility`(Visible/Hidden/Collapsed/HitTestInvisible/SelfHitTestInvisible), `is_enabled`
- **Image:** `brush_texture`(asset path), `brush_size`[WH], `color_and_opacity`[RGBA]
- **Button:** `button_normal_color`[RGBA], `button_hovered_color`[RGBA], `button_pressed_color`[RGBA]
- **WidgetSwitcher:** `active_widget_index`
- **BackgroundBlur:** `blur_strength`

### Input System (4 actions)

| Action ID | Required Params | Optional Params |
|-----------|----------------|-----------------|
| `input.create_mapping` | `action_name`, `key` | `input_type`(Action/Axis), `scale` |
| `input.create_action` | `name` | `value_type`(Boolean/Axis1D/Axis2D/Axis3D), `path` |
| `input.create_mapping_context` | `name` | `path` |
| `input.add_key_mapping` | `context_name`, `action_name`, `key` | `modifiers`[](Negate, SwizzleYXZ, etc.), `context_path`, `action_path` |

---

## Pin Names by Node Type

**K2Node_Event (BeginPlay/Tick):** `then` (exec out), `Delta Seconds` (Tick only)

**K2Node_CallFunction:** `execute` (exec in), `then` (exec out), `self` (target), `ReturnValue`, parameter-name pins

**K2Node_IfThenElse:** `Execute` (exec in), `Condition` (bool in), `Then` (exec out), `Else` (exec out)

**K2Node_VariableSet:** `execute` (exec in), `then` (exec out), variable-name (input), `Output_Get` (output)

**K2Node_VariableGet:** variable-name (output)

**K2Node_EnhancedInputAction:** `Started`, `Triggered`, `Ongoing`, `Canceled`, `Completed` (exec outs), `ActionValue`

**K2Node_DynamicCast:** `execute`→`then`/`CastFailed` (exec), `Object` (input), `As [ClassName]` (output)

**K2Node_SpawnActorFromClass:** `execute`→`then`, `Class`, `Spawn Transform`, `Collision Handling Override`, `Owner`, `ReturnValue`

**K2Node_CallFunction (Custom BP):** `execute`/`then`/`self` + input/output pins matching function signature

## UE 5.7 Function Name Rules

- Math: `Add_DoubleDouble`, `Subtract_DoubleDouble`, `Multiply_DoubleDouble`, `GreaterEqual_DoubleDouble` (not `_FloatFloat`)
- Movement: `K2_AddActorWorldOffset`, `K2_SetActorLocation`, `K2_GetActorLocation`

**Static class targets:**
- `KismetMathLibrary` — math ops
- `KismetSystemLibrary` — PrintString, etc.
- `GameplayStatics` — GetPlayerController, spawning
- `EnhancedInputLocalPlayerSubsystem` — Enhanced Input

## Object Pin Default Values

Format: `/Game/Path/AssetName.AssetName` — `node.set_pin_default` auto-calls `StaticLoadObject`.

## Common Patterns

### Enhanced Input Setup (PlayerController)
```json
[
  {"action_id": "node.add_event", "params": {"blueprint_name": "BP_PC", "event_name": "ReceiveBeginPlay"}},
  {"action_id": "node.add_self_reference", "params": {"blueprint_name": "BP_PC"}},
  {"action_id": "node.add_get_subsystem", "params": {"blueprint_name": "BP_PC", "subsystem_class": "EnhancedInputLocalPlayerSubsystem"}},
  {"action_id": "node.add_function_call", "params": {"blueprint_name": "BP_PC", "target": "EnhancedInputLocalPlayerSubsystem", "function_name": "AddMappingContext"}}
]
```

### CreateWidget + AddToViewport
```json
[
  {"action_id": "node.add_function_call", "params": {"blueprint_name": "BP_X", "target": "WidgetBlueprintLibrary", "function_name": "Create"}},
  {"action_id": "widget.add_to_viewport", "params": {"widget_name": "WBP_HUD"}}
]
```

### Event Dispatcher Communication
```json
[
  {"action_id": "dispatcher.create", "params": {"blueprint_name": "BP_Source", "dispatcher_name": "OnDoorOpened"}},
  {"action_id": "dispatcher.call", "params": {"blueprint_name": "BP_Source", "dispatcher_name": "OnDoorOpened"}},
  {"action_id": "dispatcher.bind", "params": {"blueprint_name": "BP_Listener", "dispatcher_name": "OnDoorOpened"}}
]
```

### Create Event in Function Graph (delegate binding without CustomEvent)
```json
[
  {"action_id": "dispatcher.create_event", "params": {
    "blueprint_name": "BP_Player",
    "function_name": "OnTTSEnvelope",
    "connect_to_node_id": "<bind-node-guid>",
    "connect_to_pin": "Event",
    "graph_name": "SetupTTS"
  }}
]
```

### Cross-Graph Node Migration (Event → Function pattern)

When encounter event node whose downstream logic should be moved into a separate function (e.g., converting EventGraph inline logic into a bound function for an Event Dispatcher), use this workflow:

**Scenario:** EventGraph has an event node (e.g., `OnComponentBeginOverlap`) with many downstream nodes. You need to:
1. Move the downstream logic into a new function
2. Bind the event to call that function via Create Event delegate

**Method A — Collapse to Function (preferred, simpler)**
```
1. blueprint.describe_full          → get global snapshot
2. graph.describe_enhanced          → identify nodes to migrate
3. graph.set_selected_nodes         → select the downstream nodes (exclude the event node itself)
4. graph.collapse_selection_to_function → UE auto-creates function, replaces inline nodes with function call
   (alternative: graph.collapse_selection_to_macro → collapses to macro instead; macros support latent nodes like Delay, but not available in AnimGraphs)
5. function.rename                  → give the function a proper name (or macro.rename for macros)
6. dispatcher.bind (binding_mode="function", function_name=<new_name>) → bind event to function
7. auto_layout + auto_comment + compile
```

**Method B — Export / Import (full control, arbitrary target graph)**
```
1. blueprint.describe_full          → get global snapshot
2. graph.describe_enhanced          → identify downstream node IDs (exclude event node)
3. graph.export_nodes               → serialize nodes to text
   params: { blueprint_name, node_ids: [<downstream-guids>], graph_name: "EventGraph" }
4. function.create                  → create target function graph
   params: { blueprint_name, function_name, inputs/outputs matching event signature }
5. graph.import_nodes               → paste nodes into new function graph
   params: { blueprint_name, exported_text: <from step 3>, graph_name: <new_function_name> }
6. graph.delete_node (×N)           → delete original nodes from EventGraph
7. graph.connect_nodes              → wire function entry to imported nodes in function graph
8. dispatcher.create_event / dispatcher.bind → bind event to new function
9. auto_layout + auto_comment + compile
```

**When to use which:**
- **Method A** — downstream nodes form a single linear chain, no need for custom function signature
- **Method B** — need to target an existing function graph, or need precise control over import position/wiring, or nodes span multiple disconnected chains

> **Important:** `graph.export_nodes` preserves internal connections between exported nodes. Connections to non-exported nodes (e.g., the event node itself) are NOT preserved and must be manually rewired after import.

## Extending the MCP

When a capability is missing, extend the plugin:

1. **Declare** action class in `Public/Actions/[Category]Actions.h` (inherit `FBlueprintNodeAction`)
2. **Implement** `Validate()` + `ExecuteInternal()` in `.cpp`
3. **Register** in `MCPBridge.cpp`: `ActionHandlers.Add(TEXT("name"), MakeShared<FAction>())`
4. **Add ActionDef** in `registry/actions.py` with matching schema and tags

**Key helpers:** `GetRequiredString`, `GetTargetBlueprint`, `GetTargetGraph`, `GetNodePosition`, `MarkBlueprintModified`, `RegisterCreatedNode`, `CreateSuccessResponse`

**Node creation:** Use `FEdGraphSchemaAction_K2NewNode::SpawnNode<T>(Graph, Position, Flags, InitLambda)`

**Class resolution:** `"BP_Enemy"` → asset registry; `"/Game/Blueprints/BP_Enemy"` → direct load; `"Actor"` → `/Script/Engine.Actor`

### Action Class Hierarchy
```
FEditorAction (base)
├── FBlueprintAction → FBlueprintNodeAction
├── FViewportAction, FLevelAction
├── FProjectAction
└── FUMGAction
```

### C++ Action Files
| File | Scope |
|------|-------|
| `BlueprintActions.cpp` | create, compile, components, materials, parent_class, interfaces |
| `EditorActions.cpp` | Actors, viewport, save_all, assets, layout |
| `GraphActions.cpp` | Patch system (apply/validate), enhanced graph describe, cross-graph transfer (export/import) |
| `NodeActions.cpp` | 40+ node types |
| `ProjectActions.cpp` | Enhanced Input assets |
| `UMGActions.cpp` | 24 widget types, properties, hierarchy |

---

# Part D: UE Bridge — Python API

## Overview

`ue_bridge.py` is a Python module that exposes MCPBridge commands as typed Python methods.
It connects directly to UE MCPBridge over TCP 55558 and can be used as a CLI fallback for the unified MCP server.

**File path**: `Plugins/UEEditorMCP/Python/ue_bridge.py`
**Python interpreter**: `Plugins/UEEditorMCP/Python/.venv/Scripts/python.exe`

## When to Use

1. **Bulk operations** — execute many Blueprint operations in one scripted flow (create, wire, compile)
2. **Scripted pipelines** — generate assets with reusable Python scripts
3. **Interactive debugging** — test commands one by one in a REPL-like workflow
4. **MCP server unavailable** — UE is running but VS Code is not connected to MCP

> **Note**: Prefer MCP tools (`ue_actions_run` / `ue_batch`) during normal operation; no CLI fallback is needed in most cases.
> The unified MCP server uses only 7 tool slots and is designed to avoid VS Code tool-budget limitations.

## LLM Invocation via `run_in_terminal`

### Piped parameters (recommended to avoid PowerShell escaping issues)
```powershell
'{"blueprint_name":"BP_X","node_id":"GUID","pin_name":"Value","default_value":"42"}' | & "D:/UE5projects/Demo/Plugins/UEEditorMCP/Python/.venv/Scripts/python.exe" "D:/UE5projects/Demo/Plugins/UEEditorMCP/Python/ue_bridge.py" set_node_pin_default
```

### Batch Python script (LLM creates a `.py` file, then executes it)
```python
# setup_enemy.py
import sys; sys.path.insert(0, r"D:/UE5projects/Demo\Plugins\UEEditorMCP\Python")
from ue_bridge import UEBridge

ue = UEBridge()
ue.create_blueprint("BP_Enemy", "Actor")
ue.add_component_to_blueprint("BP_Enemy", "StaticMeshComponent", "Mesh")
ue.compile_blueprint("BP_Enemy")
ue.save_all()
ue.close()
```

## Decision Logic

```
Need to call an MCP action?
  ├── MCP server connected? → Use `ue_actions_run` / `ue_batch` (preferred)
  ├── MCP server not connected, but UE is running?
  │    ├── Single command? → Use `run_in_terminal` + `ue_bridge.py` CLI
  │    └── Multiple commands? → Create a `.py` script, import `UEBridge`, execute once
  └── Command missing? → Extend MCPBridge (Part C → Extending the MCP)
```

---

## Memory MCP — 跨会话记忆管理（强制）

> 权威源：`DOC/AI_Coding_Guide.md` §1.2 | 详细规则：`.github/copilot-instructions.md` §2

**所有对 `memory-bank/` 和 `.ai-context/` 的读写必须通过 Memory MCP 工具执行，禁止直接文件操作。**

### 本 Skill 相关的记忆更新时机

| 场景 | 操作 |
|------|------|
| 创建/修改 Blueprint/Widget/Input 资产 | `memory_write("memory-bank/progress.md", ...)` |
| 建立 MCP 资产生成新模式 | `memory_write("memory-bank/systemPatterns.md", ...)` |
| MCP Action 架构变更 | `memory_write("memory-bank/techContext.md", ...)` |
| 遇到 MCP/Blueprint 编译错误 | `memory_write(".ai-context/latest-error.md", ...)` |
| 多步资产生成任务进行中 | `memory_write(".ai-context/current-task.md", ...)` |
| 会话结束 | `memory_write("memory-bank/activeContext.md", ...)` |

### 工具速查

```
memory_get(path)              → 读取记忆文件
memory_search(query)          → 关键词搜索
memory_guard_check()          → 写入前容量检查
memory_write(path, content)   → 受控写入
memory_compact(path, policy)  → 超限时压缩
memory_backup(path)           → 重大变更前备份
```
