# Daily Log — Demo

> 按日期倒序；只保留完成、验证、决定与遗留，过程细节见任务卡、日报和审计归档。

## 2026-08-06

- 内部工蜂主快照 `0ce116aaf649ad4a4e0e549a2c9988beb5ad9303` 已普通推送，推送后本地与 `origin/main` 分叉 0/0。
- 夜间只读审计复核上一快照后的 5 个白天提交：Level0 常驻摆锤已完成原型与用户初步手感验收；共享重冲击 C++ 已落盘，但只有 UHT/无链接编译证据。
- 双 MCP 在线；Level0 打开且 PIE 停止，Level0、L_Game、摆锤/角色相关资产均非脏，三份蓝图 UpToDate。
- 当前 Editor 仍加载 20:25 的旧 Demo DLL；重冲击源码晚于 21:51。两份 PCA 与 HeavyImpact DA 不存在，玩家/追猎者 CDO 没有新 HeavyImpact 字段，追猎者仍显示旧局部受击组件。
- 明日唯一 P0：用户安全关闭 Editor后完成完整链接，再重启创建/Compile PCA、装配 DA/CDO/Blueprint，最后跑 HeavyImpact 自动化、回归与玩家/追猎者 PIE。
- 可玩方向：重冲击验收后讨论“一次性追逐断点房”，首次摆锤命中只延迟追猎者一次，随后机关进入 AI 必通状态。

## 2026-08-05

- 用户确认 Level0 常驻物理摆锤基础效果“算还行”，物理原型初步验收通过；同时指出当前空旷高厅横摆会退化为单纯等时机，沿通路纵摆又会出现绕行或阻塞矛盾，后续先解决机关房型与追猎者必定通过规则。
- 完成 Level0 常驻物理摆锤：C++ Actor、调参 DataAsset、装配蓝图，以及在空白区域按既有 1×2 高厅配方独立搭建的 36 件高层测试房；原高厅保持不动。
- DemoEditor Win64 Development 完整构建成功；摆锤蓝图编译、资产保存与 Level0 保存通过。
- 关闭补能时测得过最低点半周期损耗约 13–18.5 cm/s，据此将每次补能速度增量上限定为 20 cm/s；运行中只补自然损耗，不主动刹车。
- 20kg 磁性物自然碰撞后位移约 76cm、旋转约 8°，同时摆锤最低点速度发生可见改变，证明碰撞冲量未被补能逻辑覆盖。
- 测试入口保留 20kg 磁性箱；待用户手感验收、玩家 Pawn 受击联调与后续 PCG 接入。

- 夜间只读审计复核白天提交 `fe4ca2e62f553095094f1a4e6f5343395d067b57`、3 份未跟踪策划/任务记录、85 个 Source/Demo C++/Build 文件清单与当前验证日志；未修改项目代码、资产、配置或规范。
- 当前日志证明完整构建后 `Demo.PCG 22/22 + Demo.GameFlow 2/2`；Seed 30794 正式主菜单生成成功，用户此前已实测追猎者可上原问题楼梯。
- 官方与本地 UE MCP 在线；Level0 打开、PIE Stopped；L_Game 和正式 Presentation DataAsset 非脏，关键蓝图 UpToDate。
- 下一步顺序：全部楼梯/旋转玩家实走 → 正常玩法真实追猎者完整跨层追逐 → 暂停/失败/Exit/同 Seed 重试/退出联合回归 → 三档各 300 Seed 校准。
- 物理玩法文档仍是待确认讨论稿；优先考虑用磁力搬运高压气罐触发重型环境冲击，不在夜间实施。

## 2026-08-04

- 正式多层 PCG 路线完成初步验收：完整楼梯/高厅预放置、三维占用与净空、逐层二维 WFC、整栋逻辑连通、Start/Pursuer/Exit、结构表现和动态导航门已落盘。
- 楼梯平台与顶层固定灯位为 2/3/0；运行时灯设为 Movable，第三方 LampA 未修改。追猎者占一楼主路线起点，玩家选择满足安全距离后的最近普通格。
- Seed 30794 原问题楼梯经用户实测确认追猎者可上楼。正式 Presentation DataAsset 的 6 段隐形坡道统一上移 3.5cm；磁性资源物保持导航障碍，地刺不参与导航并允许追猎者通过。
- 证伪试验已清理；未遗留全局导航参数、临时 Seed、调试 Actor 或导航轮询。
- UE 5.8 完整构建成功；`Demo.PCG 22/22 + Demo.GameFlow 2/2` 通过。全部组合实走、完整跨层追逐、整局回归与 900 Seed 校准仍待完成。
- 夜间内部快照 `1fad71accf3a48df97bb732b07209346215ea1b1` 与 `9b95f6004729dfc5ababf3eaa6d05bdd772637a9` 已普通推送。

## 2026-08-03

