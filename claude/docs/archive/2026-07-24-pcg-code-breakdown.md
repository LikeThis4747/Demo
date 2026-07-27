# PCG 实现代码架构分类报告（含精确文件位置）

> 基线：`16833e3`（2026-07-24 Nightly snapshot），10 个 C++ 文件，合计 ~13,690 行。

---

## 一、速览：每个文件的定位

| 文件 | 行数 | 定位一句话 |
|---|---|---|
| `ZeroEscapeLayoutSolver.cpp` | 5,136 | **算法核心**：WFC + 回溯 + A* + 图到网格映射 |
| `ZeroEscapeGenerationCore.cpp` | 2,853 | **生成编排**：主循环、图结构构建、预算管理、环境查询 |
| `ZeroEscapeGenerationTests.cpp` | 2,095 | **自动化测试**：WFC / A* / 约束 / 集成 全覆盖 |
| `ZeroEscapeGenerationAssets.cpp` | 1,047 | **三层校验**：DataAsset 单品→组合→跨资产校验 |
| `ZeroEscapeGenerationTypes.h` | 759 | **数据结构**：全部枚举、结构体、配置类型 |
| `ZeroEscapeRuntimeLevelGenerator.cpp` | 576 | **运行时实例化**：刷 Actor、HISM 管理、生命周期 |
| `ZeroEscapeLayoutSolver.h` | 421 | 求解器类声明 |
| `ZeroEscapeGenerationAssets.h` | 359 | DataAsset UCLASS 声明 |
| `ZeroEscapeGenerationCore.h` | 268 | GenerationCore 类声明 |
| `ZeroEscapeRuntimeLevelGenerator.h` | 176 | Runtime Actor 类声明 |

---

## 二、按功能维度精确拆解

### 2.1 WFC 求解器（含回溯） — ~2,200 行

**文件**：`Source/Demo/Private/PCG/ZeroEscapeLayoutSolver.cpp`

| 行号 | 子模块 | 做了什么 |
|---|---|---|
| **1118-1232** | `RemoveVariant` + `PropagateRemovedVariants` | WFC 唯一 Domain 删除入口：位清零 + 计数递减 + 传播队列 |
| **1232-1323** | `BuildOrRebuildSupportCounts` | 从完整 Domains 重算全部支持计数并传播零支持候选，初始化和快照恢复共用 |
| **1323-1400** | `BuildCellsFromConstraints` | 抽取 ActiveWfc Constraints，排序建立 Cell 数组和四向邻接索引 |
| **1400-1480** | `ChooseWeightedVariant` | 64 位随机样本对整数权重取模，按 StableVariantId 顺序扫描，可复现 |
| **1480-1655** | `Observe` + `WfcCollapse` | Observation 阶段：删除除 ChosenVariant 外所有候选，传播删除队列 |
| **2240-2530** | **WFC 回溯主循环** | 冲突时撤销最近选择、恢复候选集、标记失败分支、重试未尝试候选。保留 un-tried 候选 + 恢复后重建支持计数（而非快照计数） |
| **2530-2865** | GraphToGrid 放置 | 强/弱 Anchor 放置、模块占格计算、碰撞避让 |
| **3804-4315** | WFC 求解内循环 | ActiveWfc 约束→Domain→坍缩→传播→终态导出 完整流水线 |
| **4315-4835** | 后置验证 | 全部格填完后检查邻接一致性、必经点覆盖、连通性 |

---

### 2.2 图到网格映射 & 放置 — ~1,100 行

**文件**：`Source/Demo/Private/PCG/ZeroEscapeLayoutSolver.cpp`

| 行号 | 子模块 | 做了什么 |
|---|---|---|
| **445-595** | Portal 对齐 + 占格枚举 | `EnumerateFootprintCells`：检查模块旋转后在网格上占几格、哪些格 |
| **595-845** | Cell 预约 + 方向性约束 | 为已放置模块预留 Cell，检查重叠冲突 |
| **925-1005** | `BuildEndpointOptions` | Strong Anchor 按 Policy/StableSocketId 排序未使用 Portal；Weak Anchor 生成逻辑端点 |
| **1005-1118** | A* Cell 序列→WFC 一元约束 | 相邻格相对方向必须成对开放，首尾格朝实体 Portal 开放 |
| **2530-2978** | GraphToGrid 放置流程 | 递归尝试所有候选旋转/位置组合，占格计算、碰撞检查、候选排序 |

---

### 2.3 A* 确定性寻路 — ~600 行

**文件**：`Source/Demo/Private/PCG/ZeroEscapeLayoutSolver.cpp`

