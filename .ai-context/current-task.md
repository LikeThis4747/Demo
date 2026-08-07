# Current Task

- 当前任务：HeavyImpact 重冲击的起身恢复桥代码与玩家/追猎者资产装配已落盘，当前阶段是补齐真实 Chaos 命中后的起身画面覆盖并由用户验收；不得写成“功能完成”或“已验收”。
- 代码与装配：新增/修改范围集中在 `Source/Demo` 的 HeavyImpact 响应组件、`UHeavyImpactAnimInstance`、恢复调参与自动化测试，以及 `/Game/ZeroEscape/Characters`、`/Game/ZeroEscape/Enemies/Animation`、`/Game/ZeroEscape/Physics/HeavyImpact` 下的玩家/AI AnimBP、角色 AnimClass 与两份 DataAsset。
- 任务卡：`claude/tasks/active/` 下当前“重冲击物理受击/起身恢复桥”任务卡；Memory MCP 现有内容未记录精确文件名，本次不猜测编号。
- 构建与自动验证：UE5.8 Demo 模块完整链接构建成功；`Demo.Physics.HeavyImpact.*` 5/5 通过，均无 warning/error；玩家与至少一个追猎者的实际 PIE AnimInstance 分别为项目玩家/AI AnimBP 类，且两者父类均回读为 `/Script/Demo.HeavyImpactAnimInstance`。
- 资产状态：玩家项目 AnimBP、玩家 BP、AI AnimBP 与两份 HeavyImpact DataAsset 已由官方 MCP 编译/保存/回读，最终均 `is_dirty=false`。
- 覆盖边界：普通短 PIE 未出现指定的 validation/PCA/AnimInstance/Skeleton/Slot/Montage 错误，但自动场景尚未覆盖真实 `committed → Downed → recovery completed`；未做真实起身画面验收。
- 下一步：由用户在 Level0 以真实 Chaos 命中观察倒地、Pose Snapshot、起身 Montage、落点/朝向与重新恢复移动；只有画面和玩法验收通过后才能关闭任务。
