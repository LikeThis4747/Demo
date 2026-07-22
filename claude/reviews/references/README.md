# 优秀项目经验（按三周单机 Demo 裁剪）

参考项目：
- Lyra：`D:\UE5projects\LyraStarterGame`（Epic 官方样板，450+ 文件，重联机/模块化/GAS）
- ue5-warrior：`D:\UE5projects\ue5-warrior`（单机 Action RPG，108 文件，GAS+接口+DataAsset）

原则：Demo 无网络同步、工期三周，只提取通用架构原则，**不照搬大型工程的分层深度**。每条都标了"学 / 别学"。

## 目录组织

- **学（ue5-warrior 级）**：`Public/Private` 镜像 + 按稳定职责分子目录（Characters/Components/Interfaces/DataAssets/GameMode/AI/Widgets/WTypes）。Demo 现有 `Actors/Characters/Components/GameFlow/UI` 就是这个粒度，正确。
- **别学（Lyra 级）**：Lyra 分了 20+ 顶层目录（Teams/Cosmetics/Hotfix/Replays/GameFeatures…），是为可插拔 GameFeature 和联机准备的。Demo 用不到，硬套只会增加认知负担。

## 分层与解耦

- **学**：接口查询 + 委托通知 + 组件承载可复用职责。Warrior 用 `Interfaces/`（PawnCombat 等）解耦角色与能力，用 `DataAssets/` 数据驱动配置——这正是 Demo 该对 PCG 生成算法和磁性物体做的。
- **学**：极薄 Character。Lyra `LyraCharacter` / Warrior 基类角色都只做装配转发，逻辑在组件。Demo 的 `ZeroEscapeCharacter` 已做到。
- **别学**：Lyra 的 `LyraExperience`/GameFeature 动态加载玩法。Demo 单一玩法，静态引用即可。

## GAS（GameplayAbilitySystem）

- Warrior/Lyra 都重度用 GAS 做能力/属性/效果。
- **Demo 决策：不用 GAS**（规范已明确）。磁力抓取用普通 ActorComponent 状态机足够，引入 GAS 的属性集/GE/Tag 网络在三周单机里是负收益。仅当后续做肉鸽升级且能力组合爆炸时才重新评估。

## GameplayTag

- **可轻量学**：Warrior 用 `WGameplayTags.h` 集中声明原生 Tag 做稳定语义标识。Demo 规范也允许"稳定语义标识可用 GameplayTag"。若追猎者状态/物体类别需要稳定标识，用集中声明的原生 Tag 比字符串/枚举更抗重构。别为用而用。

## 日志与调试

- **学**：Lyra `LyraLogChannels`、Warrior `WDebugHelper` 各自定义日志类别/调试宏。Demo 现在靠静默 return，建议加一个轻量 `DemoLog` 类别，把关键失败（Configure 未就绪、抓取被漏选）打出来，便于答辩期定位。低成本高回报。

## 命名与文件头

- **学**：三级注释（文件头职责 / 函数用途 / 属性语义），Demo 现有代码已是行业顶尖水平，保持即可。

## 一句话

Demo 该对标的是 **ue5-warrior 的分层粒度**，不是 Lyra 的工程规模。学它的"接口+组件+DataAsset 解耦"和"极薄 Character"，跳过它的 GAS 和联机。