| 行号 | 子模块 | 做了什么 |
|---|---|---|
| **1968-2240** | **确定性整数 A*** | Open/Closed 集、G/H/F 计算、邻居展开、固定方向顺序保证可复现 |
| **2100-2196** | Portal 对齐检查 | 到达目标 Portal 时检查朝向/位置对齐；备用路径探测（MaxAlternatePathProbesPerSocketPair=4） |
| **3430-3804** | `RouteGraphEdgesWithAStar` | 路由阶段入口：对排序后全部抽象 Edge 运行有界 DFS，封闭 Active 区域外露面 |

---

### 2.4 约束构建与传播 — ~800 行

**文件**：`Source/Demo/Private/PCG/ZeroEscapeLayoutSolver.cpp` + `Source/Demo/Private/PCG/ZeroEscapeGenerationCore.cpp`

| 文件:行号 | 子模块 | 做了什么 |
|---|---|---|
| Solver: **845-925** | `MergeDirectionalConstraint` | 合并同一 Cell 面结构要求，冲突时拒绝 |
| Solver: **1005-1118** | A*→WFC 约束翻译 | 路径转一元开口约束 |
| Solver: **1830-1968** | `BuildVariantCatalog` | Catalog 的 WfcSingleCell 模块展开为"模块×允许旋转"的稳定 Variant 集合 |
| Solver: **2978-3430** | 邻接兼容性检查 | `AreConnectorSignaturesCompatible`、`IsDirectStrongConnection`、Foreign 模块避让 |
| Core: **515-550** | `BuildModuleCatalog` | 从 DataAsset 构建求解器用的模块目录 |
| Core: **1300-1734** | Plan 验证 | 生成后校验 Plan 的完整性 |

---

### 2.5 生成主循环编排 — ~700 行

**文件**：`Source/Demo/Private/PCG/ZeroEscapeGenerationCore.cpp`

| 行号 | 子模块 | 做了什么 |
|---|---|---|
| **550-780** | `ResolveGenerationBudget` | 预算解析——事务式交叉校验 |
| **780-930** | `GeneratePlan` 主入口 | 最多 N 次 Attempt，每次清空状态重新求解 |
| **1070-1300** | 规范哈希 + Plan ID | 确定性标识生成，不写入表现层数据 |
| **2196-2400** | `BuildCriticalPath` | 关键路径构造 |
| **2400-2640** | `AddForwardRejoinBranches` | 前向回路分支：跨两个 ProgressIndex 的候选 Anchor |
| **2640-2750** | 最终 Plan 装配 | 节点排序、标识分配 |
| Solver: **4835-5136** | `Solve` 入口 | 求解器顶层入口：度量采集、Attempt 循环、失败原因记录 |

---

### 2.6 环境查询 — ~500 行

**文件**：`Source/Demo/Private/PCG/ZeroEscapeGenerationCore.cpp`

| 行号 | 子模块 | 做了什么 |
|---|---|---|
| **2750-2853** | 环境查询桩 | Dense Node Index 构建、导航/碰撞查询接口 |
| Solver: **2865-2978** | 放置验证 | 检查放置坐标是否越界、是否有环境冲突 |
| Solver: **2978-3430** | Foreign 检测 | 跨 Anchor 检测放置冲突 |

---

### 2.7 运行时 Actor 实例化 — ~400 行

**文件**：`Source/Demo/Private/PCG/ZeroEscapeRuntimeLevelGenerator.cpp`

| 行号 | 子模块 | 做了什么 |
|---|---|---|
| **1-80** | Includes + 构造函数 | 组件创建、默认值 |
| **80-250** | `OnConstruction` / `Destroyed` / `BeginPlay` | 生命周期管理，bEndingPlay 防递归 |
| **250-400** | `SpawnRoomActors` + `InstantiatePlan` | 按 Plan 逐条 SpawnActor，传 Transform + Variant |
| **400-576** | **HISM 管理** | Instanced Static Mesh 的创建→添加实例→Rollback 时清理 |

---

### 2.8 DataAsset 定义与校验 — ~900 行

**文件**：`Source/Demo/Private/PCG/ZeroEscapeGenerationAssets.cpp` + `Source/Demo/Public/PCG/ZeroEscapeGenerationAssets.h`

#### .h 头文件（359 行）— 类型声明

| 行号 | 内容 |
|---|---|
| 全文件 | `UZeroEscapeRoomAsset`、`UZeroEscapeGenerationProfile`、`UZeroEscapeLayoutConfig` 等 UCLASS 声明 |

#### .cpp 实现（1,047 行）— 三层校验

