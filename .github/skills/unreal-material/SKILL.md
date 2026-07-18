---
name: unreal-material
description: "Focused workflow for creating and editing Unreal Engine Material assets via UEEditorMCP actions."
---

# Unreal Material MCP Skill (UE 5.7)

> **上下文导航**
> - 仓库级 Copilot 指令：[`.github/copilot-instructions.md`](../../copilot-instructions.md)
> - AI Coding 唯一权威源：[`DOC/AI_Coding_Guide.md`](../../../DOC/AI_Coding_Guide.md)
> - 相关 Skill：[C++ Workflow](../unreal-cpp/SKILL.md) · [Blueprint/Widget MCP](../unreal-blueprint/SKILL.md)

This skill is dedicated to **Material asset authoring** through UEEditorMCP.
Use it when the task is about creating, wiring, compiling, or validating Material graphs.

## Scope

Use this skill for:
- Creating new Material assets
- Adding Material expressions (including `Custom` HLSL nodes)
- Wiring expressions to each other and to Material outputs (for example `BaseColor`)
- Setting expression/material properties
- Auto layout, commenting, compile, and summary checks

Do not use this skill for:
- Blueprint Event Graph logic
- UMG widget hierarchy work
- C++ module/build/UHT issues

## Required Preconditions

1. UE editor is connected and responsive:
   - `ue_ping` should return `{ "pong": true }`
2. Action existence is confirmed when uncertain:
   - `ue_actions_search(query="material")`
3. Parameter schema is confirmed for unknown actions:
   - `ue_actions_schema(action_id="material.add_expression")`

## Core Actions

Primary action IDs used by this skill:
- `material.create`
- `material.add_expression`
- `material.connect_expressions`
- `material.connect_to_output`
- `material.set_expression_property`
- `material.set_property`
- `material.auto_layout`
- `material.auto_comment`
- `material.compile`
- `material.get_summary`
- `material.remove_expression`
- `material.apply_to_component`
- `material.apply_to_actor`
- `material.refresh_editor`
- `editor.save_all`

## Standard Workflow

1. **Create** the material
2. **Add expressions** (constants, parameters, math ops, texture samples, or `Custom`)
3. **Wire graph** (`connect_expressions`, then `connect_to_output`)
4. **Set properties** (node-level or material-level)
5. **Layout then comment** (`material.auto_layout` -> `material.auto_comment`)
6. **Compile** (`material.compile`)
7. **Refresh editor** (`material.refresh_editor`) — if the material editor is open, call this to make all changes visible without closing/reopening
8. **Inspect** (`material.get_summary`) and ensure expected connections exist
9. **Apply to level objects when needed** (`material.apply_to_component` / `material.apply_to_actor`)
10. **Save** (`editor.save_all`)

## Compile Diagnostics Contract (P5)

`material.compile` now returns real compile diagnostics after waiting for shader compilation to finish.

Expected response fields:
- `success`
- `material_name`
- `compiled`
- `error_count`
- `warning_count`
- `errors[]`

Each entry in `errors[]` may include:
- `message`
- `expression_name`
- `expression_class`
- `node_name`

## Custom Node Pattern (Red BaseColor)

Reference sequence:

1. `material.create`
2. `material.add_expression` with:
   - `expression_class: "Custom"`
   - `node_name: "RedCustomNode"`
   - `properties.Code: "return float3(1.0, 0.0, 0.0);"`
   - `properties.OutputType: "CMOT_Float3"`
3. `material.connect_to_output` with:
   - `source_node: "RedCustomNode"`
   - `material_property: "BaseColor"`
4. `material.compile`
5. `material.get_summary` to verify the `$output.BaseColor` link

## UObject Reference Properties (Texture, MaterialFunction)

`material.set_expression_property` supports UObject reference properties:

- **Texture**: Set a texture on an existing TextureSample / TextureObjectParameter node.
  ```
  material.set_expression_property(node_name="MySampler", property_name="Texture",
    property_value="/Engine/EditorShellMaterials/T_BaseButton.T_BaseButton")
  ```

**推荐默认贴图**：当 TextureSample 需要指定占位贴图时，统一使用 `/Engine/EditorShellMaterials/T_BaseButton.T_BaseButton`（彩色棋盘格，便于视觉确认采样是否生效）。避免使用 `T_Base`（纯灰色，难以区分采样结果）。
- **MaterialFunction**: Set a material function on a MaterialFunctionCall node (uses `SetMaterialFunction` internally).
  ```
  material.set_expression_property(node_name="MyFuncCall", property_name="MaterialFunction",
    property_value="/Engine/Functions/Engine_MaterialFunctions03/Procedurals/Noise_Perlin")
  ```

