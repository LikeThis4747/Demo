# Active Context — Demo

## 当前焦点

- 壁挂式预判抛射 Chaos 机关已完成代码、资产、Level0 和触发缺陷修复；等待用户继续画面与玩法验收。
- 磁力投掷物 P0 Geometry Collection 与轻受击方案为并行独立任务，当前不与机关 Source/Content 交叉写入。

## 机关当前合同

- 首个 ACharacter 进入 Trigger 后预警 0.55 秒；预警期 Timer 预测移动交点并机械转炮管，发射后只保留一次 Chaos 初速度。
- Muzzle 是出口平面，ProjectileSpawnPoint 是真实质心；发射后无 Thruster、ProjectileMovement、Tick、目标引用或纠偏。
- Level0 两个旧实例曾保留 `Muzzle -> SceneRoot` 并在 BeginPlay 自禁用；现已正式保存为 `AimPivot -> Muzzle -> ProjectileSpawnPoint`。
- C++ 会安全恢复其他旧关卡实例的同类父级覆盖，并在恢复失败时输出旧/新父级后禁用。

## 当前验证边界

- UHT、Demo -NoLink、Demo-only 正式链接、Blueprint/DataAsset/Level0 回读均通过。
- 定点 PIE 已覆盖两个实例 `Armed -> BeginOverlap -> Warning -> Fire`，原“完全不触发”错误已关闭。
- 尚未由用户验收弹体可读性、移动命中、躲避、反弹与薄墙穿透；CCD 仍不能写成绝不穿透。

## 下一步

1. 收口本轮完整 Git commit/push 与远端核验。
2. 用户重新进行 Level0 画面和玩法测试。
3. 只依据运行画面决定后续速度、预警、尾迹、质量或碰撞调整。
