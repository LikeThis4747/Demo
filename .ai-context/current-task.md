# Current Task

- ID：`TASK-20260727-005`
- 目标：按用户授权，为运行时 PCG 场景加入约每两个有效逻辑格一盏的 HydroLab 原始顶灯，并完成源码、构建、Presentation DataAsset 装配与用户 PIE 验收。
- 当前范围：只修改四个现有 PCG C++ 文件、项目自有 `DA_Presentation_SciFiHydroLab` 及对应任务/计划文档；不修改 WFC、Level0、第三方灯 Blueprint、测试 Harness 或小地图。
- 已确认规则：按 `(GridX+GridY)%2` 选择较少奇偶组，平局选 Start 组；若较少组不含 Start，只额外补 Start；灯位为逻辑格中心、`CeilingPivotZCm`、LampA `Roll=180°`。
- 已完成：实现前快照 `988b518` 已推送内部工蜂；四个 C++ 文件已修改且 `git diff --check` 通过；UE5.8 `DemoEditor Win64 Development` 构建 Succeeded；用户已配置并保存 Presentation DataAsset，Git 确认资产文件落盘变更。
- 当前阶段：等待用户在正常主视口 PIE 检查 Seed 12345 与至少一个不同 Seed；不运行自动化测试。
- 验收重点：灯量约为有效格一半、直路大致隔格布灯、Start 有灯、方向/高度正确、亮度与性能可接受、重新生成无旧灯残留。
- 后续：顶灯验收后再单独讨论小地图最小方案。
