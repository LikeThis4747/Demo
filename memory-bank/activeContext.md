# Active Context — Demo

## 当前焦点

- 壁挂式物理制导一次性机关已完成“发射前预判 + 单次质心冲量 + 全程 Chaos”实现、资产装配、Level0 正面迎击布局和 Demo-only 构建，等待用户 PIE 手感验收。
- 磁力投掷物 P0 Geometry Collection 由用户并行制作；运行时 C++/Blueprint 破碎逻辑仍未授权。
- HeavyImpact 重受击继续作为已验收阶段基线；轻受击融合为独立讨论任务。

## 机关当前合同

- 首个角色触发后预警 0.55 秒；预警期 Timer 预测移动交点并机械转炮管，数学无精确解仍按可解释 fallback 发射。
- Muzzle 是出口平面，ProjectileSpawnPoint 是真实质心；求解、出生检查、SpawnTransform 和一次 Chaos 冲量共享同一起点。
- 发射后无 Thruster、ProjectileMovement、Tick、目标引用或纠偏；首个 Blocking Hit 只切自由物理阶段和碰后阻尼，不改 Transform/速度。
- 默认约 10 m/s、25 kg、胶囊 22/45 cm、CCD；HeavyImpact 准备关闭。
- Level0 转角机关装在西端南半幅墙上朝东，北侧通路保留，TriggerAnchor Local=(600,200,0)，挡弹灯具已移开。

## 当前验证边界

- UHT、Demo -NoLink、Demo-only 正式链接、Blueprint warnings-as-errors、DataAsset/组件树/Level0 回读均通过。
- 未运行 PIE；不能把静态构建、视口和资产非 Dirty 当成可读性、命中率或不穿墙验收。
- 300 cm 低顶下 18 度上限的粗略余量约 20～24 cm，最终仍需真实弹体与天花板接触专项。

## 下一步

1. 收口本轮完整 Git commit/push 与远端核验。
2. 用户执行默认迎面移动、近距静止、预警期躲避、资源阻挡、反弹与薄墙 PIE 矩阵。
3. 只依据运行画面决定调参或最小修正；磁力 P0 与轻受击继续保持独立边界。
