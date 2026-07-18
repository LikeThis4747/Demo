# UMG Widget Blueprint 能力边界

> **确认来源**：用户询问负责脚手架的 AI 后给出官方答复 + 实际调用验证

---

## 能力边界总表

| 操作 | 能力 | action_id | 备注 |
|---|---|---|---|
| 列出组件（名字+类型） | ✅ | `widget.list_components` | 只返回 name + class |
| **读取树 + slot + anchors + render transform** | ✅ | `widget.get_tree` | 返回完整层级、slot 信息、anchors 四值、alignment、render transform |
| 添加组件 | ✅ | `widget.add_component` | 支持 TextBlock/Button/Image/Border/Overlay/HorizontalBox/VerticalBox/Slider/ProgressBar 等 |
| 删除组件 | ✅ | `widget.delete_component` | - |
| 重命名组件 | ✅ | `widget.rename_component` | - |
| 移动组件（改父节点） | ✅ | `widget.reparent` / `widget.add_child` | - |
| 设置文字/字号/样式 | ✅ | `widget.set_text` | 支持 TextBlock 和 Button 的子 TextBlock |
| 设置 position/size/visibility/enabled | ✅ | `widget.set_properties` | CanvasPanelSlot 的 position 和 size |
| 设置 h_align/v_align/padding | ✅ | `widget.set_properties` | 水平/垂直对齐 |
| **按钮 OnClicked 事件绑定** | ✅ | `widget.bind_event` | 会在事件图创建 ComponentBoundEvent 节点 |
| 按钮其他事件（OnPressed/OnReleased 等） | ✅ | `widget.bind_event` | event_name 参数支持 |
| Slider 专属属性 | ✅ | `widget.set_slider` | value/range/step |
| ComboBox 专属属性 | ✅ | `widget.set_combo_options` | 选项增删改 |
| MVVM ViewModel + Binding | ✅ | `widget.mvvm_add_viewmodel` / `widget.mvvm_add_binding` / `widget.mvvm_get_bindings` / `widget.mvvm_remove_binding` | MVVM 全套支持 |
| Text Binding（属性绑定） | ✅ | `widget.set_text_binding` | - |
| 加到 Viewport | ✅ | `widget.add_to_viewport` | 运行时显示 Widget |
| **读取字体/颜色/材质等 type-specific 属性** | ❌ | 无专用接口 | 需走 `blueprint.get_property` 等通用通道，绕路 |
| **写 Anchors 四值结构（FAnchors）** | ⚠️ | 无专用字段 | `widget.set_properties` 没暴露；可通过 `blueprint.set_property` 走 SCS/子对象路径设置，可行但不友好 |

---

## 关键 action 使用示例

### widget.get_tree — 读取完整 widget 树

返回数据包括：层级关系、每个 widget 的 class、slot 信息（position/size/anchors/alignment）、render transform。

**调用**：
```json
{
  "action_id": "widget.get_tree",
  "params": {
    "widget_name": "WBP_MyWidget"
  }
}
```

**返回结构**（示例）：
```json
{
  "tree": {
    "name": "CanvasPanel_0",
    "class": "CanvasPanel",
    "render_transform": {
      "translation": [0, 0],
      "scale": [1, 1],
      "shear": [0, 0],
      "angle": 0
    },
    "children": [
      {
        "name": "MyButton",
        "class": "Button",
        "slot": {
          "type": "CanvasPanelSlot",
          "position": [0, 0],
          "size": [400, 100],
          "anchors": [0.5, 0.5, 0.5, 0.5],
          "alignment": [0.5, 0.5]
        },
        "children": [...]
      }
    ]
  }
}
```

**关键点**：
- `anchors` 是四值数组 `[minX, minY, maxX, maxY]`，范围 0~1
- `alignment` 是二值数组 `[x, y]`，范围 0~1，0.5 表示中心对齐
- 用 `alignment [0.5, 0.5]` 可以让 position 直接代表组件中心点位置，不需要算 `-Size/2`

### widget.bind_event — 绑定按钮事件

**调用**：
```json
{
  "action_id": "widget.bind_event",
  "params": {
    "widget_name": "WBP_MyWidget",
    "widget_component_name": "MyButton",
    "event_name": "OnClicked"
  }
}
```

**效果**：在 Widget Blueprint 的事件图里创建 `UK2Node_ComponentBoundEvent` 节点，等价于在编辑器里点按钮 Details 面板 "On Clicked" 旁边的 "+"。

