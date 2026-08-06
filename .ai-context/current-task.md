# Current Task

- 当前任务：完成玩家/追猎者共享 HeavyImpact 物理受击原型的正式链接、A/B 对照、PIE 视觉验收，并规划真实倒地点起身恢复。
- 已完成：玩家/追猎者两份 PCA 已创建、Compile、保存并接入各自 Tuning DataAsset；角色 Blueprint 已装配；四项 HeavyImpact 自动化曾在上一版正式 DLL 中通过。
- 已完成：摆锤改为沿走廊纵向摆动，振幅 35°、主轴限位 40°、PreparationLookAheadDistance 600 cm。运行态已确认 Bob 为 QueryAndPhysics、Simulate=true、CCD=true、1000 kg，并沿 Y 轴连续摆动。
- 已完成：机关预测改为查询 HeavyImpact Receiver 提供的真实 Skeletal Mesh，并使用 SkeletalMesh 的全 Physics Asset 距离查询，避免只看 Capsule 或根刚体。
- 新增待链接代码：Prepared/Flight/Landing 分阶段 ParentSpace 控制倍率；开发 CVar `demo.HeavyImpact.PureRagdoll 0/1` 在同一碰撞、BodyModifier、状态机下切换受控 Physics Control 与 Controls 全关的普通 ragdoll。
- 新倍率默认值：Prepared Strength/Damping/Torque = 2/1.25/3；Flight = 1.75/1/3；Landing = 2.25/1.35/3.5。无 WorldSpace 控制、LinearStrength 仍为 0，不锁定骨盆世界位移。
- 新代码已通过 `DemoEditor Win64 Development -Module=Demo -NoLink -NoEngineChanges -NoHotReloadFromIDE -WaitMutex`；正式 DLL 尚未链接，运行时 A/B 尚未验证。
- 用户已关闭 Editor，并要求先提交/推送当前完整改动，再正式构建。
- 起身现状：Downed 保留真实倒地点；Recovering 只有枚举、尚无实现。玩家/AI AnimBP 均有 DefaultSlot，但项目内没有地面起身动画，且两者骨骼不同。
- 下一步：提交并推送 -> 正式链接 -> 重启 Editor -> 官方 MCP 回读新倍率 -> 自动化回归 -> PIE A/B；随后提交详细起身实现方案与资产/代码分工。

<!-- written by shiqiqiwang at 2026-08-06 14:39 UTC -->

## 并行交付记录：壁挂式物理制导一次性机关

- 独立 V1 的 C++、Blueprint/DataAsset/材质与 Level0 双实例已落盘；Review 公共层建议按用户决定延后到功能稳定后再评估。
- UE 编辑器已正常关闭；最终仅以 `-Module=Demo -NoEngineChanges` 构建 Demo 模块，结果成功且 0 个动作，没有编译引擎。
- 未运行 PIE；玩家/追猎者触发、首碰失导、多次反弹与手感由用户次日验收。
- 当前提交完成并释放工作区后，通知 HeavyImpact 任务先阅读其拟实现代码 Review，再开始实现与 Demo 模块构建。

<!-- written by shiqiqiwang at 2026-08-06 15:04 UTC -->

## 并行交付修正：壁挂式物理制导机关现场布局

- 用户现场 Review 否决了远端 `PCG_Test/Room900` 摆位；两个既有 Launcher 已全部迁出。
- 新测试区位于 `PendulumTestRoom` 画面右侧，是 300 cm 高的 L 形低顶走廊；与左侧冲锤房、中间摆锤房形成并排对照。
- 新文件夹回读 47 个 Actor（45 个房间构件 + 2 个 Launcher）；旧机关测试文件夹已不存在，两条中心线和两个发射口前 200 cm 均静态净空。
- Level0 已保存，UE 编辑器已正常关闭；仅构建 Demo 模块并成功、0 个动作，未运行 PIE。
- 本轮完整提交/推送并恢复干净工作区后，通知 HeavyImpact/起身任务先读其拟实现代码 Review，再开始实现起身代码。
