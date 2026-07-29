# Daily Log — Demo

> 按日期倒序；只保留完成、验证、决定与遗留，过程细节见任务卡、日报和审计归档。

## 2026-07-30

- 夜间只读复核确认 Level0 的项目内 HydroLab_RoomNetworkV5 仍为 259 Actor / 15 个叶文件夹；NavMeshBoundsVolume_1 与 RecastNavMesh-Default 可见，但本轮未运行 V5 玩家实走、Recast 路径或真实追猎者移动。
- Saved/Logs/Demo.log 记录同一 Seed 15339 的 6 次 PCG 成功生成和 4 次 PlayerReachedExit；这只证明该 Seed 的重复运行，不是多 Seed、V5 或真实 AI 验收。
- 本地 UE Editor MCP 在线，BP_ZeroEscapeCharacter 与 BP_MagneticProp 均为 UpToDate；官方 UE5.8 MCP 未暴露且编辑器源码控制未启用，DataAsset、BlendSpace、地图二进制的属性级差异和引用审计未执行。
- C++ 当前新增磁力物持有期间忽略 Camera 通道、释放时恢复原响应；本轮没有 C++ 构建、18 项 Demo.PCG 自动化或磁力相机手感回归。
- 明日关键路径：先验收 V5 玩家/真实追猎者路线，再建立当前构建、18 项 Demo.PCG 和至少 10 Seed 基线，随后补生命归零失败/同 Seed 重开及追猎者攻击、重复投掷边界。
- 风险：r.VirtualTextures=True 需白天确认用途与性能影响；DOC/PPT 新增约 85 MB 文件且未由 LFS 跟踪，夜间不调整仓库策略。

## 2026-07-29

- 每日自动复核确认 Level0 的 HydroLab WFCBrickStudyV3 保持 7 类独立积木、A+B / A+B+C 与完整组合；V3 为 538 Actor / 22 文件夹，完整组合为 235 Actor / 9 文件夹。
- 未发现可见并发摆放：仅一个 UnrealEditor 进程，active 卡无其他近期摆放写入；Codex 全局任务列表接口连续超时，因此不能绝对排除不可见任务。
- 原 NavMeshBounds 未覆盖完整组合。已将 NavMeshBoundsVolume_1 调为位置 (2400,5540,0)、缩放 (1,2,1)，保持 Recast Static 并重建导航；没有修改 HydroLab 第三方资产、PCG/WFC 代码或项目配置。
- 首次路径复核发现 StairsC 与 Deck135 为两个导航岛；只新增一个专用双向 NavLink 跨越已验证连续的落脚拼缝。最终入口→主空间→T 分支→2x2 房间→StairsC→Deck135 五段均 valid=true、partial=false，Deck 内部路径也完整。
- 编辑器磁盘重载后 V3/完整组合数量、导航连接和 Volume Transform 均保留；短时 PIE 运行阶段 Warning/Error/Fatal 为 0，停止后仅有既有 CrowdManager 析构期 Recast 警告。
- 按用户晚间要求暂停导航与追猎者楼梯验证，封存 V4 楼梯房；在独立区域完成 HydroLab RoomNetworkV5。保存重载后为 259 Actor / 15 叶文件夹，10 个结构模块实例组成 Low300→Tall750 过渡、两间 2x2 大房、分流汇合环路和目标支路。
- 只读回到 Demonstration 验证 DoorFrame 的真实结构用法后，增加单位缩放的 7 件 Portal450 共享边解析器。整网 15 项、小房→大房 9 项、Portal 12 项静态路线/边界/地板支撑检查在保存重载后全部通过；门槛约高 3.75cm。
- 官方 MCP 曾出现创建成功但回报文件夹不存在的部分执行，重试短暂生成双份 Portal；只在 AI 新建 Portal 子组内按标签去重为 7 件。没有叠隐形碰撞、没有修改 HydroLab 第三方资产或 PCG/WFC 代码。
- V5 遗留：用户整体审美近看与玩家 Play From Here 实走；明日再做 Recast、追猎者和楼梯 G0/F2 接口验证。本轮不把静态射线写成 AI 可达，HydroLab 任务保持 Active。
- 追猎者白天改动已加入近战攻击/方向受击蒙太奇、受击停顿、AttackApproachRadius、限时 AttackProjectile Tag 与 Camera 通道忽略；仅有静态源码和资产状态证据，本轮未构建、未跑 PIE、未做玩家验收。
- 遗留优先级：先完成 Level0 玩家/真实 AI 实走，再完成生命归零失败/同 Seed 重开；随后构建并验收追猎者攻击/受击链和投掷物 Tag 重复投掷边界。

## 2026-07-28

- 夜间只读审计确认 Seed 16001 PCG/Population/RoundFlow/Health/Physics Control 基本链路，并完成内部工蜂快照。
- 白天收敛三张过期追猎者 active 卡：Physics Control 改为已完成且用户 PIE 验收通过；追击卡改为“最小时机闭环已运行、正式动画/多 Seed 导航待验收”；最小 ABP 改为“GroundSpeed/EventGraph 已验证、AnimGraph/最终验收待后续”，三卡全部归档。
- claude/tasks/active 保留 SFCorridors 物件筛选卡，并新增 HydroLab 错层房间装配验证卡。
- UE 只读检查 70 个 SFCorridors Static Mesh 的尺寸/Pivot，并查看候选缩略图；首批建议仅保留候选 SM_LampWallL 与 SM_comp，HydroLab 继续作为 PCG 主结构。
- 295 个 SFCorridors LFS 资产本轮未删除；删除前必须完成 Asset Registry 依赖闭包、精确保留/删除清单与用户明确授权。

## 2026-07-27

- 完成 PCG 空间职责精简、运行时顶灯、独立 Population 地刺/磁力物放置和最小 RoundFlow。
- 玩家位于 PCG Start，追猎者位于身后至少 1200 cm，双方初始朝向一致，Exit 只结算一次成功。
- 接入追猎者 Physics Control 局部受击、最小 locomotion AnimBP/BlendSpace、地刺与玩家 HealthComponent；用户完成主视口位置验收、地刺首轮场景验收和 Physics Control PIE 验收。
- UE5.8 DemoEditor 完整构建及 SelectedViewport PIE 曾成功；最新静态代码审阅没有重新运行自动化。
- 遗留多 Seed、导航、追击动画、ABP 最终装配和正式失败闭环。

## 2026-07-26

- 夜间只读审计确认本地 UE Editor MCP 在线；停止状态关卡未见完整动态导航证据。
- 历史技术证据保持 DemoEditor 构建成功、Demo.PCG 19/19 和 288/288 Seed Sweep。

## 2026-07-25

- 用户授权后仅为共同根材质 M_HydroLab 启用 Instanced Static Mesh Usage。
- V4 全图 Grid-WFC、Count/MaxConsecutive/Connected/Tarjan、有界回溯与 Runtime HISM 通过历史构建、19/19 自动化和 288/288 Seed Sweep。

## 2026-07-24

- PCG V3.2 完成构造性 Progression、Grid-WFC、300 cm 分离结构展开、Runtime HISM、13/13 自动化与 288/288 Seed Sweep。
- Demo 升级 UE 5.8；双 MCP 能力矩阵与协同规范归档。

## 2026-07-23

- 开发顺序冻结为“实时整关生成 → 追猎者 → 地图内玩法闭环”；SciFiHydroLab 经 300 cm 分离结构实拼入选。

## 2026-07-18

- 创建 C++ Demo、轻量渲染与 C++ 优先工作流；初始化 Git LFS、内部工蜂备份和夜间只读维护。
