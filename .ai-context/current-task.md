# 当前任务

- 追猎者折返楼梯隔层攻击最小修正，任务卡：`claude/tasks/active/TASK-20260813-002-PCG地图追猎者追丢诊断.md`。
- 用户确认采用第二方案并授权实现：近战高度差上限为 70 cm；高度接近时保留挥斧，超过上限时继续严格位置寻路。
- 唯一实现文件为 `Source/Demo/Private/AI/PursuerAIController.cpp`；没有新增状态机、DataAsset、Blueprint 或导航系统。
- 实现基线 `593eb3e673dfb6f4f3cba5d64358dbed593ecd14` 已推送内部工蜂并完成远端哈希核验。
- 技术验证：`git diff --check` 通过；`DemoEditor Win64 Development` 完整编译、链接成功；`Demo.Combat.PursuerAttack.PredictionAndBallistics` 自动化 1/1 Success。
- 待办：用户重启编辑器后在原折返楼梯复验——高度差超过 70 cm 时持续爬楼，追近后仍会挥斧。未经验收不得归档。
