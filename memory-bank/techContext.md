# Tech Context — Demo

## Git 仓库
- 远程平台：工蜂（git.woa.com）
- SSH 地址：`git@git.woa.com:shiqiqiwang/Demo.git`
- 仓库主页：https://git.woa.com/shiqiqiwang/Demo
- 主分支：main
- 详细操作规范：`DOC/AI_WORK_GUIDELINES/GIT_INTERNAL.md`

## 引擎与模块
- Unreal Engine 5.8（源码构建，由 5.7.4 升级；引擎目录 `d:/UE5_8`）
- 项目根：`D:/UE5projects/Demo`
- Runtime 模块：`Source/Demo`
- UE 资产：`Content`（`/Game`）

## 开发基线
- C++ 优先，DataAsset/DataTable 驱动配置。
- Lumen、硬件光追、Virtual Shadow Maps、Mesh Distance Fields、Substrate、Sorted Pixels OIT 默认关闭。
- DX12 + SM6；Scalable 桌面目标。

## AI 工具
- UEEditorMCP：`Plugins/UEEditorMCP`
- Memory MCP：`MCP/memory`
- AI 沙盒：`claude/`

## 构建
`D:/UE5/Engine/Build/BatchFiles/Build.bat DemoEditor Win64 Development D:/UE5projects/Demo/Demo.uproject -WaitMutex`
