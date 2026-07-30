# Active Context — Demo

> 当前任务详情以 .ai-context/current-task.md 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

最短可玩闭环当前先收敛阶段一：主菜单选择 Seed/难度 → GameInstance 跨关卡传参 → 正式关卡 PCG 生成 → 玩家与追猎者就位。只有当前构建、UE 资产装配与 PIE 同时通过后，才继续失败/成功结算与重开。

## 当前索引

- 阶段一新增 8 个 GameFlow/UI C++ 文件并增加 UMG 模块依赖；工作区另有粗粒度 GAME_LOOP_PLAN 讨论稿。
- Active 卡 2 张：HydroLab 装配验证与 SFCorridors 只读筛选；SFCorridors 不删除，任何退场仍需依赖闭包、精确清单和用户授权。
- HydroLab V5 静态原型为 259 Actor / 15 叶文件夹；三层楼梯塔任务卡记录最终 211 Actor / 13 文件夹。二者都仍需玩家、Recast 与真实追猎者运行验收。
- 历史同一 Seed 15339 的 6 次生成/4 次 Exit 只算重复运行证据，不替代当前阶段一、至少 10 Seed 或真实 AI 验收。

## 当前边界与风险

- 新 C++ 尚无当前构建/UHT/PIE 证据；现有 Demo 模块二进制早于源码。
- 蓝图审计未执行：本地 UE Editor MCP pong=false，保存日志显示连接数达到上限；官方 UE5.8 MCP 未暴露。
- 正式 GameMode 必须在 UE 中确认使用已装配 InputConfig 与磁力资源的玩家蓝图；不能把原生 AZeroEscapeCharacter 当作可玩装配。
- 开局流程当前先移动玩家、后校验/生成追猎者，需在白天复现失败路径，避免半初始化局面。
- 当前渲染配置为软件 Lumen GI + SSR（r.DynamicGlobalIlluminationMethod=1、r.ReflectionMethod=2），r.VirtualTextures=False 为未提交白天改动；低配无 Lumen 室内补光仍待同机验证。
- .gitignore 新增 DOC/PPT/，需白天确认是否有意停止新 PPT 文件进入后续快照。
