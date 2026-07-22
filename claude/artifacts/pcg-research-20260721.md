# PCG 调研：迷宫房间 + 资源 + 陷阱生成（Demo 项目）

> 辅AI 调研产物，2026-07-21。仅供理解与选型，**未改动任何代码/资产**。
> 对应任务卡：`claude/tasks/active/TASK-20260721-001-PCG-maze-research.md`

---

## 0. 一句话结论

PCG（Procedural Content Generation Framework）是 UE5 自带的一套**可视化 + C++ 可扩展**的程序化生成框架，核心思路是"**用节点图把一堆点(Point)算出来，再在这些点上摆网格/Actor**"。做迷宫房间/资源/陷阱**完全可行**，且引擎已内置 Grid、A\* 寻路、Grammar(L-System) 等现成能力。

**当前工程状态**：PCG 插件在引擎里存在（`D:\UE5\Engine\Plugins\PCG`）但**项目未启用**（`Demo.uproject` 里没有它）。要用得先启用插件——这一步需要你授权。

---

## 1. PCG 是什么（30 秒理解）

把它想象成"**几何版的材质编辑器**"：

- 你在一张**节点图（PCG Graph）**里连线，规定"如何生成内容"。
- 数据在节点间流动，最核心的数据是**点云（Point Data）**：每个点 = 一个 `Transform`(位置/旋转/缩放) + 密度 + 包围盒 + 颜色 + 随机种子等属性。
- 图的末端用 **Spawner 节点**在这些点上真正摆出东西：静态网格实例（ISM/HISM）、Actor、Spline Mesh 等。
- 一个 **PCGComponent** 挂在场景里的某个 Actor 上，持有这张图，负责"什么时候生成、生成完管理这些资源"。

典型数据流（迷宫例子）：
```
生成格子点阵 → 用规则挑出"房间/墙/门"的点 → 打属性(是哪种房间) → Spawner 摆墙和地板 → 再采样点摆资源/陷阱
```

关键概念对照表：

| 概念 | 类 / 文件 | 通俗解释 |
|---|---|---|
| 节点配置 | `UPCGSettings`（`PCGSettings.h`） | 一个节点长什么样、有哪些引脚、参数（UObject，能被序列化/在细节面板编辑） |
| 节点执行体 | `IPCGElement`（`PCGElement.h`） | 节点真正干活的逻辑，线程安全，可分帧执行 |
| 点 | `FPCGPoint`（`PCGPoint.h`） | 单个点：Transform+Density+Bounds+Color+Seed |
| 点集 | `UPCGBasePointData` → `UPCGPointData`/`UPCGPointArrayData` | 一堆点。新代码建议面向 `UPCGBasePointData` |
| 空间数据 | `UPCGSpatialData`（`Data/PCGSpatialData.h`） | 体积/表面/样条/地形等，可被采样成点 |
| 参数数据 | `UPCGParamData`（`Data/PCGParamData.h`） | 键值属性集，用来做参数/表格，非空间 |
| 运行时驱动 | `UPCGComponent`（`PCGComponent.h`） | 挂 Actor 上，触发生成、管理生成物 |

---

## 2. 自定义 C++ 实现 PCG 的几条路径（重点）

从"改动最小/最快" 到 "最灵活/最重"，一共 4 条路：

### 路径 A：纯节点图，不写 C++（最快，先验证玩法）
只用引擎内置节点在 PCG Graph 里连线，配合 DataAsset 配参数。
- **优点**：零 C++，几小时能出第一版迷宫轮廓。
- **缺点**：复杂随机逻辑（连通性保证、房间类型状态机）在节点图里会很难维护。
- **适合**：原型验证阶段、你还不确定玩法时。

### 路径 B：蓝图自定义节点（UPCGBlueprintBaseElement）
继承 `UPCGBlueprintBaseElement`（`Elements/Blueprint/PCGBlueprintBaseElement.h`），在蓝图里重写 `Execute(Input, Output)`，把自定义逻辑做成图里的一个节点。
- **优点**：能写自定义逻辑又不用编译 C++；引脚可配置。
- **缺点**：蓝图写复杂算法（A\*、迷宫生成）性能和可维护性一般；与项目"C++ 优先"规范相悖，需说明理由。
- **适合**：局部小逻辑、快速试验。

### 路径 C：原生 C++ 自定义节点（★ 与本项目 "C++ 优先" 规范最契合）
继承 `UPCGSettings` + 实现 `IPCGElement`，做成一个真正的 C++ PCG 节点。这是引擎内置节点自己的实现方式。

