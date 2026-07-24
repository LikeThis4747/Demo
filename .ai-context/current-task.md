# Current Task

- ID：`TASK-20260723-002`
- 目标：实现《零号逃亡》实时、非工具性 PCG 整关生成；SFCorridors 是当前正式且可替换的表现素材。
- 当前链路：Progression/Spatial Graph → Special Socket → 确定性 A* → 有限 WFC → Closure → 实际 Placement 图验证 → Runtime HISM。
- 当前关卡：`/Game/Levels/L_PCG_RuntimeTest` 已保存 Generator、测试灯光、Staging、PlayerStart 与 `BP_RuntimeGenerationTestHarness`；Harness 显式引用 Generator/Staging。
- 材质决定：用户明确允许直接修改 SFCorridors；`M_Master`、`M_Glass_Panels_NM`、`M_MasterTransp` 已启用并保存 HISM Usage，材质实例无需修改。过度设计的 Source→Target 映射层、六个副本与作者化脚本已删除，Presentation 恢复 Version 1。
- 已验证：映射清理后 `DemoEditor Win64 Development` 完整构建成功；全新命令行进程运行 `Demo.PCG` 13/13，0 warning、0 error，集成烟测从磁盘读回 Version 1 Presentation 与六个 SFC Binding。
- 诚实边界：命令行 NullRHI 自动化不证明正常渲染材质、PIE Ready、Harness 传送、接缝、碰撞、净空、导航、性能或玩家走通。
- 下一步：用户重新打开 UE 并打开测试关卡；执行全新进程第一次 NewWindow PIE，检查零 HISM Usage 警告、`ZE_PCG_RESULT Success=1 State=Ready`、Harness 传送，然后由用户实际走 Start→Exit。
- 不修改：`Level0`、追猎者、陷阱、Config、插件或其他 SFCorridors 资产。
- 当前文档：`DOC/DailyPlan/2026-07-23-PCG整关场景方案冻结.md`。
