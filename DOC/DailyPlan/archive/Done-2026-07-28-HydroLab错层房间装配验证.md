# 2026-07-28 HydroLab 错层房间装配验证

> 状态：样板搭建阶段已归档；已有装配、保存重载与技术验证证据，未完成的玩家实走和最终视觉验收不记为通过。当前只保留 2026-08-03 隐藏导航坡面验收计划继续跟踪楼梯手感。

## 目标

只读参考 SciFiHydroLab 的 Demonstration 与 Overview，在 Level0 中搭建一套包含楼梯、错层平台和多种空间连接方式的低重复房间原型，为后续 WFC 垂直连接与多格组合约束提供真实装配依据。

## 范围

- 修改：`D:\UE5projects\Demo\Content\Levels\Level0.umap`
- 记录：`D:\UE5projects\Demo\claude\tasks\active\TASK-20260728-002-HydroLab错层房间装配验证.md`
- 不修改：`D:\UE5projects\Demo\Content\Assets\SciFiHydroLab\**`、PCG/WFC C++、SFCorridors。

## 步骤

1. 捕获并检查 Demonstration 的房间、楼梯、平台与边界处理。
2. 在 Overview 和 Asset Registry 中核对候选网格尺寸、Pivot 与用途。
3. 加载 Level0，选择不干扰既有玩法的隔离区域和 Outliner 文件夹。
4. 搭建入口、主厅、楼梯、上层或半层平台、侧向空间与开放边界。
5. 保存并重新加载 Level0，检查引用、拼缝、碰撞、净空与可行走性。
6. 整理可转化为 WFC 的垂直 Socket、多格占用、落脚区、护栏和房间邻接约束。

## 验证与回退

- 验证：编辑器视口截图、Actor/Transform 清单、Level0 重载、碰撞检查与 PIE 行走检查。
- 回退：本轮所有新增 Actor 放在独立 Outliner 文件夹中；如原型不合格，可按该文件夹精确删除，不触碰 Level0 既有 Actor。
- 状态：已获用户授权实施；最终结果仍待用户验收。
