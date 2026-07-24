# Latest Error

当前无已知 C++ 编译、Blueprint 编译或 `Demo.PCG` 自动化错误。UE 5.8 完整构建成功，新测试 13/13，288 组 Seed Sweep 全通过；NewWindow PIE 的 Runtime Generation 与 Harness Teleport/Transfer 均成功。

当前确认的表现错误：HydroLab 的 `MI_HydroLab_Ceiling01`、`MI_HydroLab_Wall01`、`MI_HydroLab_Trim01`、`MI_HydroLab_Floor03` 缺少 `InstancedStaticMeshes` Usage，HISM 渲染会回退为默认材质。四个实例共同继承 `/Game/SciFiHydroLab/Materials/Parents/M_HydroLab`。

最小修复是只修改并保存该第三方根材质的一项 `Used with Instanced Static Meshes`。因用户尚未授权修改 HydroLab 素材，本轮没有创建材质副本、映射或运行时绕过；等待许可后直接修复并重跑 PIE。
