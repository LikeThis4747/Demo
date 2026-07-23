# Daily Log — Demo

> 按日期倒序；不复制聊天流水。

## 2026-07-18

### 完成
- 创建并编译 UE 5.7.4 C++ Demo。
- 注入并验证三个 MCP 服务。
- 建立轻量渲染基线、C++ 优先架构和多 AI 工作流。
- 初始化 Git/Git LFS，创建 GitHub 私有仓库并推送 `main`。
- 建立夜间只读安全规则、01:00 夜间任务和 08:00 Git 失败提醒。
- 明确项目预计工期三周。

### 遗留
- 等待题目公布后规划三周里程碑与首个玩法闭环。

<!-- written by shiqiqiwang at 2026-07-23 03:38 UTC -->

# 2026-07-23

- 为运行时 WFC 素材筛选建立 TASK-20260723-004；仅在独立 DemoAssetsPreview 工程同步 Demo 的 Scalable 渲染基线，关闭 Lumen、硬件光追、VSM、Mesh Distance Fields、Substrate 与 Sorted Pixels OIT。
- 验证：RendererSettings 与 HardwareTargetingSettings 逐项匹配；DemoAssetsPreviewEditor Win64 Development 构建成功。
- 边界与遗留：未修改 Demo 代码/Config/Content，未导入素材；下一步启动预览工程后逐包检查模块、碰撞、通行、依赖、表现与许可。

<!-- written by shiqiqiwang at 2026-07-23 05:00 UTC -->
- 素材初筛：CorridorEnvironment 只有 18 个网格且结构覆盖不足，旧 Matinee 与缺失战舰依赖污染示例图；Sicka 有 66 个网格但以道具/装饰为主。两者均不建议作为 WFC 主结构包，Sicka 可保留为装饰候选；精确尺寸/Pivot/持板通行待编辑器复核。

<!-- written by shiqiqiwang at 2026-07-23 08:25 UTC -->

<!-- written by Codex at 2026-07-23 16:25 UTC+8 -->
- PCG 方案：在正式策划补入题目原文“实时、非工具”，并将既有 PCG DailyPlan 重写为 V1：Progression/Spatial Graph + 局部有限 WFC + 特殊 Socket + 内容规划/验证 + Runtime 实例化；难度开局锁定、困难保持近似时长，所有难度限制长距离折返。
- SFCorridors 只读核验：Demo 内 `/Game/Assets/SFCorridors` 共 295 项资产、70 个 Static Mesh、2 个示例 World；结构件尺寸不统一，第三方目录保持只读，由项目 Module Catalog/Presentation Set 隔离，精确 Bounds/Pivot/Portal/碰撞/净空/导航仍待测量。
- 验证与遗留：`git diff --check` 无格式错误；本轮未修改 Source、Content、Config 或保存 UE 资产。V1 待用户确认，确认后先做素材契约测量，再展示首个纯数据代码检查点并等待实现授权。

<!-- written by shiqiqiwang at 2026-07-23 09:00 UTC -->

<!-- written by Codex at 2026-07-23 17:00 UTC+8 -->
- PCG Checkpoint 1 评审拟稿：在 DailyPlan 第十二节附入未来位于 `Source/Demo/Public/PCG`、`Private/PCG` 的 9 文件 C++ 方案，覆盖 Profile Snapshot、确定性随机域、抽象拓扑、目标绑定、K-of-N 精确搜索、共享折返硬上限和 Automation Test；没有创建 Source 文件。
- 评审收敛：公共反射数据与私有求解器分离；默认 Profile 只保证 Any 目标策略，限制型策略需匹配 Flow/Profile；DataAsset 使用 EditAnywhere；验证器核对 Signature、规范主干、分支邻接与目标来源元数据。
- 验证与遗留：文档 9 个 C++ 代码块围栏、括号/花括号和 `git diff --check` 静态检查通过；因未落盘，尚无 UHT/Build/Test 结果。下一步先汇总独立 AI 评审，用户授权后才实施；WFC 前仍需素材契约测量。