- 正式一局三阶段闭环完成并由用户验收：主菜单进游戏、胜负结算、下一把/重开/选择关卡/回主菜单、ESC 暂停。
- 多层 PCG 方案完成讨论、五片代码预览、独立审查取舍与正式落盘；正式路线为跨层宏结构先冻结，再逐层二维 WFC，最后合并整栋通行图。
- 完整构建、`Demo.PCG 21/21 + Demo.GameFlow 2/2` 与代表性三层 PIE 通过；状态仍为“已实现、待可玩验收”。
- Level0 V2 的 12 段楼梯隐藏坡面完成职责分离：可见楼梯只表现，隐藏坡面负责 Pawn 与导航；真实 BP_Pursuer 已完成多座楼梯双向移动，玩家连续实走仍需用户验收。
- UI 设计记录已落入 `DOC/Design/UI/GAME_FLOW_UI.md`；多层任务保持 Active。

## 2026-08-02

- Level0 V2 静态三层样例完成灯具与结构核对；官方 MCP 发现当时 NavMeshBoundsVolume 未覆盖 V2，随后由 8 月 3 日隐藏坡面与追猎者移动验证推进。
- 当时正式 GameMode 尚缺 Exit/死亡/重开闭环、运行时生成仍是二维合同；这些缺口已由 8 月 3–4 日正式实现取代。
- 夜间快照 `6dea11f2889cb6582631bb46fe97f82802216969` 与 `758b319` 已普通推送。

## 2026-08-01

- Level0 冻结 V1 并精修 V2 三层场景；修正外墙、栏杆、灯与高厅表现，保存重载通过。
- 主菜单 Seed 12345 → L_Game → PCG → Population → 玩家/追猎者开局链路已有日志证据；当时 1138cm 实际出生距离风险已由后续出生语义重构取代。
- WFC 决定：跨层楼梯/平台/净空是不可拆多格宏结构；先固定垂直结构，再逐层求解平面路线，最后派生墙、门框和栏杆。
- 夜间快照 `14ab54f54adcc0eccdf81b7f791932752dd12dbe` 与 `8dbd06c9e9db20cd9c402b98c5d87c35dcfa88e3` 已普通推送。

## 2026-07 月度摘要

- 建立 UE 5.8 C++ 优先 Demo、内部工蜂 Git/LFS、夜间只读维护与双 MCP 协同规范。
- PCG 从二维构造性布局推进到 HydroLab Runtime HISM、Population、最小 RoundFlow，并形成跨层宏结构 + 逐层二维 WFC 的正式方向。
- 追猎者 C++ Timer 状态机、最小 locomotion、Physics Control 局部受击、地刺、磁性抓取与玩家 Health 基础链路形成；当前受击/连续命中与多层完整追逐仍需当前版本验收。
- Level0 完成 HydroLab V3/V4/V5 与三层 V1/V2 静态样例，验证了结构接口、隐藏导航坡面方向和真实追猎者楼梯移动基础。
- 主菜单、GameInstance、正式 GameMode/UI 于月底进入实现；8 月初已完成整局闭环。
- 关键历史验证包括 DemoEditor 构建、PCG 19/19、288/288 Seed Sweep 与多次 PIE；均不能替代当前版本玩家实走和真实追猎者验收。
- 第三方 SFCorridors 只做只读筛选；任何退场/删除仍需依赖闭包、精确清单与用户授权。

<!-- written by shiqiqiwang at 2026-08-05 14:57 UTC -->

## 2026-08-05 — 重冲击物理受击代码实现

- 基线：实施前完整工作区提交 `1c50616c8f19cfa5daa62d39fd626c1c11ff7310` 已推送内部工蜂并核验。
- 完成：共享重冲击状态组件、玩家/AI 接入、磁力中断、摆锤预测合同与测试源码；旧追猎者局部受击保留但暂停三处装配。
- 验证：UHT 与 Demo 模块多轮 no-link 编译通过，最终 4 个动作成功；三路静态复核无 P0/P1；`git diff --check` 通过。
- 遗留：运行中 Editor/Live Coding 锁住 DLL，完整链接未完成；PCA/DA/Blueprint、自动化、PIE 和画面验收均待后续，Content/Config 未改。

<!-- written by shiqiqiwang at 2026-08-06 10:06 UTC -->


## 2026-08-06 — Level0 自动周期冲锤

- 完成四个冲锤 C++ 文件、默认 DataAsset、装配 Blueprint 与摆锤房旁三段低天花测试走廊；DemoEditor 正式链接及 Blueprint 警告即错误编译通过。
- PIE 确认 0→450→0cm 周期；普通物理箱发生真实位移，Prepared 玩家/追猎者均出现非零 Chaos solver impulse；HeavyImpact 自动化 4/4 通过。
- 审计要求的 RamBody CCD、严重掉帧帧感知准备窗口、装配方向与冲程净空约定均已落实，审计报告已归档。
- 用户指出房外大 Cube 穿帮后已移除该占位；保留杆件并改成细圆柱推杆 + 小型墙面导向座，允许穿墙后空间，不增加大机箱遮挡。
- 当前待用户在 Level0 验收预警/周期、追猎者实际通行与压力手感；不把参数写成最终策划结论。

<!-- written by shiqiqiwang at 2026-08-06 13:55 UTC -->


