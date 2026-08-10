# Active Context — Demo

## 当前焦点

- 壁挂式预判抛射 Chaos 机关当前基础版本已获用户验收，验收版提交为 `f7ae4f44f427b562ec87cfd4a0ce9a109a873747`。
- 当前权威参数：900 cm / 18 deg / 50 kg / 8 s，实际设计初速约 1225 cm/s。
- 验收后最小技术债与文档权威清理已完成并通过构建，由最终 Git 提交收口。

## 当前实现合同

- 发射前预测与机械转向；发射后只施加一次 Chaos 质心冲量，无推进、持续制导或 Tick。
- `Phase` 唯一管理 Ballistic/FreePhysics/Sleeping/Disabled；不再保留重复首次阻挡布尔状态。
- 正常弹体使用 DataAsset 的 8 秒 LifeSpan；配置或生成失败的禁用弹体 1 秒后回收。
- HeavyImpact Preparation 与 ExhaustVisualRoot/ExhaustLight 是后续受击和表现接缝，不视为旧推进残留。

## 当前验证

- 用户已验收当前基础机制；模型、细节和轻受击接入另行处理。
- Demo `-NoLink` 与 DemoEditor 正式链接成功，只构建 Demo 项目模块。
- 独立代码/文档复核无 Blocker/High，未修改 Blueprint、DataAsset 或 Level0。

## 后续边界

- 后续如需模型更换、表现细节、轻受击或边界专项，另开独立增量。
