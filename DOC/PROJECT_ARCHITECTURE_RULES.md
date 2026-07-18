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

## 性能基线

- 默认关闭 Lumen、硬件光追、Virtual Shadow Maps、Mesh Distance Fields、Substrate 和 Sorted Pixels OIT。
- 默认使用 Scalable 桌面配置；按题目需求逐项开启高级效果。
- Actor/Component 默认关闭 Tick，优先事件、Delegate、Timer、AnimNotify、碰撞、感知和行为树。
- 战斗或高频路径禁止同步加载、全局 Actor 搜索和重复创建对象。
- 大型或可选资产使用软引用与异步加载，并处理失败和对象销毁。
- Timer、Delegate、异步回调在 EndPlay 或取消路径中清理。

当前同时关闭 Lumen 与静态光照，适合快速原型，但没有烘焙间接光。若题目重视室内照明，再评估开启静态光照；不要默认重新开启 Lumen。

## MCP 工作流

AI 不得只读 C++ 就推断整个项目。修改前通过 UE Editor MCP 检查相关 Blueprint、Widget、Input、DataAsset/DataTable、AI、动画资产、引用关系、配置和编译状态。

标准流程：`搜索动作 → 读取 schema → 执行动作 → 检查结果`。修改后必须验证 C++ 构建、蓝图编译、资产保存、实际运行和至少一个边界情况。

## AI 开发流程

编码前输出简短说明：核心循环、类/组件职责、关键状态 Owner、C++/蓝图/数据边界、文件与资产清单、性能风险和验证方式。

实现时先完成最小可运行闭环，不为未提出的需求增加 GAS、联机框架或大型系统。

在三周工期内，优化建议必须区分“阻塞交付/高回报/可延后”；除非问题会导致崩溃、数据丢失或明显性能风险，否则优先提出下一项可玩的内容和新点子，不擅自实施重构。

完成时提供实际验证证据；仅生成代码或仅通过 C++ 编译不算完成。