<!-- written by shiqiqiwang at 2026-07-23 09:32 UTC -->

<!-- written by Codex at 2026-07-23 18:10 UTC+8 -->
- PCG V2 送审版：纠正“完整素材契约后才能继续”的过度门禁，明确 SFCorridors 现在就是首个正式素材输入；只在模块进入 PIE 前补该模块的 Footprint/Pivot/Portal/碰撞/净空与表现数据，未来通过 Module Catalog + Presentation Profile 替换素材。
- 算法与代码拟稿：收敛为 Progression/Spatial Graph → Special Socket → 确定性 A* → 有限 Simple-Tiled WFC → Closure → 全局验证 → Runtime 实例化；K-of-N 与布局解耦。源码拟规划为 Public/PCG、Private/PCG 下 10 个同层文件，不再一文件一目录。
- 送审加固：Socket 使用整数 CellOffset/ConnectorTypeId，增加全局确定性工作预算，区分 WFC 无解和预算耗尽，碰撞/导航进入可替换 Presentation Binding；规划 PIE-A EscapeOnly 结构闭环与 PIE-B Collect 2 of 3 扩展闭环。
- 验证与遗留：只更新 DailyPlan 和任务卡，没有创建或修改 PCG Source、DataAsset、Blueprint、关卡、SFCorridors、Config、插件或 Build.cs；尚无 UHT/Build/Test/PIE 结论。下一步先交独立 AI 评审，用户明确允许后才实施。

<!-- written by shiqiqiwang at 2026-07-23 09:46 UTC -->

<!-- written by Codex at 2026-07-23 18:45 UTC+8 -->
- PCG V2 内部只读审查收敛：补齐 Abstract Node/Edge→实际 Placement/Path、Objective/Gameplay Anchor、Portal Connection、StableId→DenseIndex、完整 Connector Signature、ActiveWfc/Reserved/Outside、传播结果分类、Portal Frame 反向约定和 PIE Staging/TestHarness 契约；同时加入 A*/Socket/全局工作预算与完整生成签名。仍只修改方案/任务上下文，未实施源码或 UE 资产。

<!-- written by shiqiqiwang at 2026-07-23 11:48 UTC -->

<!-- written by Codex at 2026-07-23 19:50 UTC+8 -->
- PCG V2.1 评审收敛：读取最新未 Done 的 PCG 评审并逐项吸收。WFC 改为删除事件 + 四向 Support Count，冻结 256 Active Cell、64 Variant、16 MiB 实时快照和 64 MiB 单 Attempt 累计复制；首版移除 OrderedObjectives 暴露，并补真实选择诊断、多层回溯与失败分类。
- 拟代码加固：Transform 三个 helper + Test 0、A* int64 G/F 与转向成本、完整 Snapshot 跨文件定义、Public/Internal Clear、Guard 重入规则、对象创建即登记 rollback、实例化结构化失败，以及同步/线程/分批实例化三类性能门槛。
- 验证与边界：WFC 与 Runtime 各完成两轮只读静态复核，未发现剩余阻塞；评审已重命名为 Done。仍未创建或修改 PCG Source、DataAsset、Blueprint、关卡、Config、插件或第三方资产，无 UHT/Build/Automation/PIE 结论；下一步等待用户确认 V2.1 和明确实施授权。

<!-- written by shiqiqiwang at 2026-07-23 13:33 UTC -->