**最小骨架（两段式）**：
```cpp
// 1) Settings：定义节点外观 + 引脚 + 参数
UCLASS()
class UPCGMazeGeneratorSettings : public UPCGSettings
{
    GENERATED_BODY()
public:
    // 节点外观（仅 WITH_EDITOR）
    virtual FName GetDefaultNodeName() const override { return TEXT("MazeGenerator"); }
    virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
    // 引脚
    virtual TArray<FPCGPinProperties> InputPinProperties() const override;
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override; // 可用 Super::DefaultPointOutputPinProperties()
    // 参数（暴露到细节面板）
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin=3))
    int32 GridWidth = 9;
    // 工厂
    virtual FPCGElementPtr CreateElement() const override;
};

// 2) Element：真正干活（可跨线程/分帧）
class FPCGMazeGeneratorElement : public IPCGElement
{
protected:
    virtual bool ExecuteInternal(FPCGContext* Context) const override; // 核心：读输入→算迷宫→写输出点
    virtual bool IsCacheable(const UPCGSettings*) const override { return true; }
};
```
- **参考示例（引擎里最简单的）**：`Elements/PCGCreatePoints.h` 和 `Elements/PCGCreatePointsGrid.h`——后者就是"生成 2D/3D 点阵网格"，几乎是迷宫格子的起点，强烈建议照抄结构。
- **优点**：性能好、可复用、可单元测试、符合项目规范；迷宫算法(DFS/Prim/A\*)直接用 C++ 写。
- **缺点**：要了解 PCG 的引脚/数据/Context API，首个节点上手成本最高（但有内置示例可抄）。
- **适合**：本项目主力路径。

### 路径 D：完全不用 PCG，自己写生成器 Actor（备选）
写一个普通的 C++ Actor/Subsystem，自己算迷宫、自己 SpawnActor / 加 ISM 组件，不引入 PCG 插件。
- **优点**：不引入 PCG 依赖，最轻量、最可控，打包体积小。
- **缺点**：放弃了 PCG 的编辑器可视化调试、缓存、RuntimeGen 调度、现成 Grammar/A\* 等；后续扩展成本高。
- **适合**：如果最终只是"生成个迷宫"且不打算深挖程序化内容，这条反而最省事。

> **推荐组合**：原型期用 **A/B** 快速验证 → 稳定后把核心迷宫算法沉淀成 **C（原生 C++ 节点）**。若评估后觉得 PCG 太重且需求简单，直接选 **D**。

---

## 3. 引擎已内置、可直接用的"迷宫相关"能力

