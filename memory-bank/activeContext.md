# Active Context — Demo

## 当前焦点

- 壁挂式预判抛射 Chaos 机关当前基础版本已获用户验收。
- 当前权威参数：900 cm / 18 deg / 50 kg / 8 s，实际设计初速约 1225 cm/s。
- 先提交验收版，再审计旧代码和文档权威入口；任何清理需先展示预览并获得用户许可。

## 当前实现合同

- 发射前预测与机械转向；发射后只施加一次 Chaos 质心冲量，无推进、持续制导或 Tick。
- 弹体进入 Ballistic 后启动 8 秒 Actor LifeSpan。
- Level0 两实例层级正确，C++ 保留旧实例父级自愈。
- HeavyImpact Preparation 与 ExhaustVisualRoot/ExhaustLight 是后续受击和表现接缝，不视为旧推进残留。

## 当前验证

- Demo -NoLink、Demo-only 正式链接、官方 DataAsset 回读通过。
- 最小 PIE 覆盖实际 1224.97 cm/s、50 kg、8 s，并确认 9 秒后弹体 Actor 清除。
- 用户已验收当前基础机制；模型、细节和轻受击接入另行处理。

## 下一步

1. 完整 commit/push 当前验收版并恢复干净工作区。
2. 完成 Level0 只读残留核对。
3. 展示精确清理预览，等待用户授权后再写入。
