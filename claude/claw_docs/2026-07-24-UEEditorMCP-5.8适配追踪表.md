# UEEditorMCP 插件 · UE5.8 逐文件适配追踪表

> 日期：2026-07-24　用途：升级 5.7.4→5.8 时边编边勾，追踪每个文件的编译适配状态
> 状态图例：☐ 未开始　⚙ 编译报错待修　✅ 编译通过　🧪 运行验证通过
> 依赖模块（Build.cs）：UnrealEd, BlueprintGraph, Kismet, KismetCompiler, GraphEditor, UMG, UMGEditor, EnhancedInput, InputBlueprintNodes, EditorScriptingUtilities, AssetTools, SourceControl, MaterialEditor, RenderCore, RHI, ModelViewViewModel, ModelViewViewModelBlueprint, FieldNotification

---

## 修复顺序建议

1. **先修 `MCPCommonUtils.h/.cpp`**（被几乎所有 Action 复用的公共 helper，一处修好多处受益）。
2. 再按风险从高到低修 Actions：`NodeActions` → `GraphActions` → `UMGActions` → `MaterialActions` → `EditorActions` → `LayoutActions` → `BlueprintActions` → 其余。
3. 网络/日志基础设施（`MCPBridge/MCPServer/MCPContext/MCPLogCapture`）通常最稳，放最后确认。
4. 每修完一个高风险文件就重编一次，避免错误雪球。

---

## 追踪表（按风险排序）

| # | 文件 (.cpp / .h) | 大小 | 风险 | 主要 5.8 风险 API 面 | 状态 | 报错/备注 |
|---|---|---:|:---:|---|:---:|---|
| 1 | `Private/Actions/NodeActions.cpp` `Public/Actions/NodeActions.h` | 184KB | ★★★★★ | K2Node 全家（CallFunction/Event/CustomEvent/Variable Get·Set/DynamicCast/MacroInstance/Sequence/ComponentBoundEvent/CreateDelegate）、`AllocateDefaultPins`、`UEdGraphSchema_K2` 连接/类型 | ☐ | |
| 2 | `Private/MCPCommonUtils.cpp` `Public/MCPCommonUtils.h` | 26KB | ★★★★ | pin/类型转换 helper、`FEdGraphPinType`、`UEdGraphSchema_K2` 常量、JSON↔UE 类型；**被大量复用，优先修** | ☐ | |
| 3 | `Private/Actions/GraphActions.cpp` `.h` | 67KB | ★★★★ | 节点连线/删除/查找、`FBlueprintEditorUtils`、collapse to function/macro、pin 遍历 | ☐ | |
| 4 | `Private/Actions/UMGActions.cpp` `.h` | 138KB | ★★★★ | UMG/UMGEditor：`UWidgetBlueprint`、`UPanelSlot`/`UCanvasPanelSlot`、Widget 树、事件绑定；MVVM 相关 | ☐ | |
| 5 | `Private/Actions/MaterialActions.cpp` `.h` | 139KB | ★★★★ | `UMaterialEditingLibrary`、材质表达式节点、`UMaterialExpression*`、RenderCore/RHI 编译诊断 | ☐ | |
| 6 | `Private/Actions/EditorActions.cpp` `.h` | 135KB | ★★★ | Actor CRUD/Transform、AssetRegistry/AssetTools、缩略图、PIE 控制、Outliner、viewport | ☐ | |
| 7 | `Private/Actions/LayoutActions.cpp` `.h` | 108KB | ★★★ | 图自动排版/注释、GraphEditor、节点 bounds/位置 API | ☐ | |
| 8 | `Private/Actions/BlueprintActions.cpp` `.h` | 45KB | ★★★ | Blueprint 创建/编译/属性、SCS 组件、`FKismetEditorUtilities`、接口增删 | ☐ | |
| 9 | `Private/Actions/EditorDiffActions.cpp` `.h` | 20KB | ★★ | SourceControl：`ISourceControlModule`/`ISourceControlProvider`、asset history | ☐ | |
| 10 | `Private/UEEditorMCPModule.cpp` `.h` | 38KB | ★★ | 模块注册、Action 分发、EditorSubsystem 生命周期 | ☐ | |
| 11 | `Private/Actions/EditorAction.cpp` `.h` | 13KB | ★★ | Action 基类/接口、JSON 请求分发 | ☐ | |
| 12 | `Private/Actions/ProjectActions.cpp` `.h` | 14KB | ★★ | 项目/资产枚举、批量操作 | ☐ | |
| 13 | `Private/MaterialLayoutUtils.cpp` `.h` | 9KB | ★★ | 材质图节点排版，`UMaterialGraph` 相关 | ☐ | |
| 14 | `Private/BlueprintAutoLayoutCommands.cpp` `.h` | 1KB | ★ | 蓝图自动排版命令封装（薄） | ☐ | |
| 15 | `Private/MCPBridge.cpp` `.h` | 25KB | ★ | Sockets/Networking（端口 55558）、Core，编辑器 API 少 | ☐ | |
| 16 | `Private/MCPServer.cpp` `.h` | 15KB | ★ | 请求收发、线程/Ticker、Core | ☐ | |
| 17 | `Private/MCPContext.cpp` `.h` | 12KB | ★ | 上下文/会话状态、Core | ☐ | |
| 18 | `Private/MCPLogCapture.cpp` `.h` | 7KB | ★ | 日志输出设备 hook、`FOutputDevice` | ☐ | |

---

## UE5.7→5.8 常见破坏模式速查（对号入座）

| 报错现象 | 多半原因 | 处理 |
|---|---|---|
| `no member named X` / `use of undeclared identifier` | 函数/枚举改名或移动头文件 | 在 5.8 源码 grep 新名，替换；补 `#include` |
| 裸指针赋值报错、`TObjectPtr` 不匹配 | 成员/返回类型改为 `TObjectPtr<T>` | 用 `.Get()` 或直接用 TObjectPtr，必要处显式转换 |
| `X is deprecated` 警告转错误 | API 标 `UE_DEPRECATED` | 换用提示里的新 API（错误信息通常给出替代） |
| 参数个数/顺序不符 | 函数签名新增参数（常见加 `const FXxxParams&`） | 按 5.8 签名补参数，多为可默认值 |
| K2Node `AllocateDefaultPins`/pin 创建报错 | Kismet pin API 调整 | 对照 5.8 `EdGraphSchema_K2` 与对应 K2Node 源码 |
| 链接错误 `unresolved external` | 模块拆分/API 宏变化 | 检查 Build.cs 是否需新增模块依赖 |
| UMG slot / MVVM 类型报错 | UMGEditor / MVVM API 演进较快 | 对照 5.8 `UMGToolSet`/MVVM 源码用法 |

> 提示：每个报错先看它属于上表哪一类，再定位到追踪表对应文件，把状态从 ☐ 改 ⚙，修好改 ✅。

---

## 全部 ✅ 后的运行验证（🧪）
- 编辑器启动无插件加载错误。
- MCP bridge 端口 55558 能连上（`ue_ping` 或简单 `editor.is_ready`）。
- 抽测 3 类操作：一个只读（`graph.describe`/`editor.get_actors`）、一个建图（`node.add_event`）、一个 UMG（`widget.get_tree`），确认功能未回归。
