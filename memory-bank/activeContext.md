# Active Context — Demo

> 当前任务详情以 .ai-context/current-task.md 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

PCG 多层关卡完整代码预览已完成首轮审查修订，当前等待复审。正式功能源码、DataAsset、蓝图、关卡与配置尚未获得落盘授权。

## 当前决定

- DataAsset 提供实际 `GridSize`；2/3/4 层按难度权重抽取；玩家与追猎者在一楼，Exit 在最高层。
- 每对相邻楼层至少一座必需双层楼梯；额外楼梯远离端点并公平分配。贯通三层的楼梯间不能替代必需楼梯。
- 高天花板房间允许某层为零和顶层生成；先放完整跨层结构，再逐层复用二维 WFC，最后一次检查整栋可达。
- 删除旧 Room 链；RegionKind 先迁移真实消费者再删除。
- Level0 隐藏坡面直接复用；不创建临时导航实验。最多 20 点/19 条真实路径；10 秒只是不无限等待的保护。
- Generator 单 Actor 生命周期只接受一次请求；自动重试创建新 World，不做原地重生成。
- 仅 Seed 可能改变的生成/最终导航失败允许确定性换 Seed，最多跨 World 重试 3 次；固定配置、装配和导航准备失败直接回菜单。正式游戏关卡由 GameMode 蓝图软引用配置。
- 首轮审查只采纳必要去重和契约修正；拒绝拆 Planner、新子系统/组件、加载界面、删除累计 Spawn 预算与弱化导航验收。

## 当前证据

- 代码预览：`claude/proposals/2026-08-03-PCG-MultiFloor-CodePreview/`。
- 首轮审查归档：`claude/reviews/archive/Done-2026-08-03-PCG-MultiFloor-CodePreview-审查.md`。
- 五片补丁顺序应用与 diff check 通过；UE 5.8 UHT、Demo 模块编译/DLL 生成通过；`Demo.PCG` 24/24、`Demo.GameFlow` 2/2 通过。
- 正式资产、真实运行时 RecastNavMesh、PIE、玩家和追猎者验收未执行。

## 当前边界

- 权威 Plan：`DOC/DailyPlan/2026-08-03-PCG多层关卡正式实施计划.md`。
- 当前任务卡：`claude/tasks/active/TASK-20260802-001-PCG多层WFC新方案讨论.md`。
- 复审通过且用户再次明确允许后，才按五片顺序落入正式源码并迁移资产。
