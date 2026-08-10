# Active Context — Demo

## 当前焦点

- 磁力投掷物 P0 方案已经用户确认；用户已授权完整 Git 基线和首个 Geometry Collection 手工制作，C++/Blueprint 运行逻辑仍未授权。
- 壁挂式物理制导一次性机关已完成稳定性重构的代码、默认参数、Blueprint 回读和 Demo-only 构建，仍等待用户 PIE 手感验收。
- HeavyImpact 重受击继续作为已验收阶段基线；轻受击融合是独立讨论任务，不与磁力物破碎方案合并。

## 磁力投掷物 P0 合同

- 完整 `UStaticMeshComponent` 刚体继续负责拉取、持有、普通放下和正式投掷；Geometry Collection 只在合格命中后生成。
- 只有 `ThrowHeldObject()` 写入道具自身的破碎资格；`AttackProjectile` Tag 仍只服务目标受击，不能兼任破碎状态。
- 拉取、持有、普通放下、安全释放和普通外力碰撞不破碎；命中前重新抓取会取消旧资格。
- 第一次 Blocking Hit 只排队一次，下一帧用原刚体碰撞后的 Transform、线速度和角速度生成短命 Geometry Collection；生成成功后才移除完整 Actor。
- P0 所有碎片经 Remove On Break 清空；P1 才考虑大物体另外生成独立小磁力物，不把 Geometry Collection 单块直接做成可抓取资源。

## 当前资产与接口证据

- 官方 UE MCP：`BP_MagneticProp` 父类为项目中的 `MagneticPrototypeProp`；20 kg 模拟物理根刚体使用 `SM_crate4`，Blueprint 与源网格均非 Dirty。
- `SM_crate4` 约 80 cm 立方，LOD0 492 三角形/510 顶点、2 LOD、1 材质槽；第三方原资产保持只读。
- UE5.8 引擎源码确认 Geometry Collection 初始速度、显式解簇、External Strain 和 Remove On Break 路径可用。
- 首轮资产建议是项目自有 `GC_MagneticCrate4_P0`，Uniform Voronoi 约 12～16 块、单层簇、全部叶子移除；不启用任意碰撞自动损坏。

## 当前验证边界

- 本轮只有只读源码/资产/引擎接口核对和文档落盘，没有 C++ 构建、Blueprint 编译、Geometry Collection 制作、PIE 或玩家验收。
- 官方 MCP 回读磁力 Blueprint 和静态网格均非 Dirty；没有修改 `/Game/Assets/SFCorridors/**`。
- 用户已统一确认当前制导机关、轻受击、磁力破碎方案和项目记忆改动全部保留并纳入一次基线；基线完成前不创建 Geometry Collection。

## 下一步与门禁

1. 完成全工作区基线 commit、内部工蜂 push、远端包含核验和 clean status。
2. 在当前非 Dirty 的 Level0 测试区逐步制作 `GC_MagneticCrate4_P0`，先验收 12～16 块的外观、切面和碎片数量。
3. Geometry Collection 资产通过后，再由用户决定是否授权 C++ 状态替换和 Blueprint 装配；P0 未验收前不进入 P1。