<!-- written by Codex at 2026-07-23 21:35 UTC+8 -->
- PCG C++ 实施：落盘 Public/PCG 与 Private/PCG 的 10 个首版文件，覆盖 DataAsset 契约、抽象 Progression、CollectAll/K-of-N、Special Socket、整数 A*、Support-count WFC、Closure/Finalize、实际 Placement 图验证和 Runtime HISM 事务式实例化。
- 评审修复：补目标叶房随机失败的有界最小折返回退、Strong↔Strong 直连、Anchor Footprint 净空、foreign weak anchor 防误共享、Edge→Socket/A* 硬预算 DFS/完整回滚，以及游戏线程/游戏世界/Construction Script 防护；首版 Actor Policy fail-closed。移除未被算法消费的 WfcBandRadiusCells，避免假配置。
- 验证：最终 DemoEditor Win64 Development 通过 UHT、编译和链接；Demo.PCG 实际发现 12 项并 12/12 成功，含 64 Seed Objective 回退、T 形 Strong Socket 分支、预算失败原子性与重复 Hash。git diff --check 无错误，仅有既有 LF/CRLF 提示。
- 遗留：未创建或修改 SFCorridors 第三方资产、项目 DataAsset、Blueprint 或测试关卡，尚未执行 PIE。下一步必须由用户重新打开 UE，再完成最小 Portal/接缝/净空测量、/Game/ZeroEscape/Generation 资产装配与 PIE-A。

<!-- written by shiqiqiwang at 2026-07-23 14:13 UTC -->

<!-- written by Codex at 2026-07-23 22:15 UTC+8 -->
- PCG 素材适配：只读测量 SFC 两张示例地图，Room_Pass 间距约 658～662 cm，冻结 660 cm 逻辑 Cell/Portal 步长；24～66 cm 外壳外探进入有 100 cm 硬上限的 Presentation Overhang，不改变逻辑占格，第三方 SFCorridors 未修改。
- 项目资产：创建并保存 Generation Profile、Module Catalog、SFC Presentation、Generator Blueprint 与独立 L_PCG_RuntimeTest Map 包；BP CDO 装配 Seed 12345 / Normal / EscapeOnly / BeginPlay。测试 Map 当前为空。
- 代码质量：PCG 10 个 C++ 文件补齐详细中文设计注释，覆盖确定性、不变量、Stable Id/Dense Index、K-of-N、Socket/A*/WFC、预算、回滚、HISM 生命周期和素材替换边界。
- 验证：DemoEditor Win64 Development 成功；Demo.PCG 13/13。新增集成烟测从磁盘读回三份 DataAsset 与 Generator BP GeneratedClass/CDO，核对六个模块、六个 SFC Mesh、Pivot/Overhang 和默认请求，并以全新快照执行两次完整生成，Abstract/Layout Hash 一致。
- 遗留与边界：-NullRHI 编辑器进程在首次 Spawn 关卡 Actor 时于 FSceneViewport::EnqueueBeginRenderFrame 命中整数除零；已停止重试且未删除资产。下一步用户正常打开 UE 后，在 L_PCG_RuntimeTest 装配 Generator、Staging、PlayerStart 和灯光并执行 PIE-A。当前不能声称接缝、碰撞、净空、导航、性能或实际走通已验收。

<!-- written by shiqiqiwang at 2026-07-23 17:03 UTC -->


<!-- written by Codex nightly automation at 2026-07-24 UTC+8 -->

## 2026-07-24 夜间审计

- 今日完成：实施前快照 `4a38b9f9d8507d96603e8dab08f3b200c0daed68` 已推送；PCG 10 个 C++ 文件、三份项目 DataAsset、Generator Blueprint 与空测试 Map 已落盘。既有记录显示 DemoEditor Win64 Development 构建成功、Demo.PCG 13/13 成功，含序列化资产全链烟测；夜间未重复构建或运行会写入工程的测试。
- 审计：Git 起始与 origin/main 0/0；origin 精确为内部工蜂 `git@git.woa.com:shiqiqiwang/Demo.git`。UE MCP `ping=false`，因此蓝图审计未执行；未猜测 Blueprint、关卡、资产引用或配置状态。
- 遗留：`L_PCG_RuntimeTest` 仍为空，尚未完成 HISM 世界实例化、视觉接缝、碰撞、净空、导航、性能、固定 Seed 批量或用户实际走通；同步主线程性能需在 packaged Development 采样后判断。
- 明日建议：用户正常打开 UE 与 `L_PCG_RuntimeTest`，只装配 Generator、Staging、PlayerStart 和基础灯光并执行 PIE-A；通过后再做固定 Seed/碰撞/净空检查，之后才接追猎者与地图内玩法闭环。