Path resolution: full asset path is tried first; if not found, `/Game/` prefix is auto-prepended.
Type safety: loaded object class is validated against the property's expected class before assignment.

**Note**: `material.add_expression` `properties.Texture` also supports texture assignment at creation time. Use `set_expression_property` when you need to change the texture on an already-created node.

## Troubleshooting

- If an action fails with missing parameters, check schema and resend with full `params`.
- If compile fails, use `material.compile` response `errors[]` first, then inspect logs and re-check node/property names.
- If output looks incorrect, verify connection target (`BaseColor` vs `EmissiveColor`) and Custom code return type.
- Parameter naming guard: do not use `None` / `NAME_None` (or empty strings) for `node_name` or any material parameter key (`scalar_parameters` / `vector_parameters` / `texture_parameters` / `static_switch_parameters`). Use stable explicit names like `AlbedoTex`, `RoughnessScale`.

## Output Contract for Agent Responses

When finishing a material task, report:
- Asset path (for example `/Game/Demo/M_RedCustom`)
- Expressions created (names + classes)
- Output connections made
- Compile result (errors/warnings)
- Any capability gaps if the requested operation is not supported

## AI Prompt Templates (English)

Use the following templates directly in IMA or any LLM tool. Template A is for pure math logic in Custom nodes. Template B is for multi-sample effects that require texture/screen sampling.

## Mandatory Execution Rules for Custom Requests

These rules are mandatory and override agent preference when a task involves a `Custom` node.

1. **Template selection is required before implementation**
   - If the task is math-only in `Custom`, use **Template A**.
   - If the task requires sampling in `Custom`, use **Template B**.

2. **Prompt language must be English**
   - The template prompt used for generating `Custom` HLSL must be written in English.
   - Keep variable names/pin names exactly aligned with Unreal pin names (case-sensitive).

3. **When user explicitly requires sampling inside `Custom`**
   - The agent **must not** move sampling outside `Custom` as a substitution.
   - For regular texture sampling, use combination **A (Surface + TextureObject sampling)**.
   - Ensure `TextureObject` inputs are passed into `Custom` and sampled via `Texture2DSample(...)` in `Custom` code.

4. **Delivery contract for Custom tasks**
   - State which template was used (A or B) and why.
   - State which sampling combination was used (A/B/C/D) when sampling is involved.
   - If implementation deviates from the user’s explicit requirement, stop and report the constraint instead of silently changing approach.

### Template A — Standard Custom Node (No Texture Sampling)

```text
You are an Unreal Engine material/HLSL engineer.

Goal:
Generate compilable HLSL for an Unreal Material Custom node to implement: [DESCRIBE EFFECT].

Hard requirements:
1) Do NOT sample any texture inside the Custom node.
2) Perform math only on inputs passed from the material graph (Color/UV/parameters/etc.).
3) Use a struct wrapper exactly in this style:

struct Functions {
   float3 Core(/* inputs */) {
      // pure math only
      return /* result */;
   }

   float3 Out(/* inputs */) {
      return Core(/* same inputs */);
   }
};

Functions f;
return f.Out(/* Custom node inputs */);

4) Keep input names case-sensitive and consistent with Unreal Custom node pins.
5) Ensure return type and Custom node Output Type match.

Output format (strict order):
1) Full HLSL code (paste-ready)
2) Output Type for Unreal Custom node (CMOT Float1/2/3/4)
3) Inputs table with columns:
   Name | Unreal Material input type | Purpose | Recommended default | Connection plan in graph

Do not output extra explanation outside these 3 sections.
```

### Template B — Texture Sampling Custom Node (Blur/Sharpen/Glow)

