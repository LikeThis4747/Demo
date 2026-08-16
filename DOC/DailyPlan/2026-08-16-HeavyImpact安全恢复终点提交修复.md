# 2026-08-16 HeavyImpact 安全恢复终点提交修复

## 目标

- 修复 Heavy 起身已经找到安全站立终点，却因当前 Capsule 已穿入墙体或地板、从非法起点执行 Sweep 而交接失败的问题。
- 禁止在安全终点无法提交时直接于当前 Heavy 位置恢复碰撞、移动与 Gameplay。
- 玩家与追猎者继续共用 `UHeavyImpactResponseComponent`，不增加角色、AI 或机关特判。

## 最小实现范围

- 修改 `Source/Demo/Private/Components/Physics/HeavyImpactResponseComponent.cpp`。
- 必要时修改 `Source/Demo/Public/Components/Physics/HeavyImpactResponseComponent.h`，只增加内部放置校验 helper，不增加新状态或公共接口。
- 复用现有 HeavyImpact / CharacterImpact 合同测试；真实 Capsule 穿入墙体后的离开路径必须由 PIE 验证，不为测试额外增加公开接口或模拟 Chaos 的假夹具。
- 不修改摆锤、冲锤、玩家类、追猎者类、Physics Asset、DataAsset、AnimBP、关卡或碰撞配置。

## 实现规则

1. 保留现有骨盆附近 `0/20/40/60 cm` 候选搜索和完整站立 Capsule/可行走地面校验。
2. 正常情况下继续从 Capsule 真实当前位置 Sweep 到安全终点。
3. 若真实起点已发生阻挡重叠，则不再要求一个已经穿模的 Capsule Sweep 出来；保持当前 QueryOnly 恢复外壳，直接放置到已验证终点，并立刻用恢复后的响应再次验证完整 Capsule。
4. 任一放置或最终校验失败都不得调用 `CompleteRecovery` 在当前非法位置恢复 Gameplay。
5. 保留 1.5 秒恢复截止、0.30 秒 Snapshot 淡入和现有 Heavy 状态机；本轮不增加“最近安全锚点”、大范围回退、机关判断或新配置字段。

## 验证

- `DemoEditor Win64 Development -Module=Demo -NoEngineChanges` 冷构建通过。
- 官方 UE5.8 MCP 运行 HeavyImpact 5 项与 CharacterImpact 2 项，共 `7/7 Success`、0 warning、0 error。
- `BP_ZeroEscapeCharacter` 与 `BP_Pursuer` 均以 warning-as-error 编译通过。
- Level0 普通 PIE 启动/停止无新增 Heavy 初始化错误；墙边、地板与柱角的真实受击复现仍等待现场验收。
- PIE 复测玩家与追猎者的墙边、地板、柱角 Heavy：不得出现 `validated standing Capsule placement failed` 后原地恢复，也不得出现 CharacterMovement stuck。
- 摆锤正常击飞距离、冲锤 `PhysicalResponseScale=0.60`、起身 Snapshot 淡入保持不变。

## 基线

- 分支：`main`
- HEAD / `origin/main` / 远端 main：`48c7b7422bc98e6d4096723712d323419327561f`
- origin：`git@git.woa.com:shiqiqiwang/Demo.git`
- 写入前 `git status --short`：空
