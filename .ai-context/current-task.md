# Current Task

## 当前任务

统一轻受击响应第一版已完成代码、配置、DataAsset 与 Blueprint 装配，当前等待用户现场手感验收。实施基线为 `aa00afca171cc4864e742f83f1815a2d9c1a8111`。

## 当前实现

- 玩家与追猎者共享 Standing Impact 请求、方向/强度、去重与 Heavy 抢占；来源 Profile 分别映射 `None / Slow / Stop`。
- 磁力投掷物：玩家 None，追猎者 Stop 0.60 秒并播放方向动画。
- 地刺：玩家 Stop 0.25 秒；追猎者 Slow 0.60 秒、速度倍率 0.45。
- 空中 Stop 只清水平速度，保留 Z、Falling 与重力。
- AI Stop 结束后即使旧攻击冷却未结束也恢复追击移动，但冷却仍限制下一次攻击。
- HeavyImpact 保持唯一击飞、PCA、倒地和起身权威，内部未修改。

## 技术验证

- DemoEditor 只构建 Demo 模块成功，使用 `-Module=Demo -NoEngineChanges`。
- CharacterImpact 2/2 + HeavyImpact 5/5，合计 7/7 自动化成功。
- 四个 Blueprint 和四份新 DataAsset 已编译/保存/官方 MCP 回读。
- 短 PIE 初始化与运行时碰撞基线回读正常，无轻受击配置错误。

## 待用户验收

真实磁力命中、地刺玩家/AI手感、空中 Stop、Light/Heavy 交叉抢占及不同帧率边界。玩家三条方向动画仍待人工 Replace Skeleton 与 Persona 观感确认；当前字段有意留空，不使用绕过。
