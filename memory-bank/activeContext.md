# Active Context — Demo

## 当前阶段

实时、非工具性 PCG 整关生成是第一优先。纯 C++、首套 SFC 项目 DataAsset 与 Generator Blueprint 已有构建和序列化资产烟测证据；当前门禁是正常 UE 视口下的测试关卡装配与 PIE-A。

## 活跃任务

- `TASK-20260723-002`：当前关键路径；测试 Map 为空，等待装配与 PIE。
- `TASK-20260723-003`：Demo MCP 诊断；Memory MCP 当前已注册，2026-07-24 夜间 UE MCP `ping=false`。
- `TASK-20260723-004`：素材预览/性能验收；不抢占 PCG PIE-A。

## 稳定决定

- 宏观 Flow 不交给 WFC；Progression/Spatial Graph 先定流程，Special Socket 与确定性 A* 保证连接，有限 WFC 只选局部模块。
- Runtime 每局现场生成且局内 Seed/难度/布局固定；确定性分支只使用有界工作预算。
- SFCorridors 通过 Catalog + Presentation 隔离，第三方资产只读；首版只允许 HISM，Actor Policy fail-closed。
- 逻辑 Portal/Grid 步长为 660 cm；外壳外探只属于 Presentation，不扩大逻辑占格或替代碰撞验收。

## 已验证与未验证

- 已验证：DemoEditor Win64 Development 构建；Demo.PCG 13/13；三份 DataAsset 与 Generator BP CDO 的序列化全链烟测。
- 未验证：HISM 世界实例化、视觉接缝、碰撞、可走净空、导航、packaged 性能、固定 Seed 批量和用户实际走通。
- 2026-07-24 夜间 UE MCP 不在线，蓝图、关卡、资产引用与配置审计未执行。

## 下一步

用户正常打开 UE 和 `/Game/ZeroEscape/Generation/Maps/L_PCG_RuntimeTest`。只在该测试关卡装配 Generator、Staging Platform、PlayerStart 与基础灯光，回读并保存后执行 PIE-A；不修改 `Level0`，不提前接入追猎者或陷阱。
