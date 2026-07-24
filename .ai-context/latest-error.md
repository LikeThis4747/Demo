# Latest Error

当前无已知 C++ 编译或 `Demo.PCG` 自动化错误。材质映射层清理后，`DemoEditor Win64 Development` 构建成功；全新命令行编辑器运行 `Demo.PCG` 13/13、0 warning、0 error。

此前首次真实 PIE 因过度设计的材质映射只覆盖三项、漏掉 `MI_floor` 而在配置阶段 fail-closed；该映射机制现已完整删除，不再作为当前错误。用户已明确授权并保存三个 SFC 根材质的 `Used with Instanced Static Meshes`，其材质实例无需单独修改。

当前待验证项不是已确认错误：必须在全新正常渲染编辑器进程的第一次 NewWindow PIE 中确认根材质 Usage 持久化、零相关 warning、Generator 到达 Ready 且 Harness 成功传送。NullRHI 自动化不能替代该门禁。
