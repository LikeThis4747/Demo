# Daily Log — Demo

> 按日期倒序；不复制聊天流水。较早或过程性内容按需压缩为结论摘要，迭代细节见任务卡与 `claude/reviews/` 归档。

## 2026-07-24

- PCG 实施后审计：实施前快照 `4a38b9f` 已推送；PCG 10 个 C++ 文件、三份项目 DataAsset、Generator Blueprint 与测试 Map 已落盘。DemoEditor Win64 Development 构建成功、Demo.PCG 13/13（含序列化资产全链烟测）。
- 目录纠正：PCG 测试 Map 迁移到 `/Game/Levels/L_PCG_RuntimeTest`；生成器/Profile/Catalog/Presentation 留在 `/Game/ZeroEscape/Generation`；命令行加载 Map Check 0/0。
- PIE-A0：在测试 Map 保存 Generator 与两盏测试灯，实例回读正确，Simulate 创建正确 PIE World、停止后无脏包。发现三项 SFC 材质缺 Instanced Static Mesh Usage 会回退 Default Material；Staging/PlayerStart、Ready 状态探针与玩家走通仍待完成。
- 文档精简：DailyPlan 巨型方案文件删除 V1 废稿与已实现内嵌代码（226KB→约 55KB）；记忆层收敛——activeContext 改为索引+指针，current-task 为唯一当前详情源，daily 去除方案迭代流水。

## 2026-07-23

- PCG 方案冻结：题目“实时、非工具”要求补入正式策划；PCG DailyPlan 经 V1→V2→V2.1 多轮内部与独立评审收敛为 Progression/Spatial Graph → Special Socket → 确定性 A* → 有限 WFC → Closure → 实际图验证 → Runtime HISM，并获授权实施（迭代细节见任务卡与 reviews 归档，不在此展开）。
- PCG C++ 实施：落盘 Public/PCG 与 Private/PCG 的 10 个首版文件，覆盖 DataAsset 契约、抽象 Progression、CollectAll/K-of-N、Special Socket、整数 A*、Support-count WFC、Closure/Finalize、Placement 图验证与 Runtime HISM 事务式实例化；Actor Policy fail-closed。DemoEditor 构建通过，Demo.PCG 首轮 12/12 后补至 13/13。
- SFC 素材适配：只读测量两张示例地图，Room_Pass 间距约 658～662 cm，冻结 660 cm 逻辑 Cell/Portal 步长；外壳外探进入 100 cm 硬上限 Presentation Overhang，不改变逻辑占格，第三方 SFCorridors 未修改。创建并保存 Generation Profile/Module Catalog/SFC Presentation/Generator Blueprint 与独立测试 Map。
- 素材筛选（TASK-004）：搭建 DemoAssetsPreview 独立工程同步 Scalable 渲染基线；CorridorEnvironment 与 Sicka 两包结构覆盖不足，不宜作 WFC 主结构包，Sicka 可留装饰候选；精确尺寸/Pivot/通行待编辑器复核。
- 遗留：尚未 PIE 装配与走通；-NullRHI 进程在 Actor Spawn 时命中 UE 视口整数除零，属编辑器/MCP 启动模式问题，非 PCG 崩溃。

## 2026-07-18

- 创建并编译 UE 5.7.4 C++ Demo；注入并验证三个 MCP 服务。
- 建立轻量渲染基线、C++ 优先架构与多 AI 工作流；初始化 Git/Git LFS 与内部工蜂备份。
- 建立夜间只读安全规则、01:00 夜间任务与 08:00 Git 失败提醒；明确工期三周。
