# Progress — Demo

## M0 项目基础设施

- [x] UE 5.7.4 C++ 项目、项目 MCP、轻量渲染基线与 C++ 优先工作流
- [x] 本地 Git/Git LFS、内部工蜂备份与夜间只读维护流程

## M0.5 引擎升级与双 MCP 平台（2026-07-24）

- [x] Demo 由 UE 5.7.4 升级到 5.8（源码引擎 `d:/UE5_8`）；升级前已冻结 Git tag `v5.7.4-final` 与物理副本 `Demo_UE5.7.4_backup`
- [x] `DemoEditor Win64 Development` 在 5.8 下构建成功；实际仅两类改动：两个 `Target.cs` `DefaultBuildSettings` V6→V7；5 处 `FJsonObject` 键类型 `Pair.Key`→`*Pair.Key`（5.8 键改为 `UE::FSharedString`）；并清除 `UMGActions.cpp` 的 `REN_ForceNoResetLoaders` 弃用警告
- [x] 启用官方 `ModelContextProtocol` + 5 个 toolset（Editor/Physics/UMG/AutomationTest/SemanticSearch），Experimental 插件 dll 引擎已编好，零编译成本
- [x] 官方 MCP 连通并实测：走 tool-search 模式（`list_toolsets`/`describe_toolset`/`call_tool`），共 23 toolset；确认官方 `BlueprintTools` 也有逐节点建图+连线
- [x] 全局 `mcp.json` 加 `ue58-official-mcp`→`http://localhost:8000/mcp`，项目设 `bAutoStartServer=True`（开编辑器自动起 server）；重启 CodeBuddy 后原生工具通路已生效
- [x] 产出并归档双 MCP 协同规范 `MCP/capability-notes/dual-mcp-usage-guide.md` + 两份实测数据附录；`AGENTS.md`、`memory-bank`、`DOC` 等文档统一改为 5.8
- [ ] 磁力 PIE 手感 5.8 实测对比、`Demo.PCG` 自动化在 5.8 重跑（当前 5.7.4 下 13/13）

## M1 实时 PCG 场景

- [x] 冻结 Progression/Spatial Graph → Special Socket → 确定性 A* → 有限 WFC → Closure → 实际图验证 → Runtime HISM 的 V2.1 方案
- [x] 实现 PCG 纯 C++、项目 DataAsset 契约、Generator Blueprint 装配与详细设计注释
- [x] 完成 SFC 示例间距测量；逻辑 Cell/Portal 步长为 660 cm，第三方素材通过 Catalog/Presentation 隔离并保持只读
- [x] DemoEditor Win64 Development 构建成功；Demo.PCG 13/13 成功，含序列化 DataAsset/Generator BP 全链烟测
- [ ] 在独立 `L_PCG_RuntimeTest` 装配 Generator、Staging、PlayerStart 与灯光并完成 PIE-A
- [ ] 完成固定 Seed 批量、碰撞/净空/接缝/性能与用户实际走通验收
- [ ] PIE-A 通过后接入追猎者，再实现生成地图内单局玩法闭环

## 当前边界

`L_PCG_RuntimeTest` 包已存在但仍为空。NullRHI 编辑器 Actor Spawn 会触发 UE 视口整数除零，下一步必须在用户正常打开 UE 后装配关卡并执行 PIE。自动化与资产烟测不能替代 HISM 世界实例化、碰撞、导航、性能和用户走通验收；2026-07-24 夜间 UE MCP `ping=false`，蓝图审计未执行。