## 2026-08-06 — 物理机关相机更新顺序与位置平滑
- 完成强制基线 `1592d13dd71913646aad64612c922bedebd7d5a7` 的内部工蜂推送和远端核验。
- 落盘 1 行更新依赖与 4 项玩家 SpringArm 配置；无链接编译、正式 DLL 链接、Blueprint 保存和重启回读均通过。
- 审计已处理并归档；用户明确自行完成 PIE 体验验证。已知边界仍是原生 SpringArm 墙体命中位置不会被 Camera Lag 平滑。

<!-- written by shiqiqiwang at 2026-08-06 14:39 UTC -->


## 2026-08-06 — 壁挂式物理制导一次性机关

- 完成独立一次性机关 C++、两个装配 Blueprint、默认 DataAsset、物理材质/机身材质与 Level0 正面墙/拐角墙两个测试实例。
- 运动合同为真实 Thruster 短时推进和小角度制导，首个有效碰撞永久失导但推进计时继续，随后纯 Chaos；角色侧不补冲量。
- Review 采纳结构校验和轴约定；按用户决定不先抽 HeavyImpact 公共层，待三个机关稳定后再评估。
- Blueprint 静态编译/回读、Muzzle 净空/发射走廊检查和 Demo 模块构建通过；Editor 已正常关闭，只使用 `-Module=Demo -NoEngineChanges`，没有编译引擎。
- 按用户要求未运行 PIE，次日由用户验收玩家/追猎者触发、命中与连续反弹手感。

<!-- written by shiqiqiwang at 2026-08-06 15:04 UTC -->

## 2026-08-06 — 壁挂式物理制导机关现场布局修正

- 完成：否决远端 Room900 摆位，在摆锤房画面右侧搭建 300 cm 低顶 L 形走廊，并迁入正面/转角两个既有 Launcher。
- 验证：新文件夹 47 个 Actor；旧文件夹不存在；中心线与 Muzzle 前 200 cm 静态净空；Level0 保存成功。
- 构建：正常关闭 UE 后仅构建 Demo 模块，结果 `Succeeded`、目标最新、0 个动作，没有编译引擎。
- 遗留：未运行 PIE；运行时触发、命中、首碰失导与 Chaos 多次反弹仍待用户验收。

<!-- written by shiqiqiwang at 2026-08-06 17:06 UTC -->


## 2026-08-07 夜间只读审计

- 复核上一夜间快照后的 9 个白天提交、107 个 Demo C++/Build 文件、活动任务、既有构建/测试日志与 Git/LFS 状态；未修改项目代码、资产、配置或规范。
- 白天完成 HeavyImpact 正式集成、自动冲锤、起身动画导入/重定向、相机更新顺序修复，以及壁挂式一次性制导机关和低顶 L 形测试走廊；各项仍按构建/静态验证/运行验收边界分别记录。
- 既有证据包括 Demo 模块正式链接、HeavyImpact 4 项自动化通过、冲锤完整循环与真实 Chaos 接触；制导机关按用户安排未运行 PIE，起身 `Recovering` 尚未实现。
- 官方 UE5.8 MCP 未暴露，本地 UE Editor MCP `pong=false`，因此蓝图审计未执行，没有猜测资产、关卡、DataAsset 或引用状态。
- 明日 P0：统一完成制导机关/冲锤玩家与追猎者运行验收、HeavyImpact A/B、相机对照和墙边/连续碰撞；随后验证动画兼容并实现真实倒地点起身。
- 可玩方向：把相邻三间机关房收束为一段物理压力走廊，先串联现有状态，不继续增加孤立机关原型。

<!-- written by shiqiqiwang at 2026-08-07 04:45 UTC -->


## 2026-08-07 — HeavyImpact 起身恢复桥实现与最终非破坏验证

- 完成 HeavyImpact 起身恢复桥 C++ 与恢复调参，并以官方 UE5.8 MCP 装配玩家项目 AnimBP、玩家 AnimClass、追猎者 AnimBP 和两份 DataAsset；没有修改 Level0 或机关资产。
- Demo 模块完整链接构建成功；`Demo.Physics.HeavyImpact.*` 5/5 全部成功，无 warning/error。
- PIE 实际对象回读确认玩家/追猎者 AnimInstance 为各自项目 AnimBP 类，父类均为 `HeavyImpactAnimInstance`；本轮未出现指定 validation、PCA、AnimInstance、Skeleton、Slot 或 Montage 错误，结束后 PIE=false、五项资产均非脏。
- UE5.8 源码结论纠正了早先的骨架阻塞判断：无需重定向玩家 AnimBP；运行时 Skeleton 来自玩家 Mesh，动态 Montage 来自序列，两者同为 CH_SciFiTrooper 骨架。
- 覆盖边界：自动场景与两次临时定点未形成摆锤 Chaos 接触，出现过 `Expected source did not make contact` / `Prediction arrived too late`，因此未获得真实 `committed → Downed → recovery completed` 或起身画面证据，待用户验收。
- 首次 EditorExit 曾在 UnrealEd/Slate 发生一次 AV，第二次正常退出；当前不把该单次退出问题归因于起身代码。