| 行号 | 校验层级 | 做了什么 |
|---|---|---|
| **1-90** | 基础设施 | Includes、结构体辅助函数 |
| **90-280** | 校验工具 | `IsValidDifficulty`、`IsValidSocketPolicy`、边界检查 |
| **280-374** | **组合校验** | `ValidateGenerationProfile`：Grid 尺寸、CriticalPath 节点数、Objective 候选数等组合合法性 |
| **374-818** | **单品校验** | `ValidateRoomAsset`：必需字段、尺寸合理性、Socket 完整性、ClosureModule 引用 |
| **818-958** | **跨资产联动校验** | ClosureModule 存在性、PresentationProfile 绑定完整性 |
| **958-1047** | 汇总校验 | `ValidateAllAssets`：遍历 AssetRegistry 调上述所有校验 |

---

### 2.9 数据结构 / 类型定义 — ~1,100 行

**文件**：全部 `.h` 文件（不含函数实现声明）

| 文件 | 行数 | 关键内容 |
|---|---|---|
| `ZeroEscapeGenerationTypes.h` | 759 | 枚举（`EZeroEscapeDifficulty`、`EZeroEscapeSocketPolicy`、`EZeroEscapeLayoutPolicy`、`EZeroEscapeGenerationFailure` 等 10+ 枚举）；结构体（`FZeroEscapeGenerationRequest`、`FZeroEscapeGenerationReport`、`FZeroEscapePlacementPlan`、`FGraphAnchorPlacement`、`FAbstractNode`、`FAbstractEdge`、`FConnectorSignature` 等 20+ 结构体） |
| `ZeroEscapeGenerationAssets.h` | 359 | UCLASS DataAsset 声明（`UZeroEscapeRoomAsset`、`UZeroEscapeGenerationProfile`、`UZeroEscapeLayoutConfig`） |
| `ZeroEscapeGenerationCore.h` | 268 | `FZeroEscapeGenerationCore` 类声明 |
| `ZeroEscapeLayoutSolver.h` | 421 | `FLayoutSolver` 类声明，内部结构（`FWfcState`、`FModuleCatalog` 等） |
| `ZeroEscapeRuntimeLevelGenerator.h` | 176 | `AZeroEscapeRuntimeLevelGenerator` Actor 类声明 |

---

### 2.10 安全性防护 — ~500 行

分散在各 `.cpp` 中的纯防护代码（Guard 开头、Early Return、空指针检查、边界检查）。

| 文件:行号（示例） | 防护类型 | 典型模式 |
|---|---|---|
| Core: **87-340** | 资产校验入口 | `CheckGenerationAssets` / `ValidateConfig` → Fail 即返回 |
| Core: **550-780** | 预算事务式校验 | 所有交叉字段校验通过前 OutBudget 保持默认空值 |
| Core: **1734-2196** | 边界检查 | `ValidateNodeCount`、`ValidateEdgeIdOrder` |
| Solver: **2240-2530** | 回溯安全 | 重建支持计数后验证无矛盾 |
| Solver: **4315-4835** | 终态验证 | 全部 Cell 必须恰一个候选、邻接一致 |
| Assets: 各处 | 空资产/非法值 | 每个 Validate 函数都在开头检查输入有效性 |
| Runtime: 各处 | Guard 全覆盖 | bEndingPlay 防递归、HISM 空指针 |

---

### 2.11 测试 — ~2,100 行

**文件**：`Source/Demo/Private/PCG/ZeroEscapeGenerationTests.cpp`

| 行号范围 | 测试类别 | 内容 |
|---|---|---|
| **1-100** | 基础设施 | Includes、共享 Fixture、版权头 |
| **100-600** | Transform 合成 + WFC 基础 | 冻结 Transform 合成约定；小网格坍缩/传播行为 |
| **600-1100** | WFC 回溯测试 | 回溯触发条件、回滚恢复、未尝试候选重试 |
| **1100-1500** | A* 寻路测试 | 直连、绕路、无路可达三种场景 |
| **1500-1900** | 约束兼容性 + 完整流程 | Socket 签名匹配、Portal 对齐、端到端集成 |
| **1900-2095** | 工具函数 | 共享辅助、Fixture 清理 |

---

### 2.12 基础设施/样板 — ~500 行

| 文件 | 大约行数 | 内容 |
|---|---|---|
| 全部 .h/.cpp | ~100 | 版权头 `// Copyright Epic Games, Inc.` × 10 文件 |
| 全部 .h/.cpp | ~120 | `#include` 指令 |
| 全部 .h | ~20 | `namespace ZeroEscape::LevelGeneration` 声明 |
| 全部 .h | ~100 | `GENERATED_BODY()` / `UPROPERTY()` / `UFUNCTION()` 宏 |
| 全部 .cpp | ~15 | `DEFINE_LOG_CATEGORY` 宏 |
| 全部 .cpp | ~25 | `using namespace` 声明 |
| 全部 | ~120 | 纯空行/注释分隔线等语法开销 |

---

## 三、按关心场景的快速索引