不用自己造轮子的部分（都在 `D:\UE5\Engine\Plugins\PCG\Source\PCG\Public\`）：

| 能力 | 文件 | 对迷宫的用处 |
|---|---|---|
| 点阵网格生成 | `Elements/PCGCreatePointsGrid.h` | 生成房间格子（GridExtents/CellSize/中心或角点） |
| A\* 寻路 | `SpatialAlgo/PCGAStar.h` | 保证房间连通、生成走廊路径（可迭代/部分路径） |
| Grammar / L-System | `Grammar/PCGGrammarParser.h` + `Grammar/PCGGrammar.h` | 用语法串（如 `[Room,Corridor]*`）程序化排布房间/走廊模块 |
| 线段/样条细分 | `Elements/Grammar/PCGSubdivideSegment.h` | 把一条走廊按 grammar 切成一段段模块 |
| 等值线提取 | `SpatialAlgo/PCGMarchingSquares.h` | 从占用网格提取房间轮廓/墙体边界 |
| 八叉树查询 | `SpatialAlgo/PCGOctreeQueries.h` | 邻近房间查询、判断哪些点该放资源/陷阱 |
| 网格生成 | `Elements/PCGStaticMeshSpawner.h` | 在点上摆墙/地板/门（ISM/HISM，高性能） |
| Actor 生成 | `Elements/PCGSpawnActor.h` | 房间/陷阱如果需要蓝图 Actor（带逻辑）就用它 |
| 表面/体积采样 | `Elements/PCGSurfaceSampler.h`、`PCGVolumeSampler.h` | 在房间地面上撒资源/陷阱点 |

---

## 4. 三个目标的推荐技术路线（初步）

### 4.1 随机生成迷宫房间
1. `PCGCreatePointsGrid` 生成 N×M 格点（每格 = 一个候选房间/单元）。
2. 迷宫拓扑算法（C++ 自定义节点里写 DFS/Prim/递归分割）决定哪些格子是房间、哪些边是通道、哪些是墙——给每个点打属性（`bIsRoom`/`bIsWall`/`RoomType`）。
3. 可选：A\*（`PCGAStar.h`）保证起点→终点连通、生成主路径。
4. `PCGStaticMeshSpawner` 按属性摆地板/墙/门模块。
- **随机可复现**：PCG 点自带 `Seed`，`UPCGSettings::UseSeed()` 返回 true 即可让节点吃随机种子——换种子就换一套迷宫，同种子结果一致（方便调试）。

### 4.2 生成资源（拾取物 / 宝箱等）
1. 从"房间点"里筛选可放置点（避开门口、避免重叠）。
2. 用 `PCGSurfaceSampler` 或直接在房间点上按密度/权重抽样。
3. 用 DataAsset/DataTable 配"资源表"（种类、权重、稀有度），`PCGStaticMeshSpawner`(静态摆件) 或 `PCGSpawnActor`(可交互拾取物) 摆放。

### 4.3 生成陷阱
1. 陷阱通常要在**走廊/门口/房间中央**这类特定位置——用属性过滤 + `PCGOctreeQueries` 做间距约束（陷阱之间别太密）。
2. 陷阱一般有逻辑（触发、伤害），所以用 `PCGSpawnActor` 摆**陷阱 Actor 蓝图/C++ 类**，而不是纯网格。
3. 难度曲线：可用点到起点的距离作为属性，越深陷阱越多/越强。

---

## 5. 运行时（游戏里）怎么触发生成

`UPCGComponent`（`PCGComponent.h`）的生成时机 `EPCGComponentGenerationTrigger`：
- `GenerateOnLoad`：关卡加载即生成（适合固定关卡）。
- `GenerateOnDemand`：**按需**，代码/蓝图调用 `GenerateLocal(true)` 触发——**适合"每局随机迷宫"**（进关卡时用新种子生成一次）。
- `GenerateAtRuntime`：交给 RuntimeGen 调度器按玩家位置流式生成（`RuntimeGen/PCGRuntimeGenScheduler.h`），适合大世界，本 Demo 大概率用不到。

Demo 迷宫推荐：`GenerateOnDemand` + 进关卡时 C++ 里设随机种子并调 `GenerateLocal(true)`。

---

## 6. 对本项目规范的影响与风险

- **规范契合**：`PROJECT_ARCHITECTURE_RULES.md` 要求 C++ 优先、蓝图只做配置。→ 迷宫算法走**路径 C（C++ 节点）**最契合；蓝图只用于配 Graph 引用和资源表。
- **性能基线**：项目默认关 Lumen/VSM 等。PCG 生成本身不违反，但 `PCGStaticMeshSpawner` 会产生大量实例，需注意实例化(ISM/HISM)与剔除。
- **插件依赖（需你决策）**：启用 PCG 会连带启用 `ComputeFramework`、`GeometryProcessing`、`MeshModelingToolset`、`EditorScriptingUtilities`。会增加编译时间和打包体积。三周工期要权衡。
- **题目未公布**：`projectbrief.md` 显示 Demo 题目尚未公布，"迷宫/资源/陷阱"目前是预设方向，需注意与最终题目对齐。

---

## 7. 建议你先读的官方文档（快速入门顺序）

1. PCG 框架总览（`.uplugin` 里的 DocsURL）：
   https://docs.unrealengine.com/latest/en-US/procedural-content-generation--framework-in-unreal-engine/
2. 关键词，按此顺序查：`PCG Overview` → `PCG Graph` → `PCG Component` → `PCGStaticMeshSpawner` → `PCG Grammar / Building Generator` → `PCG Custom Node C++`。
3. 引擎内直接读源码示例（最有价值）：`Elements/PCGCreatePointsGrid.h/.cpp`（抄结构）。

---

## 8. 需要你拍板的决策点

1. **是否启用 PCG 插件**？（启用=能用全部内置能力，代价是依赖变重；不启用=只能走路径 D 自己写。）
2. **走哪条实现路径**？A(纯图) / B(蓝图节点) / C(C++节点，符合规范) / D(不用PCG自己写)。
3. **迷宫风格**：网格房间型（Rogue-like 房间+走廊）还是走廊迷宫型（细路+死胡同）？影响算法选择。
4. 确认后，我再出**迷宫房间生成的最小可运行闭环**的详细方案（含文件清单、类职责、验证步骤），仍然**等你授权才动代码**。
