# Active Context — Demo

> 当前任务详情以 .ai-context/current-task.md 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

最短可玩闭环仍是：PCG 生成 → 地刺/磁力投掷 → 追猎者追击/受击 → Exit 成功，并补生命归零后的失败/同 Seed 重开。Level0 的 V5 大小房网络已保存重载并通过静态结构检查，下一步必须先完成玩家、Recast 与真实追猎者验收，再映射到 Runtime WFC。

## 当前索引

- Active 卡 2 张：SFCorridors 只读筛选与 HydroLab 错层房间装配验证；SFCorridors 不删除，任何退场仍需依赖闭包、精确清单和用户授权。
- V5 当前为 259 Actor / 15 个叶文件夹，包含 Low300→Tall750 过渡、两间 2x2 大房、分流汇合侧环路、目标支路和 7 件 Portal450 共享边配方。
- 同一 Seed 15339 的日志有 6 次生成和 4 次 PlayerReachedExit，只是单 Seed 重复运行证据，不替代至少 10 Seed、V5 或真实 AI 验收。
- 追猎者近战/方向受击与磁力 Camera 通道改动已落盘，但当前构建、18 项 Demo.PCG、PIE、连续命中和重复投掷仍未验收。

## 当前边界与风险

- V5 静态 Outliner、射线和地板支撑不能替代玩家胶囊连续行走、Recast 覆盖、真实追猎者移动或 Runtime PCG 动态导航。
- 本地 UE Editor MCP 在线；官方 UE5.8 MCP 未暴露且编辑器源码控制未启用，DataAsset、BlendSpace 与地图二进制的属性级差异/引用审计未执行。
- r.VirtualTextures=True 是新的全局渲染配置，需白天确认用途及纹理、着色器和显存影响。
- DOC/PPT 新增约 85 MB PPTX，当前未由 Git LFS 跟踪；夜间按快照规则备份，不在本轮改变仓库策略。