| 你想做什么 | 直奔这里 |
|---|---|
| **理解 WFC 怎么坍缩** | `ZeroEscapeLayoutSolver.cpp` → 1118-1655（RemoveVariant → Observe → Collapse） |
| **理解回溯怎么恢复** | `ZeroEscapeLayoutSolver.cpp` → 1232-1323（BuildOrRebuildSupportCounts）+ 2240-2530（回溯主循环） |
| **理解 A* 怎么找路** | `ZeroEscapeLayoutSolver.cpp` → 1968-2240 |
| **理解房间怎么放到网格** | `ZeroEscapeLayoutSolver.cpp` → 2530-2978（GraphToGrid）+ 925-1005（端点候选） |
| **理解生成主流程** | `ZeroEscapeGenerationCore.cpp` → 780-930（GeneratePlan） |
| **理解预算系统** | `ZeroEscapeGenerationCore.cpp` → 56-87（FCoreWorkBudget）+ 550-780（ResolveGenerationBudget） |
| **理解校验都验了什么** | `ZeroEscapeGenerationAssets.cpp` → 280-1047（三层校验全路径） |
| **理解运行时怎么刷场景** | `ZeroEscapeRuntimeLevelGenerator.cpp` → 250-576（SpawnRoomActors + HISM） |
| **跑测试看什么用例** | `ZeroEscapeGenerationTests.cpp` → 全文（按 IMPLEMENT_SIMPLE_AUTOMATION_TEST 分段） |
| **新枚举/结构体在哪** | `ZeroEscapeGenerationTypes.h` → 全文 |

---

## 四、分布图（带文件标注）

```
━━━━━━━━━━━━━━━━ WFC 求解器+回溯        ~2,200 (16%)  ← ZeroEscapeLayoutSolver.cpp
━━━━━━━━ 图到网格映射+放置              ~1,100 ( 8%)  ← ZeroEscapeLayoutSolver.cpp
━━━━ A* 确定性寻路                        ~600 ( 4%)  ← ZeroEscapeLayoutSolver.cpp
━━━━━━ 约束构建与传播                     ~800 ( 6%)  ← Solver.cpp + GenerationCore.cpp
━━━━━ 生成主循环编排                      ~700 ( 5%)  ← ZeroEscapeGenerationCore.cpp
━━━━ 环境查询                             ~500 ( 4%)  ← ZeroEscapeGenerationCore.cpp
━━━ 运行时实例化                          ~400 ( 3%)  ← ZeroEscapeRuntimeLevelGenerator.cpp
━━━━━━━ DataAsset 定义+校验               ~900 ( 7%)  ← ZeroEscapeGenerationAssets.h/.cpp
━━━━━━━━ 数据结构/类型定义               ~1,100 ( 8%)  ← 全部 .h 文件
━━━━ 安全性防护                           ~500 ( 4%)  ← 分散在各 .cpp
━━━━━━━━━━━━━━━━ 测试                   ~2,100 (15%)  ← ZeroEscapeGenerationTests.cpp
━━━━ 基础设施/样板                        ~500 ( 4%)  ← 全部文件
━━━━━━━━━━━━━━━━━ 辅助/注释/其他        ~2,290 (17%)  ← 分散在各文件
                                  ─────────
                                  13,690  (100%)
```

---

## 五、Solver.cpp 内部时间线（阅读路径）

如果你想一行行读最大的文件，按这个顺序读逻辑最顺：

```
L1-180    ▶ 基础设施（includes, namespace, 方向常量）
L180-445  ▶ WFC 状态结构（FWfcState, FAttemptRecord）
L445-595  ▶ Portal 对齐 + 占格枚举
L595-845  ▶ Cell 预约 + 方向性约束构建
L845-925  ▶ MergeDirectionalConstraint（约束合并）
L925-1005 ▶ BuildEndpointOptions（端点候选）
L1005-1118▶ A*→WFC 约束翻译
L1118-1323▶ WFC 核心：删除→传播→支持计数重建  ← ★ 核心
L1323-1480▶ Cell 数组构建 + 加权选择
L1480-1655▶ Observe + WfcCollapse（坍缩）
L1655-1830▶ 终态导出
L1830-1968▶ BuildVariantCatalog（变体目录）
L1968-2240▶ A* 确定性寻路              ← ★ 核心
L2240-2530▶ WFC 回溯主循环             ← ★ 核心
L2530-2978▶ GraphToGrid 放置           ← ★ 核心
L2978-3430▶ 邻接兼容性 + Foreign 检测
L3430-3804▶ RouteGraphEdgesWithAStar（路由入口）
L3804-4315▶ WFC 求解内循环             ← ★ 核心
L4315-4835▶ 后置验证
L4835-5136▶ Solve 入口 + 度量采集
```

---

*报告生成时间：2026-07-24，对应提交 `16833e3`。*
