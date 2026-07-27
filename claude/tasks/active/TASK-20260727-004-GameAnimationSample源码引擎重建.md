# TASK-20260727-004 - GameAnimationSample 源码引擎重建

- Owner：Codex / root（本对话）
- Status：waiting_user_acceptance
- Stage：构建及启动验证通过，等待用户目视验收
- Created：2026-07-27
- Updated：2026-07-27

## 目标与验收

- 目标：使用 `D:\UE5_8` 为 `D:\UE5projects\GameAnimationSample\GameAnimationSample.uproject` 重建 `GameAnimationSampleEditor`。
- 验收：`Development Editor | Win64` 构建成功；启动编辑器后不再报告 Chooser、Mover、PoseSearch 等 Engine module BuildId 不兼容。
- 非目标：不修改 `Content`、`Config`、项目源码或玩法资产；不删除旧编译产物。

## 修改范围

- 允许生成或更新：`D:\UE5projects\GameAnimationSample\Binaries`、`Intermediate`、解决方案/工程文件及 `Saved\Logs`。
- 允许生成或更新：`D:\UE5_8\Engine\Binaries` 与所需 Engine Plugin 的 `Binaries`/构建中间产物。
- 不允许：删除用户文件，修改 Demo 功能代码或第三方内容资产。

## 计划与检查点

- [x] 从日志确认 Engine/Plugin/Project BuildId 混杂。
- [x] 用户明确授权使用 `D:\UE5_8` 重建。
- [x] 尝试生成工程文件；确认无 Source 的官方内容示例不能独立生成解决方案。
- [x] 由 UBT 确认可为该项目自动创建临时 Target，无需新增占位源码。
- [x] 使用项目参数构建 `UnrealEditor Win64 Development`，由 UBT 自动生成临时 Target 并统一插件模块清单。
- [x] 启动并检查最新日志。
- [x] 记录验证结果，等待用户最终验收。

## 验证结果

- 构建：`Build.bat UnrealEditor Win64 Development -Project=...GameAnimationSample.uproject -WaitMutex`，Result `Succeeded`。
- 模块：Chooser、ProxyTable、PoseSearch、Mover、TargetingSystem、LiveLinkControlRig、AnimationLocomotionLibrary 等均成功 `InternalLoadLibrary`；未再出现 `Incompatible or missing module` 或 `Engine modules cannot be compiled at runtime`。
- 运行：编辑器进程正常响应，窗口标题为 `GameAnimationSample - 虚幻编辑器`；引擎初始化完成并加载 `/Game/Levels/DefaultLevel`。
- 保留项：项目自带的官方 55116800 占位 DLL 仍被安全跳过；`.uproject` 未声明该代码模块，因此不阻塞启动。未删除或修改 Content、Config、项目源码。

## 风险

- 首次统一构建可能编译数十个实验性动画插件，耗时和磁盘写入较大。
- 若旧临时 Target 阻碍生成，可能需要清理项目 `Binaries/Intermediate`；任何删除前必须再次征得许可。
- 后续若再次单独重建 `D:\UE5_8` 的 Editor Target，可能再次改变 BuildId；应继续使用带 `-Project=GameAnimationSample.uproject` 的构建命令同步该示例依赖的插件清单。
