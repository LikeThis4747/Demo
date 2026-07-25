# Latest Error

当前无已知 C++、Blueprint、自动化或 HydroLab HISM Usage 错误。

2026-07-25 已在用户授权后，只为第三方共同根材质 `/Game/SciFiHydroLab/Materials/Parents/M_HydroLab` 启用并保存 `Used with Instanced Static Meshes`。全新正常渲染 NewWindow PIE 生成成功，Harness 传送成功，日志中不再出现 HydroLab、InstancedStaticMeshes、Usage Flag 或 Default Material 相关警告。

一次性作者化脚本首次因 UE 5.8 Python 不暴露 `post_edit_change()` 而在保存前失败；确认资产未变后删除该非必要调用，第二次回读 `before=0 → after=1`、`saved=1`，脚本随后删除。当前仅剩用户视觉、碰撞和走通验收，不属于已确认错误。
