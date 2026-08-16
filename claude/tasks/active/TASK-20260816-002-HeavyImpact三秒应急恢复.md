# TASK-20260816-002 HeavyImpact 三秒应急恢复

- Owner：Codex `/root`
- 任务类型：实现
- 实现文件写入权：占用
- Status：active
- Stage：代码、构建与自动化完成，待用户现场 PIE 验收
- Created：2026-08-16
- Baseline：`06b4359a15414229bc3c4098bc1c9bf420cb0638`

## 目标与验收

- 正常 Heavy 的自然稳定、物理击飞、相机跟随和现有 0.3 秒起身过渡保持不变。
- 从已提交的 Heavy 物理阶段起累计约三秒仍找不到起身位置时，才执行一次应急局部三维搜索。
- 应急候选仍限于当前骨盆附近、必须有可行走地面、完整站立胶囊无重叠，并保持在受击前的结构墙内侧。
- 不再无限重复红色错误日志；不得把角色未经验证地塞进墙体、地板或墙外。

## 修改范围

- `Source/Demo/Public/Components/Physics/HeavyImpactResponseComponent.h`
- `Source/Demo/Private/Components/Physics/HeavyImpactResponseComponent.cpp`
- `Source/Demo/Private/Physics/Tests/HeavyImpactResponseTests.cpp`
- 本任务卡与对应 `DOC/DailyPlan`。

## 明确排除

- 不修改 Heavy DataAsset、PCA、角色、AI、机关、动画、相机、关卡或 PCG。
- 不加入逐帧“最近安全位置”查询。
- 不修改、暂存或提交 `claude/_extract_ppt.py`。

## 实施检查点

- [x] 用户确认方案并授权实现。
- [x] 内部远端基线提交、推送与哈希核验完成。
- [x] 实现三秒门槛与一次性应急搜索。
- [x] 构建 DemoEditor（Development Win64，成功）。
- [x] 运行 HeavyImpact 5 项与 CharacterImpact 2 项自动化（7/7 Success，0 Error，0 Warning）。
- [ ] PIE 验证正常起身、墙边/悬空卡住与墙外拒绝。
- [ ] 独立提交、推送并交给用户验收。

## 实施结果

- 截止前继续执行既有 120 cm 地面探测、60 cm 水平候选和自然起身路径。
- 以 `MaximumSimulationSeconds + MaximumRecoveryBlockedSeconds` 作为应急门槛；当前两项默认各 1.5 秒，合计约 3 秒。
- 截止时先以当前物理骨盆为中心扩大一次向下探测；失败后才以受击前位置为第二搜索中心，但仍重新查询当前地面和完整胶囊空间。
- 应急目标提交前后均走既有完整胶囊、可行走地面和结构墙内侧验证；仅应急目标允许跳过从异常起点到安全终点的 Sweep。
- 两次搜索均失败时只记录一次错误并停止 0.2 秒无限重试；不使用未经复验的旧坐标恢复 Gameplay。
