# Active Context — Demo

## 当前已确认基线

- PCG Population 已完成机关优先、资源后置的分层放置并由用户验收；Normal Seed 12345 代表结果为机关 36/36、资源 13/13。
- HeavyImpact 已验收：真实 Chaos 位移、有限 Physics Control、Snapshot 起身、同来源保护；HeavyImpact 5/5。
- 追猎者近战斧击与预判跑跳 Heavy 攻击已经技术交付并由用户看到击飞效果；不要无依据调整其预测、命中或击飞参数。
- Light 当前验收：制导/磁力命中玩家为 Slow 0.40 秒、倍率 0.55、无动画、局部物理；磁力命中追猎者为 Stop 0.60 秒、三方向动画、局部物理。异常前冲已修复；局部物理偏弱但用户接受当前版本。
- 新机关受击接入权威：`DOC/Outputs/Physics/CHARACTER_IMPACT_INTEGRATION_GUIDE.md`。Light/Heavy 接收核心不包含机关类型特判。

## 当前 P0

1. 补齐玩家 Stop 的 Front/Left/Right 方向受击动画；当前 `DA_PlayerStandingImpact` 三个动画引用为空。计划：`DOC/DailyPlan/2026-08-13-玩家Stop方向受击动画补齐.md`。
2. 在同一正式一局中证明动态 Recast 和真实追猎者多层追逐。
3. 用户验收追猎者跑跳/近战的预警、可躲性、落空恢复与动画观感。
4. 确认并灰盒化最小目标链与首批音效。
5. 完成首轮 Development 打包与整局回归。

## 受击架构边界

- 新机关 None/Slow/Stop：来源 DataAsset + 稳定事件 ID + 一次 `FStandingImpactRequest`；不得修改 CharacterImpact 核心或 Cast 具体角色。
- 新机关 Heavy：机关侧实现接触前预测并由真实动态刚体接触；不得在 Heavy 失败后对同一接触降级补 Light。
- 机关自己的碰撞、销毁、伤害、持续力和破碎不进入角色受击组件。
- 不恢复全身常驻受控物理、Light/Heavy 大一体化或跨对象磁力租约方案。

<!-- written by shiqiqiwang at 2026-08-13 13:40 UTC -->

## 2026-08-13 刺轮技术初版（待手感验收）

- Level0 已放置一个项目自有刺轮：按实例 Seed 从对应 1～3 格路线模板中选择镜像、方向和起始相位；开放路线往返，闭合路线循环。
- 玩家接触提交 Stop 并扣 20 点生命；机关自身使用离开再进入 + 1.0 秒局部门禁，追猎者映射 None。玩家受击动画仍由独立任务处理。
- 当前只验证手工两格实例，不接入 Population/PCG；跨格足迹、占用格合同和组合投放留到用户确认路线压力后再设计。
