# Current Task

## 当前任务

壁挂式物理制导一次性机关已从“短时推进、飞行中制导”重构为“发射前预测移动交点 → 炮管机械转向 → 一次质心冲量 → 全程 Chaos”。六文件 C++、默认 DataAsset、两个 Blueprint、Level0 转角正面迎击布局与 Demo-only 正式链接均已完成；当前等待用户执行 PIE 画面、命中、躲避、反弹和薄墙专项验收，不得写成玩法完成或已验收。

## 当前证据

- 实现前完整基线 `168316b1d3834e4ce1afc32239b54c4a457f079f` 已推送并核验。
- UHT、Demo `-NoLink` 与 `DemoEditor Win64 Development` 正式链接均成功；只更新项目 `UnrealEditor-Demo.dll`，未编译 UE5.8 引擎。
- Launcher 在预警期用 UE5.8 `SuggestProjectileVelocity` 预测移动交点并受固定中性轴 Yaw/Pitch 与机械转速约束；发射后不保留目标或制导。
- Projectile 无 Thruster、ProjectileMovement、Actor Tick 或 Powered 阶段；权威胶囊只施加一次 `Mass * LaunchVelocity` 冲量，首碰后不改速度。
- 默认值：约 1000 cm/s、25 kg、胶囊 22/45 cm、预警 0.55 s、Aim 更新 0.05 s、Yaw/PitchUp 30/18 度，HeavyImpact 准备关闭。
- 两个 Blueprint warnings-as-errors 编译保存成功；Blueprint、DataAsset、Level0 均回读为非 Dirty。
- Level0 西端新增南半幅端墙并保留北侧 300 cm 通路；转角 Launcher 为 `(49300,30400,150) / Yaw 0`，TriggerAnchor Local=(600,200,0)；出口灯已移出默认弹道。

## 并行边界

- 用户并行制作磁力投掷物 P0 Geometry Collection；`GC_MagneticCrate4_P0` 与对应任务记录属于用户/磁力任务，本机关没有修改其 C++、Blueprint、材质或运行逻辑。
- 磁力 P0 运行时代码仍未授权；HeavyImpact 继续冻结为已验收阶段基线；轻受击是独立讨论任务。
- 本机关不接 PCG Population、不做循环发射/对象池、不在首轮启用 HeavyImpact 准备。

## 下一步

1. 完成本轮记录和全工作区最终 commit/push，核验内部工蜂远端与 clean status。
2. 由用户在 Level0 运行 PIE：默认移动目标、600～700 cm 静止专项、急停/后退/横移躲避、道具阻挡、连续反弹和薄墙连续多发。
3. 根据画面结果只调必要的速度、预警、轮廓/尾迹、质量或碰撞；没有实测证据前不宣称穿墙已解决。
