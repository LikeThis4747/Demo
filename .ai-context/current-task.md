# Current Task

- ID：`TASK-20260723-002`
- 目标：完成《零号逃亡》实时 PCG V3.2 的表现与玩家验收，再进入追猎者和玩法闭环。
- 已实现：构造性 Progression、中央主干、Objective 双入口短回路、16 Mask 无回溯 Grid-WFC、600→300 cm 结构展开与 Runtime HISM。
- 已验证：UE 5.8 DemoEditor 构建成功；`Demo.PCG` 13/13；3 难度 × 3 Flow × 32 Seed = 288/288；NewWindow PIE 生成 27 Cells / 444 Instances / 5 HISM，Harness Teleport/Transfer 成功。
- 当前阻塞：四个 HydroLab 材质实例缺 `InstancedStaticMeshes` Usage，共同继承第三方 `/Game/SciFiHydroLab/Materials/Parents/M_HydroLab`。
- 最小方案：取得许可后只在该根材质启用 `Used with Instanced Static Meshes`，不建副本、映射或 Runtime 绕过。
- 验收边界：正常材质、接缝、碰撞、净空与玩家 Start→Exit 未验收；旧 `DA_LevelModuleCatalog` 验收前不删除。
- 2026-07-25 审计：UE Editor MCP `pong=false`，官方 MCP 工具未暴露，蓝图审计未执行。
- 下一步：用户授权根材质最小修改后，以正常渲染 NewWindow PIE 复验零 Usage 警告并完成玩家走通。