```text
You are an Unreal Engine material/HLSL engineer.

Goal:
Generate compilable HLSL for an Unreal Material Custom node to implement: [DESCRIBE EFFECT] with multi-sampling (Blur/Sharpen/Glow/etc.).

1) Choose exactly ONE pipeline and follow it strictly:
A) Surface + TextureObject sampling
B) Surface (Translucent) + SceneColor sampling
C) PostProcess + PostProcessInput0 sampling
D) PostProcess + TextureObject sampling

2) Stability rules (mandatory):
- Do NOT use struct wrapper for sampling code.
- Do NOT place sampling statements inside helper/member functions.
- Keep all sampling statements flattened in main scope near return.
- Use fixed, countable taps (prefer <= 16) and symmetric offsets (+offset/-offset).
- Reuse sampled/blurred result when possible (avoid duplicate center sampling).

3) Node-side prerequisites (must be reflected in your output):
- For B: Material Domain must be Surface (Translucent), and a SceneColor node must exist in graph.
- For C: Material Domain must be PostProcess, and SceneTexture(PostProcessInput0) must exist in graph.
  Recommended: pass SceneTexture Color as CenterColor and InvSize as InvSize input to Custom.

4) Output format (strict order):
1. Full HLSL code (paste-ready)
2. Output Type for Custom node (CMOT Float1/2/3/4)
3. Inputs table:
   Name | Unreal Material input type | Purpose | Default | Connection plan
4. Sampling budget summary:
   - whether center pixel is additionally sampled
   - taps count
   - total sampling operations per pixel

Do not output extra explanation outside these 4 sections.
```

## Critical Notes for Sampling Scenarios

- Dependency injection matters: if the material graph does not contain required scene texture expressions, Custom code helpers/resources may not be injected.
- For PostProcess sampling, `PostProcessInput0` is commonly accessed with SceneTexture Id `14` in UE5 workflows; validate per engine version when needed.
- If you see errors like undeclared `SceneTextureLookup`, check domain/pipeline selection and whether prerequisite nodes were placed in graph.
- For UV correctness in post process, prefer engine helper conversions to scene texture space when available in current version.

## Sampling Combination Matrix (English)

| Combination | Material Domain | Required Input Node / Prerequisite | Core Sampling HLSL | Typical Use Cases |
| :--- | :--- | :--- | :--- | :--- |
| **Non-PostProcess material sampling regular texture** | Surface | TextureObject provided by material graph | `Texture2DSample(Tex, TexSampler, UV)` | Detail enhancement, triplanar, texture filtering |
| **Non-PostProcess material sampling screen texture** | Surface (Translucent) | A `SceneColor` node must exist in graph (even as a warm-up/injection trigger) | `DecodeSceneColorAndAlpharForMaterialNode(UV)` | Heat haze, frosted glass, refraction/distortion |
| **PostProcess material sampling screen texture** | PostProcess | A `SceneTexture(PostProcessInput0)` node must exist in graph (even if sampled once) | `SceneTextureLookup(UV, 14, false)` | CRT, blur, sharpen, glow |
| **PostProcess material sampling regular texture** | PostProcess | TextureObject (noise/LUT/mask) | `Texture2DSample(Tex, TexSampler, UV)` | Film grain, LUT, scanline, mask overlays |

## Dependency Injection and Auto-Insert Rule

When the requested effect uses sampling, the agent must automatically add required graph-side sampling nodes based on the selected combination before final compile.

Automatic node insertion policy:

1. **Surface + TextureObject (A)**
   - Ensure a TextureObject path exists in graph and is passed to Custom inputs.

2. **Surface (Translucent) + SceneColor (B)**
   - Ensure material domain/blend supports translucent scene sampling.
   - Auto-add a `SceneColor` expression node if missing.
   - Keep it connected as an injection trigger path when needed by the graph setup.

3. **PostProcess + PostProcessInput0 (C)**
   - Ensure material domain is `PostProcess`.
   - Auto-add a `SceneTexture` expression with `PostProcessInput0` if missing.
   - Prefer passing node outputs (for example `Color` as `CenterColor`, `InvSize` as `InvSize`) into Custom to avoid redundant center sampling.

4. **PostProcess + TextureObject (D)**
   - Ensure a TextureObject path exists in graph and is wired to Custom inputs.

Validation requirements before delivery:
- Compile must succeed.
- `material.get_summary` should confirm required sampling nodes exist for the chosen pipeline.
- Final response must state which combination (A/B/C/D) was used and which prerequisite nodes were auto-inserted.

---

## Memory MCP — 跨会话记忆管理（强制）

> 权威源：`DOC/AI_Coding_Guide.md` §1.2 | 详细规则：`.github/copilot-instructions.md` §2

**所有对 `memory-bank/` 和 `.ai-context/` 的读写必须通过 Memory MCP 工具执行，禁止直接文件操作。**

### 本 Skill 相关的记忆更新时机

| 场景 | 操作 |
|------|------|
| 创建/修改 Material 资产 | `memory_write("memory-bank/progress.md", ...)` |
| 建立 Material MCP 新模式/模板 | `memory_write("memory-bank/systemPatterns.md", ...)` |
| 遇到材质编译错误 | `memory_write(".ai-context/latest-error.md", ...)` |
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
