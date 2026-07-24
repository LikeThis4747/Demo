# Static Mesh 批量体检 MCP 工具

## 目的

批量读取 UE Content 目录下 Static Mesh 的真实局部尺寸与 Pivot 位置，避免为了筛选模块化 PCG 素材而逐个拖入关卡测量。

工具只读，不修改、保存或重导入任何素材。

## 位置

- 动作 ID：`editor.inspect_static_meshes`
- C++ 声明：`Plugins/UEEditorMCP/Source/UEEditorMCP/Public/Actions/EditorActions.h`
- C++ 实现：`Plugins/UEEditorMCP/Source/UEEditorMCP/Private/Actions/EditorActions.cpp`
- UE 注册：`Plugins/UEEditorMCP/Source/UEEditorMCP/Private/MCPBridge.cpp`
- MCP Schema：`Plugins/UEEditorMCP/Python/ue_editor_mcp/registry/actions.py`

## 方案

工具通过 Asset Registry 递归查找指定目录下的 `UStaticMesh`，加载网格后读取：

- Local Bounds Min/Max；
- X/Y/Z 尺寸；
- Pivot 在每个 Bounds 轴上位于最小边、最大边、中心、内部还是外部；
- 综合 Pivot 类型：角点、边、面、中心、内部或自定义；
- 尺寸相对指定逻辑网格的比例。

模型 Pivot 本质上是局部坐标原点，因此无需额外创建 Actor 或 Socket。

## 输入

| 参数 | 含义 | 默认值 |
|---|---|---:|
| `path` | Content 路径，例如 `/Game/SciFiHydroLab` | 必填 |
| `recursive` | 是否递归子目录 | `true` |
| `grid_size` | 参考网格尺寸，厘米 | `300` |
| `tolerance` | 判断 Pivot 是否位于边界的误差，厘米 | `0.1` |
| `max_results` | 最大返回数量 | `500`，上限 2000 |

示例：

```json
{
  "path": "/Game/SciFiHydroLab/Meshes/Walls",
  "recursive": true,
  "grid_size": 300,
  "tolerance": 0.1,
  "max_results": 500
}
```

## 输出

每个网格返回 `asset_name`、`asset_path`、`bounds_min`、`bounds_max`、`size`、`pivot_axes`、`pivot_class` 和 `grid_units`。同时返回扫描总数、截断状态与加载失败列表。

## 验证

- `DemoEditor Win64 Development` 构建成功。
- MCP 动作目录由 149 增加到 150。
- 扫描 `/Game/SciFiHydroLab`：312 个 Static Mesh 全部成功，0 个失败。
- 自动结果与编辑器手工测量一致：`FloorA` 约 150×300×3.75，`WallB1` 约 10.85×300×300，并能识别角点/端点 Pivot。

## 边界

Bounds 与 Pivot 只能证明几何规格，不能单独证明接缝、材质、碰撞、玩家净空或视觉兼容。正确流程是先批量分组，再为每个规格组实拼少量代表件。
