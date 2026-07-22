# Demo 项目架构规则

## 项目定位

- 工程：`D:\UE5projects\Demo`
- 引擎：Unreal Engine 5.7.4
- 类型：以 C++ 为主的单机 Demo
- 预计工期：三周；所有架构和任务优先级必须考虑短工期，先完成玩法闭环，再做必要优化。
- 目标：结构清晰、方便扩展，避免因职责混乱或性能问题反复返工。
- 主要架构参考：`D:\UE5projects\ue5-warrior`
- 对照项目：`D:\UE5projects\MyFPS`

## C++ 优先

- C++ 实现玩法逻辑、ActorComponent、GameMode、GameState、PlayerController 和 Subsystem。
- C++ 定义接口、委托、状态、生命周期、数据校验和性能关键路径。
- 蓝图只做资源填充、UI 布局、关卡实例配置和 AnimBP 状态机连线。
- `UPROPERTY` 只暴露资源与配置，默认使用 `EditDefaultsOnly`/`VisibleAnywhere` 和 `BlueprintReadOnly`。
- 无明确蓝图调用需求时，不使用 `BlueprintCallable` 或 `BlueprintReadWrite`。
- DataAsset/DataTable 保存配置，蓝图类不作为纯数据容器。

AI 使用蓝图实现玩法逻辑前，必须先说明 C++ 不适合的具体原因。

## 架构边界

| 类型 | 职责 |
|---|---|
| Character/Pawn | 组件装配、移动、相机、输入转发 |
| ActorComponent | 可复用且独立变化的玩法职责 |
| GameMode/GameState | 本局规则、流程和胜负状态 |
| PlayerController | 本地输入模式、菜单与 UI 流程 |
| DataAsset/DataTable | 角色、武器、敌人和关卡配置 |
| Widget | 展示状态、发送用户意图，不持有玩法权威状态 |

- 每个关键状态只有一个明确 Owner。
- 查询能力使用 Interface；状态变化使用 Delegate；稳定语义标识可使用 GameplayTag。
- Character 是组合入口，不堆积武器、生命、UI、胜负等全部逻辑。
- GameMode 不直接操作具体 Widget。
- 只有职责会复用、独立变化或能显著简化 Actor 时才创建组件。

## 源码目录

- 参考 Warrior 的 `Public/Private` 镜像结构，但按三周 Demo 规模精简：

```text
Source/Demo/
├─ Public/                 对外头文件
│  ├─ Characters/
│  ├─ Components/          按 Magnetism 等稳定职责再分层
│  ├─ AI/
│  ├─ Generation/
│  ├─ GameFlow/
│  ├─ Interfaces/
│  ├─ Data/
│  └─ Types/
└─ Private/                与 Public 镜像的实现文件
```

- 目录按稳定职责创建；没有实际文件时不提前建立空目录。
- 模块根只保留 `Demo.Build.cs`、模块入口和真正全局的最小文件。
- Character 负责装配、移动、相机和输入转发；磁力、追猎、生成与局流程不得全部堆入 Character 或 GameMode。
- 编码方案必须列出每个新增/修改文件的完整路径、职责、状态 Owner、依赖及并行冲突；用户确认后才能创建。
- 头文件只有需要被模块其他部分引用时才放 `Public`；纯内部实现放 `Private`。

## 性能基线

- 默认关闭 Lumen、硬件光追、Virtual Shadow Maps、Mesh Distance Fields、Substrate 和 Sorted Pixels OIT。
- 默认使用 Scalable 桌面配置；按题目需求逐项开启高级效果。
- Actor/Component 默认关闭 Tick，优先事件、Delegate、Timer、AnimNotify、碰撞、感知和行为树。
- 战斗或高频路径禁止同步加载、全局 Actor 搜索和重复创建对象。
- 大型或可选资产使用软引用与异步加载，并处理失败和对象销毁。
- Timer、Delegate、异步回调在 EndPlay 或取消路径中清理。

当前同时关闭 Lumen 与静态光照，适合快速原型，但没有烘焙间接光。若题目重视室内照明，再评估开启静态光照；不要默认重新开启 Lumen。

## 代码可读性与注释

- 每个新增或实质修改的 C++ 文件必须在文件顶部说明职责、边界与关键状态 Owner，便于快速判断代码归属。
- 每个函数都要有简洁注释，说明用途；非平凡函数还要补充输入、输出、状态副作用或失败处理。构造函数、生命周期函数和输入回调也不能省略。
- 每个属性都要说明语义、单位、生命周期或配置影响；运行时状态还要标明由谁写入和何时恢复。注释不得只重复变量名。
- 物理、PCG、筛选评分和容错等关键算法，必须解释设计意图、主要假设、边界条件与降级路径，方便调试和答辩定位。
- 行为变化时必须同步更新注释；可以用一条分组注释覆盖连续的简单访问器，但不得为了减少篇幅牺牲可理解性。

## 参数暴露与调参

- 需要频繁验证手感、难度或表现的参数，优先提供设计师可直接调整的 `DataAsset`、DataTable 或蓝图默认值；算法不变量和安全边界留在 C++，不得为了“可调”暴露所有内部状态。
- 不同职责使用独立配置源。例如输入资源与磁力手感必须使用两份互不引用的 DataAsset，避免修改输入配置时牵连物理手感，也避免形成包罗所有系统的大配置表。
- 同一参数只能有一个运行时权威来源。接入 DataAsset 后不得在组件里保留另一套静默兜底数值；资产缺失或非法时应输出明确错误并停用对应功能。
- DataAsset 属性注释必须写明对应的 C++ 属性/读取位置、单位、初始值、编辑范围，以及调高或调低的主要影响；同时使用合适的 `ClampMin/ClampMax`、`UIMin/UIMax` 和 `Units` 元数据约束编辑器输入。
- 若资源装配或参数调整由用户在 UE 编辑器内完成更直接，AI 应在正确时机给出前置条件、逐步路径和预期结果，不得用构造期硬编码资源路径规避正常协作。

## MCP 工作流

AI 不得只读 C++ 就推断整个项目。修改前通过 UE Editor MCP 检查相关 Blueprint、Widget、Input、DataAsset/DataTable、AI、动画资产、引用关系、配置和编译状态。

标准流程：`搜索动作 → 读取 schema → 执行动作 → 检查结果`。修改后必须验证 C++ 构建、蓝图编译、资产保存、实际运行和至少一个边界情况。

## AI 开发流程

编码前输出简短说明：核心循环、类/组件职责、关键状态 Owner、C++/蓝图/数据边界、文件与资产清单、性能风险和验证方式。

实现时先完成最小可运行闭环，不为未提出的需求增加 GAS、联机框架或大型系统。

在三周工期内，优化建议必须区分“阻塞交付/高回报/可延后”；除非问题会导致崩溃、数据丢失或明显性能风险，否则优先提出下一项可玩的内容和新点子，不擅自实施重构。

完成时提供实际验证证据；仅生成代码或仅通过 C++ 编译不算完成。
