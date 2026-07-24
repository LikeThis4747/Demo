# Level Static Mesh Component Binder MCP 工具

## 目的

为当前编辑器关卡中的 Actor 实例批量绑定 Static Mesh，避免在素材实拼阶段按 Outliner 分组逐个进入细节面板设置 Mesh。

工具不区分墙、地板、天花板或 Trim，也不写死任何素材包路径；这些对象对工具而言都是 `UStaticMesh` 资产。

## 位置

- 动作 ID：`editor.set_static_mesh_component`
- C++ 声明：`Plugins/UEEditorMCP/Source/UEEditorMCP/Public/Actions/EditorActions.h`
- C++ 实现：`Plugins/UEEditorMCP/Source/UEEditorMCP/Private/Actions/EditorActions.cpp`
- UE 动作注册：`Plugins/UEEditorMCP/Source/UEEditorMCP/Private/MCPBridge.cpp`
- MCP Schema：`Plugins/UEEditorMCP/Python/ue_editor_mcp/registry/actions.py`

## 接口

```json
{
  "actor_names": [
    "PCG_StyleB_Wall_01",
    "PCG_StyleB_Wall_02"
  ],
  "static_mesh": "/Game/SciFiHydroLab/Meshes/Walls/SM_HydroLab_WallB1.SM_HydroLab_WallB1"
}
```

- `actor_names`：当前关卡中 Actor 的准确对象名；同一调用内不得重复。
- `static_mesh`：Static Mesh 的包路径或完整对象路径。

## 实现方法

1. 在修改前加载并验证 Static Mesh。
2. 解析整批 Actor；每个 Actor 必须恰好拥有一个 `UStaticMeshComponent`。
3. Actor 没有组件或拥有多个匹配组件时明确失败，不猜测目标。
4. 使用一个编辑器 Undo Transaction 包裹整批修改。
5. 对 Actor 与组件调用 `Modify()`，再通过正式的 `SetStaticMesh()` 设置资源。
6. 设置后回读组件并验证 Mesh 引用。
7. 将当前关卡标记为 Dirty，但不自动保存其他 Dirty Package。

## 适用范围

- 普通 `AStaticMeshActor`。
- 仅拥有一个 `UStaticMeshComponent` 的关卡 Blueprint/C++ Actor 实例。
- 墙、地板、天花板、Trim、道具等任意 Static Mesh。
- 按同一 Mesh 分组的批量绑定，例如一次设置一组墙体 Actor。

## 边界

- 不修改 Blueprint 组件模板或 Static Mesh 源资产。
- 不设置材质、Transform、碰撞、物理或其他属性。
- Actor 拥有多个 Static Mesh 组件时暂不支持指定组件；只有出现真实需求后再扩展 `component_name`。
- 工具只完成确定性的编辑器绑定，接缝、Z-fighting、碰撞、通行和视觉效果仍需在关卡中验收。

## 验证

- Python Action Schema AST 检查通过。
- 本工具相关文件的 `git diff --check` 通过。
- `DemoEditor Win64 Development` 完整冷编译成功；UEEditorMCP 插件编译、链接与 Target Metadata 写入均成功。
- 重启 UE 后动作目录由 150 增加到 151，`ue_actions_search` 与 `ue_actions_schema` 均能读取 `editor.set_static_mesh_component`。
- Level0 实际执行 7 组调用，18/18 个 Actor 写入成功；覆盖 FloorB、WallH、CeilingB、TrimB、TrimD、WallTrimG 与 CeilingBTrim1。
- 每个结果均回读到 `StaticMeshComponent0` 的新 Mesh，修改前值为 `None`，关卡保持 Dirty 且没有自动保存其他 Package。
- 真实写入与工具通用性已验证；接缝、Z-fighting、碰撞、通行和视觉效果仍待用户在视口验收。
