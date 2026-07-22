# 审计：DataAsset 化重构 + 蓝图

> Done（2026-07-22，处理 AI：Codex）：阻塞项 1 已由后续蓝图装配与 PIE 日志验证消项；建议 2-4 与文档漂移不属于本次输入焦点修订，已明确延后给磁力/规范任务，不夹带实现。本报告已完成分流，不再作为当前输入修复阻塞。

- 审计日期：2026-07-21（当日第 2 次）
- 代码基线：git `1e90d46`（2026-07-20 23:04:11 +0800）之后的未提交工作区改动；`Source/` 全目录仍未纳入版本控制
- 范围：8 个 C++ 类（新增 `MagneticGrabTuningData`、`ZeroEscapeInputConfig` 两个 DataAsset）+ 4 个蓝图（`BP_ZeroEscapeCharacter` / `BP_MagneticProp` / `BP_ZeroEscapePrototypeGameMode` / 未查的 GameMode 引用）+ 2 个 DA 实例
- 前一份报告 `2026-07-21-magnetism-vertical-slice.md` 尚未 `Done-`：其高回报建议 3（DataAsset 收敛）和问题 4（Configure 时序防御）本次已落实；可与本报告一并归档，无需重复处理。

## 结论

代码相比上次明显进步：手感/输入已 DataAsset 化并带 `IsConfigured` 校验、`bConfigurationReady` 缓存校验 + 全入口防御、加了日志类别、`UnPossessed` 清理 + 防幽灵输入。无阻塞项。蓝图侧干净（纯 C++ 父类、无图逻辑、编译 UpToDate）。仅两处需运行时确认 + 上次两条选择系统问题仍在。

## 阻塞交付（需运行时确认，不确认可能整功能失效）

1. **BP 里 `InputConfig` 与 `ElectromagneticGrab.TuningData` 是否已赋值——MCP 读不到，必须目视/日志确认**。二者是 C++ `EditDefaultsOnly` 裸 `TObjectPtr`，未赋值时 C++ 会走 `UE_LOG(Error)` 并**整体停用输入 / 磁力**（非崩溃，静默失效）。`DA_ZeroEscapeInput` / `DA_MagneticGrabTuning` 资产存在 ≠ 已在 BP 引用。验证：PIE 启动看 `LogZeroEscapeInput` / `LogZeroEscapeMagneticGrab` 有无 Error；或在 BP Class Defaults 目视两个引用非空。这是"功能完全失效"头号原因，优先排。

## 高回报

2. **`BP_MagneticProp` 多了一个组件 `MagneticMesh`（StaticMeshComponent，挂在 root `MagneticBody` 下）**。C++ 父类的 `MagneticBody` 已是可见 Cube + 模拟物理 + 碰撞。这个子 Mesh 若无独立用途就是冗余：徒增一个组件、可能遮挡或产生多余碰撞体。确认它是有意的纯装饰（应关碰撞、不模拟物理）还是误加；无用途就删。

## 可延后（上次已提，逻辑未变）

3. **选择用 `OverlapMultiByObjectType(ECC_PhysicsBody)` 隐式耦合**。`CanGrab` 只判 `IsSimulatingPhysics`，但候选查询只捞 Object Type=PhysicsBody 的物体。PCG 生成物若 Object Type 非 PhysicsBody 会被静默漏选。方案：约定并注释"磁性物体 Object Type 必须 PhysicsBody"，或改自定义 Object Channel。现原型无问题。

4. **候选截断顺序不确定**。`Overlaps` 顺序不保证按距离；超 `MaximumCandidateChecks`（默认 32）时可能截掉准星正中该选的。追逐密集场景偶发"对着抓不到"。方案：截断前按到相机距离粗排，或压测确认不超限。

## 已消项（本次核实，无需再看）

- 上次问题 6（质量设置时序）：`MagneticPrototypeProp::BeginPlay` 已正确调 `SetMassOverrideInKg(NAME_None, mass, true)`，正确。
- 上次建议 3/4：DataAsset 收敛 + Configure 校验防御 + 日志，均已落实。

## 文档漂移（仍未修）

5. `PROJECT_ARCHITECTURE_RULES.md` 源码目录树仍未列 `Actors/`、`UI/`、`Data/`（实际已有 `Data/Magnetism`、`Data/Input`）。按"文档以真实代码为准"补上。

## 审计侧未覆盖（能力边界，非代码问题）

- 两个 DA 实例的具体数值、BP 继承属性赋值：当前 ue-editor-mcp 无读取 CDO/DataAsset 属性值的 action（已换 3 组关键词确认），只能运行时或目视核对。GameMode 的 `DefaultPawnClass` 等 Class Defaults 同理。
