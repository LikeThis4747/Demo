# 审计：磁力交互垂直切片

- 审计日期：2026-07-21
- 代码基线：git `1e90d46`（2026-07-20 23:04:11 +0800，chore: snapshot PCG physics planning baseline）之后的未提交工作区改动
- 审计范围：`Source/Demo` 全部 6 个 C++ 类（Character / ElectromagneticGrab / MagneticObject / MagneticPrototypeProp / GameMode / HUD）

## 结论

无阻塞项。状态 Owner 单一、engine-first 克制、释放路径全可逆、Tick 按需开关，符合项目规范。风险不在已写代码，在即将写的 PCG 与追猎者——需先钉死契约再动手。下方按优先级列建议，仅供实施 AI 参考，非必须立即改。

## 高回报（写 PCG / 追猎者之前先做）

1. **先落生成算法接口再写算法**。策划案 §3.2 要求玩法只依赖统一生成接口。动手前先建 `Public/Interfaces/IGenerationAlgorithm.h`（Seed → 抽象房间图），让 BSP/房间图可替换、可对照。否则算法与玩法耦合，后期换算法要大改。

2. **追猎者受击用委托解耦**。策划案 §2.3 的偏转/踉跄/倒地是"冲击事件→状态机"。让 `MagneticObjectComponent` 命中时广播 `FOnMagneticImpact`（质量/相对速度/接触方向），`Pursuer` 订阅自行决定反馈级别。禁止 Pursuer 反向依赖玩家组件。

3. **散配置收敛为 DataAsset**。`MagneticObjectComponent` 的 `ThrowSpeedMultiplier` / `HeldAngularDamping` 等应逐步迁到 `UMagneticObjectDataAsset`，符合规范"DataAsset 保存配置"。物体种类一多，硬编在组件里会难维护。

## 可延后（记录，等密集场景数据再定）

4. **选择系统的对象类型语义模糊**。`FindBestCandidate` 用 `OverlapMultiByObjectType(ECC_PhysicsBody)` 筛候选，但"是否磁性"由 `MagneticObjectComponent` 决定。二者靠"物体 Object Type 必须是 PhysicsBody"隐式耦合。PCG 生成的物体若 Object Type 非 PhysicsBody 会被静默漏选，难排查。方案：约定并注释"磁性物体 Object Type 必须为 PhysicsBody"，或改自定义 Object Channel。现原型 4 道具无问题。

5. **候选截断顺序不确定**。`OverlapMultiByObjectType` 返回顺序不保证按距离排。物体数超 `MaximumCandidateChecks`（32）时，被截断的可能是准星正中该选的那个 → 追逐密集场景偶发"对着却抓不到"。方案：截断前按到相机距离粗排，或压测确认密集房间不超上限。

6. **质量设置时序需自检（未读 cpp，仅据头文件推断）**。`MagneticPrototypeProp::ApplyConfiguredMass` 必须在物理体创建后调用（`BeginPlay` 而非构造函数）且用 `SetMassOverrideInKg(NAME_None, mass, true)`，否则质量不生效。头文件注释已意识到，实施 AI 落地时确认一次即可。

## 文档漂移（顺手修）

7. `PROJECT_ARCHITECTURE_RULES.md` 源码目录树未列 `Actors/` 和 `UI/`，实际已存在。按"文档冲突以真实代码为准"补上，避免后续 AI 困惑 UI 放哪。
