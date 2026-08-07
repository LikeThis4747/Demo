# Active Context — Demo

> 当前任务详情以 `.ai-context/current-task.md` 为唯一来源。

## 当前焦点

HeavyImpact 起身恢复桥已完成代码与玩家/追猎者资产装配、完整链接和自动验证；当前只补真实 Chaos 命中后的完整运行证据与用户画面验收，不继续扩展新机关或抽象公共层。

## 已确认

- 共享 HeavyImpact 仍由真实 Chaos 接触决定位移，角色侧不补 `AddImpulse`/`LaunchCharacter`；旧追猎者局部 Physics Control 受击路径完整保留，但运行装配保持停用。
- 玩家/AI 使用项目 AnimBP，均继承 `UHeavyImpactAnimInstance`，Pose Snapshot 与起身 Montage 桥已装配；Demo 模块完整构建成功，HeavyImpact 自动化 5/5 通过。
- 不再需要对玩家 AnimBP 做 Target Skeleton 重定向。玩家副本保留源资产的 `SK_Mannequin` TargetSkeleton 是预期状态，不把它误判为 Montage 播放硬门槛。
- UE5.8 该运行路径中，AnimInstance 运行时 Skeleton 来自当前 SkeletalMesh；动态 Montage 的 Skeleton 来自动画序列。玩家实际 Mesh 与两条玩家起身序列均使用 CH_SciFiTrooper 骨架，因此无需 Skeleton Compatibility 或额外重定向绕过。
- 短 PIE 已确认玩家与追猎者实际 AnimInstance 分别为对应项目 AnimBP 类，且两者父类均为 `HeavyImpactAnimInstance`；本轮未见 validation/PCA/AnimInstance/Skeleton/Slot/Montage 错误。
- 玩家 AnimBP、玩家 BP、AI AnimBP 和两份 HeavyImpact DataAsset 最终均非脏。

## 当前门槛

1. 让玩家或追猎者取得真实 `committed → Downed → recovery completed` 日志和连续画面；现有自动场景及两次临时定点未覆盖摆锤 Chaos 接触。
2. 用户验收倒地姿势、仰/俯面动画选择、起身落点/朝向、Montage 混合和恢复移动；此前验证不能替代画面验收。
3. 保留首次 EditorExit 在 UnrealEd/Slate 的单次 AV 与第二次正常退出记录，不把它归因于起身代码；只有复现后再单独诊断。
