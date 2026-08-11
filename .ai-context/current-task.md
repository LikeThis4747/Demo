# Current Task

## 当前任务

完成追猎者物理轻受击“效果原型”的拟实现稿。当前不设计生产接入，不把测试物、磁力投掷或 Heavy 生命周期揉进轻受击效果。

## 当前权威方向

- 第一轮新增 C++ 为 0。
- 只新增一个独立测试 Physics Control Asset、一个普通 Actor Blueprint 测试目标、一个独立测试关卡。
- 测试目标仅有追猎者 Mesh、固定 Idle 与独立 PhysicsControl；胸、头、左右手臂在测试碰撞前已受动画目标约束地模拟，spine_01、骨盆、腿和脚保持 Kinematic。
- 用户可用任意真实模拟物体直接撞击；效果资产不生成、不识别、不控制箱子或机关，也不绑定 Hit、不补角色冲量、不播 Montage。
- 只先比较 Kinematic 与 Controlled 的 60 FPS 画面，判断外物反作用、部位相关让位、站立稳定和自然回稳。

## 冻结范围

- 已验收 HeavyImpact 的 C++、PCA、DataAsset、击飞、倒地、起身和误预测回滚全部不改。
- 生产玩家、BP_Pursuer、AnimBP、CharacterImpact、磁力、破碎、地刺、PCG、Level0 与 ThrustGuidedHazard 全部不改。
- 全身常驻、Light/Heavy 大一体化、箱子—角色跨对象 Reserve/Prepare/Commit/Clearing 均为撤销的研究草稿，不得实施。
- 效果通过前不抽共享组件、不预留生产接口。

## 当前阻塞

- 首次写入三个测试资产前，仍须完成完整 Git 基线门禁。
- 工作区另有不属于本任务的 Level0 与 AS_Pursuer_ChargeRun_Work 改动；不得修改、回退或纳入本任务提交。
