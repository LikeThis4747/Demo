# Current Task

## 轻受击动画与局部物理融合（2026-08-12）

- 用户已授权并确认当前实现方向。
- 功能代码、追猎者与磁力来源 DataAsset 装配已完成。
- Demo 模块构建、CharacterImpact 2/2、HeavyImpact 5/5、官方 MCP 资产回读和短 SIE 初始化通过。
- 当前仍是技术交付，不是效果验收；任务保持 active。

## 当前权威边界

- 保留 StandingImpact 的 None/Slow/Stop、速度效果、攻击打断和 Stop 方向动画。
- 可选物理层使用 UPhysicalAnimationComponent：追猎者上半身短时模拟、封顶表现冲量、Hold/Blend Out；玩家和地刺首轮关闭。
- Heavy 只新增校验成功后、捕获前清场 Delegate；Heavy 状态机、PCA、击飞、倒地、起身与误预测恢复不变。
- 不改磁力/破碎/地刺来源 C++、碰撞路由、ABP、Level0、Config 或 UE5.8 引擎。
- 当前 Mesh 为 QueryOnly，因此该层是局部骨骼物理表现，不宣称第一次接触中的完整双向动量交换。

## 下一步

1. 用户用磁力物验收胸口、左侧、右侧命中画面。
2. 验证连续命中后的退出与无永久模拟。
3. 验证 Light 活动时 Heavy 抢占，以及 Heavy 起身后再次 Light。
4. 最多做有限 A/B；若仍不可接受，停止扩写来源特判或共享身体系统并回到方案评审。