**支持的事件名**：
- `OnClicked` — 点击
- `OnPressed` — 按下
- `OnReleased` — 释放
- 其他 UMG Widget 的事件（如 OnCheckStateChanged 等）

**注意**：`widget.bind_event` 只创建事件节点，**不会连线**。连线需要用户在 Graph 面板手动操作，或通过其他蓝图节点操作 action 完成。

### widget.set_properties — 设置位置和尺寸

**调用**：
```json
{
  "action_id": "widget.set_properties",
  "params": {
    "widget_name": "WBP_MyWidget",
    "target": "MyButton",
    "position": [-150, 0],
    "size": [300, 60],
    "h_align": "Center",
    "v_align": "Center"
  }
}
```

**支持的参数**：
- `position`: [X, Y] — CanvasPanelSlot 的位置
- `size`: [W, H] — CanvasPanelSlot 的尺寸
- `visibility`: "Visible" / "Hidden" / "Collapsed" / "HitTestInvisible" / "SelfHitTestInvisible"
- `is_enabled`: boolean
- `h_align`: "Fill" / "Left" / "Center" / "Right"
- `v_align`: "Fill" / "Top" / "Center" / "Bottom"
- `padding`: [Left, Top, Right, Bottom]

**不支持**：
- `anchors` 四值结构（FAnchors）— 需走 `blueprint.set_property`

### widget.set_text — 设置文字和字号

**调用**：
```json
{
  "action_id": "widget.set_text",
  "params": {
    "widget_name": "WBP_MyWidget",
    "target": "MyButton",
    "text": "开始游戏",
    "font_size": 24
  }
}
```

**支持的参数**：
- `target`: TextBlock 名称或 Button 名称（Button 会自动找子 TextBlock）
- `text`: 文字内容
- `font_size`: 字号

**不支持**：
- 读取文字内容（无 `get_text` 对应接口）
- 读取字号、颜色等其他样式属性

---

## 已知能力缺口

### 1. 读取 type-specific 属性

**缺口**：没有对称的 `get_text` / `get_style` / `get_font` 等读取接口。

**绕路方案**：使用 `blueprint.get_property` 走通用属性通道，访问 CDO 的子对象。但路径复杂，不友好。

### 2. 写 Anchors 四值结构

**缺口**：`widget.set_properties` 的 schema 没有暴露 `anchors` 字段。

**绕路方案**：使用 `blueprint.set_property` 走 SCS/子对象路径设置 FAnchors 结构。可行但不友好。

**临时解决**：
- 让用户在编辑器里手动设锚点
- 或使用 `alignment` 代替（部分场景适用）

### 3. 事件节点连线

**缺口**：`widget.bind_event` 只创建事件节点，不连线。

**绕路方案**：使用蓝图节点操作 action（如 `connect_blueprint_nodes`）连线，但需要先获取节点 ID 和 pin ID，流程复杂。

**实际建议**：事件节点的连线让用户在 Graph 面板手动操作，简单直观。

---

## 实战经验

### 经验 1：创建 Widget Blueprint 的根节点

**问题**：MCP 无法设置 Widget Blueprint 的根 widget。`widget.add_component` 要求父节点已存在。

**解决**：让用户在编辑器里手动创建根 Canvas Panel，然后用 MCP 添加子组件。

### 经验 2：锚点 + alignment 的居中方案

**问题**：position 是锚点的左上角偏移，不是中心偏移。设 position=[0,0] 会让组件偏右。

**解决**：用 `alignment [0.5, 0.5]`，position 直接代表组件中心点位置，不需要算 `-Size/2`。

**示例**：
- 锚点在中央 `[0.5, 0.5, 0.5, 0.5]`
- alignment `[0.5, 0.5]`
- position `[0, 0]` → 组件中心在屏幕正中央
- position `[0, 100]` → 组件中心在屏幕中央下方 100px

### 经验 3：Widget 编译和保存

**操作**：
```json
// 编译
{
  "action_id": "blueprint.compile",
  "params": { "blueprint_name": "WBP_MyWidget" }
}

// 保存
{
  "action_id": "editor.save_all",
  "params": {}
}
```

**注意**：`blueprint.compile` 会自动保存关联的包。`editor.save_all` 用于保存所有未保存的包。

---

## 参考文档

- `MCP-SEARCH-GUIDE.md`（项目根目录）— 搜索词换词表
- 本文件夹下 `blueprint-capabilities.md` — Blueprint 蓝图能力边界（待创建）
- 本文件夹下 `editor-capabilities.md` — 编辑器/场景能力边界（待创建）

---

*本文档随项目开发持续更新。发现新的能力或缺口请补充。*
