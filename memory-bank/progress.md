# Progress — Demo

## M0 项目基础设施

- [x] UE 5.7.4 C++ 项目、项目 MCP、轻量渲染基线与 C++ 优先工作流
- [x] 本地 Git/Git LFS、内部工蜂备份与夜间只读维护流程

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
