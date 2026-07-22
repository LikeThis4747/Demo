# 《零号逃亡》磁力交互垂直切片 — 详细代码分析

- 分析日期：2026-07-21
- 代码基线：git `1e90d46`（2026-07-20 23:04:11 +0800）之后的未提交工作区改动（`Source/` 全目录尚未纳入版本控制）
- 引擎：UE 5.7.4 / 模块 `Demo`（单 Runtime 模块）
- 分析范围：8 个 C++ 类 + 4 个蓝图 + 2 个 DataAsset 实例

---

## 0. 一分钟速览

这是《零号逃亡》的**第一个可玩垂直切片**：第三人称角色，右键"电磁吸取"场景中的物理刚体到身前悬停，左键把它朝准星方向投掷出去。目前跑通的是"选取 → 持有 → 安全悬停 → 投掷/放下"这一条完整手感链，外加一个临时的测试关卡（GameMode 运行时生成 4 个不同质量的方块）和一个纯 Canvas 画的准星。

代码整体是**"C++ 写逻辑 + DataAsset 存配置 + 蓝图只做装配"**的分工。质量非常高：状态归属清晰、所有临时物理改动可回退、参数全部外置到可校验的 DataAsset、错误有日志。策划案里更大的两块（PCG 关卡生成、追猎者）还没开始写。

---

## 1. 架构总览

### 1.1 分层与职责

```
玩家输入 (Enhanced Input)
      │  IA_* / IMC_ZeroEscape，资源集中在 DA_ZeroEscapeInput
      ▼
AZeroEscapeCharacter  ……… 装配层：相机/PhysicsHandle/组件 + 输入转发，本身不含玩法算法
      │  Configure() 注入 PhysicsHandle + Camera
      │  BeginGrab / EndGrab / Throw 转发意图
      ▼
UElectromagneticGrabComponent  ……… 玩法核心：选取/持有/投掷/安全恢复状态机（唯一状态 Owner）
      │  读取 ← UMagneticGrabTuningData（全局手感参数，18 项，带校验）
      │  查询 ← UMagneticObjectComponent（单个物体的磁性标记 + 差异）
      │  驱动 → UPhysicsHandleComponent（UE 引擎约束）
      ▼
Chaos 刚体（AMagneticPrototypeProp 承载：Cube Mesh + 模拟物理 + 磁性组件）

旁路：
AZeroEscapePrototypeGameMode ……… 临时测试台：指定 Pawn/Controller/HUD + 运行时 spawn 4 个测试方块
AZeroEscapeHUD               ……… 纯 Canvas 画准星（四段圆弧 + 中心点），无 UMG 依赖
```

### 1.2 一条核心设计原则贯穿始终：engine-first + 单一状态 Owner

- **engine-first**：物理约束用引擎自带的 `PhysicsHandleComponent`，投掷用 `AddImpulse`，质量用 `SetMassOverrideInKg`。项目自己**不重写任何物理积分**，只写"玩法策略"（选谁、放哪、什么时候松手）。这与策划案"基于 Chaos 的定制玩法系统，不假装自研物理引擎"的定位一致。
- **单一状态 Owner**：谁持有物体、临时改了哪些物理设置、输入锁状态，**全部且只在** `UElectromagneticGrabComponent` 里。角色只是"传话筒"，物体只存自己的静态配置。这让"谁该负责恢复现场"永远只有一个答案。

### 1.3 模块依赖

`Demo.Build.cs` 依赖：`Core / CoreUObject / Engine / InputCore / EnhancedInput`。
没有引入 GAS、没有联机模块、没有 Slate/UMG（HUD 用引擎 Canvas 直接画）。依赖面很干净，适合三周单机。

---

## 2. 目录结构

```
Source/Demo/
├─ Demo.Build.cs / Demo.cpp / Demo.h        模块入口
├─ Public/  (对外头文件，与 Private 镜像)
│  ├─ Characters/     ZeroEscapeCharacter.h
│  ├─ Components/Magnetism/
│  │     ElectromagneticGrabComponent.h    抓取状态机
│  │     MagneticObjectComponent.h         物体磁性标记
│  ├─ Actors/Magnetism/
│  │     MagneticPrototypeProp.h            测试用磁性刚体
│  ├─ Data/
│  │  ├─ Magnetism/MagneticGrabTuningData.h 全局手感参数 DataAsset
│  │  └─ Input/ZeroEscapeInputConfig.h      输入资源 DataAsset
│  ├─ GameFlow/       ZeroEscapePrototypeGameMode.h
│  └─ UI/             ZeroEscapeHUD.h
└─ Private/  (实现，与 Public 一一对应)
```

对应的 UE 资产（Content，编辑器内 `/Game/ZeroEscape`）：

```
/Game/ZeroEscape/
├─ Characters/BP_ZeroEscapeCharacter          （父类 = C++ AZeroEscapeCharacter）
├─ GameFlow/BP_ZeroEscapePrototypeGameMode     （父类 = C++ GameMode）
├─ Interaction/Magnetism/
│     BP_MagneticProp                          （父类 = C++ AMagneticPrototypeProp）
│     M_MagneticPrototype                       测试材质
├─ Data/Magnetism/DA_MagneticGrabTuning         手感参数实例
├─ Data/Input/DA_ZeroEscapeInput                输入资源实例
└─ Input/  IMC_ZeroEscape + Actions/IA_*        输入映射与动作
```

设计原则：目录按"稳定职责"划分，没有实际文件就不建空目录。头文件只有需要被别的文件引用才放 `Public`。

---

## 3. 运行时数据流（一次完整"抓取—投掷"）

1. **启动**：`BP_ZeroEscapePrototypeGameMode` 指定玩家 Pawn = `BP_ZeroEscapeCharacter`、HUD = `AZeroEscapeHUD`，`BeginPlay` 里 spawn 4 个 `AMagneticPrototypeProp`（质量 5/20/45/70 kg，形状各异）。
2. **组件接线**：角色 `PostInitializeComponents()` 调 `ElectromagneticGrab->Configure(PhysicsHandle, FollowCamera)`。Configure 内校验 `TuningData` 合法后，把 3 个 Physics Handle 参数（刚度/阻尼/插值）写进引擎组件，并置 `bConfigurationReady = true`。
3. **输入注册**：`PawnClientRestart()` 按 `DA_ZeroEscapeInput` 里的 Mapping Context 幂等添加输入映射（先移除再添加 + 忽略当前按住的键，防幽灵输入）；`SetupPlayerInputComponent` 校验输入配置后逐个 `BindAction`。
4. **按下右键**：`BeginMagneticGrab → ElectromagneticGrab->BeginGrabInput()`。若配置就绪且没在锁定/持有中，调 `FindBestCandidate()` 做一次屏幕空间选取。
5. **选取评分**（`FindBestCandidate`）：以相机为球心、`GrabRange` 为半径做 `OverlapMultiByObjectType(ECC_PhysicsBody)`，对每个候选：查 `MagneticObjectComponent::CanGrab`（磁性 + 在模拟 + 质量 ≤ 上限）→ 距离/可见性过滤（射线检测防隔墙）→ 把物体包围球投影到屏幕算"轮廓到准星的像素距离"→ 综合 `屏幕分 + 世界距离分×0.35 − 优先级×0.05` 取最低分。
6. **抓取**（`GrabCandidate`）：快照物体当前角阻尼和对 Pawn 的碰撞响应 → 临时改写（角阻尼用 TuningData 值、对 Pawn 设 Ignore 防推人）→ `PhysicsHandle->GrabComponentAtLocation(质心)` → 开启组件 Tick。
7. **持有中**（`TickComponent`，仅持有期间跑）：每帧算期望悬停点（角色位置 + 上/前/右偏移）→ 射线检测若被墙挡则缩短锚点距离 → `SetTargetLocation`。同时监控两个安全条件：连续被挡超过 `ObstructionReleaseDelay`，或过了 `PullGracePeriod` 后质心与目标误差仍超 `MaximumHoldError`，任一满足就自动安全释放。
8. **左键投掷**（`ThrowHeldObject`）：算准星命中点（相机射线）→ 方向 × `ThrowSpeed × 物体倍率` = 目标速度 → **先释放**（恢复现场 + 置输入锁）→ 用"速度变化冲量"`AddImpulse(目标速度 − 现速度, true)`。用速度变化而非固定冲量，是为了让投掷速度不随物体质量线性衰减、手感可预测。投掷后必须先松开右键才能抓下一个（`bAwaitingGrabRelease`）。
9. **放下 / 结束**：松右键 `EndGrabInput` 或组件 `EndPlay`，都走 `ReleaseHeldObject` 恢复角阻尼和碰撞、清空引用、关 Tick。

---

## 4. 逐文件详解

### 4.1 `AZeroEscapeCharacter`（Characters/）— 装配 + 输入转发

**职责**：搭第三人称相机臂 + 相机 + PhysicsHandle + 磁力组件；把 Enhanced Input 转成移动/视角/磁力意图。**刻意不含任何磁力算法**。

- 构造函数：胶囊体、关闭控制器直接转向改用"朝移动方向转"、相机臂 300cm + SocketOffset 做过肩构图、创建 PhysicsHandle 和 ElectromagneticGrab 两个组件。`PrimaryActorTick.bCanEverTick = false`（不用 Tick）。
- 输入全部走 `DA_ZeroEscapeInput`（`UZeroEscapeInputConfig` 类型的 `InputConfig` 指针）。没有硬编码资源路径。
- `PawnClientRestart()`：`FlushPressedKeys()` + `ApplyInputMappingContexts()`（先移除后添加，`bIgnoreAllPressedKeysUntilRelease` 防止切换瞬间残留按键触发幽灵输入）。
- `UnPossessed()`：移除本角色加的输入上下文、清空移动输入、`EndMagneticGrab()`（防止换 Pawn 时物体被遗留在持有态）、`StopJumping()`。**这是很多项目会漏的清理，做得很到位**。
- `SetupPlayerInputComponent`：先 `Cast` EnhancedInputComponent（失败打 Error 日志），再校验 `InputConfig->IsConfigured()`（失败也打 Error 并 return，不会崩），然后逐个 BindAction。移动/抓取都绑了 Triggered + Completed + Canceled 三态，保证输入能被干净地结束。
- `Move`：只用控制器 Yaw 算前后左右，相机俯仰不会让角色上下飘。`ClearMoveInput`：Completed/Canceled 时消费残留输入向量。

### 4.2 `UElectromagneticGrabComponent`（Components/Magnetism/）— 玩法核心

整个切片最重的类（约 460 行 cpp）。是选取/持有/投掷/安全恢复的**唯一状态 Owner**。

**状态字段**：`HeldComponent`（弱引用当前持有刚体）、`HeldMagneticObject`（弱引用其磁性配置）、`PreviousAngularDamping` / `PreviousPawnCollisionResponse`（抓取前快照，用于恢复）、`HeldElapsedSeconds` / `ObstructedElapsedSeconds`（安全计时）、`bConfigurationReady`（Configure 校验缓存）、`bAwaitingGrabRelease`（投掷后输入锁）。用弱引用是为了不阻止物体被销毁。

**关键方法**：
- `Configure`：注入引用 + 三级校验（PhysicsHandle/Camera 有效 → TuningData 非空 → `TuningData->IsConfigured()`），任一失败打明确 Error 日志并保持 `bConfigurationReady = false`（功能停用但不崩）。校验通过才把手感参数写进 Handle。
- `IsConfigurationReady()`：Tick 里高频调用，只读缓存 bool + 校验对象仍有效，**不每帧重新遍历 18 个参数**，兼顾安全和性能。
- `FindBestCandidate`：见 §3.5。用"包围球投影到屏幕"算轮廓像素距离，让大/不规则物体靠近准星边缘也能选中——这是策划案"快速追逐中容易选中"的直接实现。有 `MaximumCandidateChecks` 上限控制单次开销。
- `TickComponent`：仅持有期间启用。每帧更新安全悬停点 + 检查两条自动释放条件。首个判断就是 `if (!IsConfigurationReady() || !IsHoldingObject()) ReleaseHeldObject(true)`，任何不变量被破坏都安全兜底。
- `ReleaseHeldObject`：**所有退出路径（正常放下、投掷、Tick 失效、EndPlay）都走这里**，统一恢复角阻尼 + 碰撞响应、清空引用、关 Tick。这是"临时物理改动绝不泄漏"的保证。
- `ResolveSafeHoldLocation`：一条射线检测悬停锚点是否穿墙，命中就在障碍前保留 `ObstructionClearance` 间隙、但不小于 `MinimumHoldDistance`。

**性能**：默认不 Tick，仅持有时开；选取用有界 Overlap + 候选上限；没有全局 Actor 搜索、没有每帧同步加载。符合项目性能基线。

### 4.3 `UMagneticObjectComponent`（Components/Magnetism/）— 物体磁性标记

极薄的纯配置组件（无 Tick）。3 个可编辑属性：`bMagnetizable`（是否可吸）、`SelectionPriority`（重叠时优先级）、`ThrowSpeedMultiplier`（该物体的投掷倍率）。唯一方法 `CanGrab` 判断"磁性开 + 正在模拟物理 + 质量 ≤ 传入上限"。它只回答"我能不能被抓"，不碰任何 Chaos 状态。

### 4.4 `AMagneticPrototypeProp`（Actors/Magnetism/）— 测试用磁性刚体

自带 `StaticMeshComponent`（root，引擎 Cube + PhysicsActor 碰撞 + 模拟物理 + 线/角阻尼）+ `MagneticObjectComponent`。构造时加载引擎 Cube 和测试材质。`ConfigurePrototype(scale, mass)` 供 GameMode 生成不同规格的方块。**质量在 `BeginPlay` 里用 `SetMassOverrideInKg(NAME_None, mass, true)` 应用**——时序正确（物理体创建后），是"设了质量不生效"这个经典坑的正确写法。

### 4.5 `UMagneticGrabTuningData`（Data/Magnetism/）— 全局手感 DataAsset

把抓取组件里原本硬编码的 **18 个手感参数**全部外置：选取（范围/准星容错/质量上限/候选数）、持有（距离/侧偏/高度/角阻尼）、安全（最小距离/间隙/阻挡释放延迟/宽限期/最大误差）、投掷（速度/瞄准距离）、Physics Handle（刚度/阻尼/插值）。每个属性都有中文注释说明"对应哪个 C++ 属性、被谁读、单位、初值、范围、调高调低的效果"——**这份注释本身就是一份调参手册**。

`IsConfigured(FString& OutError)` 做两件事：逐项校验数值有限且在范围内（用一个本地 lambda 复用逻辑），以及跨属性约束 `MinimumHoldDistance ≤ HoldDistance`。失败返回中文原因（含属性名和实际值），能直接定位到编辑器里改哪个字段。

### 4.6 `UZeroEscapeInputConfig`（Data/Input/）— 输入资源 DataAsset

集中声明输入资源：一个 `FZeroEscapeInputMappingContextConfig` 数组（每项 = 一个 IMC + 优先级）+ 6 个 InputAction（Move/Look/MouseLook/Jump/Grab/Throw）。`IsConfigured` 校验：至少一个 Mapping Context、无空项、无重复 IMC、每个 Action 非空**且值类型匹配**（Move/Look/MouseLook 必须 Axis2D，其余必须 Boolean）。类型校验能挡住"把 Boolean 动作误装到 Move 上"这种运行时才炸的错。

### 4.7 `AZeroEscapePrototypeGameMode`（GameFlow/）— 临时测试台

指定 Pawn/Controller/HUD 类。`BeginPlay` 里 `SpawnPrototypeProps()` 生成 4 个写死的测试方块（位置/缩放/质量在一个 `FPrototypePropCase` 结构数组里）。注释明确写了"这是验证用的固定 lineup，不是 PCG"——**诚实标注了临时性**，将来 PCG 就绪后可关掉 `bSpawnPrototypeProps`。

### 4.8 `AZeroEscapeHUD`（UI/）— 纯 Canvas 准星

不依赖 UMG。`DrawHUD` 在屏幕正中画 4 段分离圆弧（45/135/225/315 度）+ 一个中心实心点，形似霰弹枪准星。`DrawArc` 用有限直线段逼近圆弧，`DrawCenterDot` 按圆方程逐扫描线画真圆点。所有尺寸/颜色/段数可编辑。零资产依赖，适合原型期。

---

## 5. 蓝图与资产层

4 个蓝图**全部是"薄壳"**：父类都是对应 C++ 类，`compile_status = UpToDate`，EventGraph 里只有引擎默认的、被禁用的空事件节点（BeginPlay/Tick/Overlap），**0 条连线、无自定义图逻辑**。完全符合"逻辑在 C++、蓝图只做配置装配"的分工。

- `BP_ZeroEscapeCharacter`：作用是在 Class Defaults 里指定 `InputConfig = DA_ZeroEscapeInput`、`ElectromagneticGrab.TuningData = DA_MagneticGrabTuning`（这两个赋值是功能能否跑起来的前提）。
- `BP_MagneticProp`：比 C++ 父类多挂了一个 `MagneticMesh`（StaticMeshComponent，挂在 root `MagneticBody` 下）。用途待确认——父类的 `MagneticBody` 本身已是可见 Cube，这个子 Mesh 若非有意装饰则冗余。
- `BP_ZeroEscapePrototypeGameMode`：只带默认场景根，无额外逻辑。

---

## 6. 设计亮点

1. **状态归属零歧义**：抓取相关的一切都在一个组件里，角色/物体/HUD 都不碰。
2. **全路径可回退**：临时改的物理设置（角阻尼、碰撞响应）在每一条退出路径都被恢复，`IsHoldingObject` 用"本地引用 + Handle 实际抓取物"双重校验，不会状态错位。
3. **配置数据化 + 可校验**：手感和输入都在 DataAsset，带带中文原因的校验，非法配置在启用前就被明确拒绝并打日志，而不是运行时诡异失效。
4. **性能自觉**：默认不 Tick、按需开 Tick、有界查询、无全局搜索。
5. **注释即文档**：每个文件头写清职责/边界/状态 Owner，每个参数写清语义/单位/调节效果。可读性远超一般 Demo。
6. **诚实**：临时测试台、engine-first 边界都在注释里如实标注，不夸大自研成分。

---

## 7. 已知边界与待办（不影响当前可玩性）

1. **BP 里两个 DataAsset 引用必须已赋值**：`InputConfig` 和 `TuningData` 是 C++ `EditDefaultsOnly` 裸指针，没赋值会静默停用输入/磁力（有 Error 日志但不崩）。运行时看 `LogZeroEscapeInput` / `LogZeroEscapeMagneticGrab` 即可确认。
2. **选取依赖 `ECC_PhysicsBody` 对象类型**：将来 PCG 生成的物体若碰撞对象类型不是 PhysicsBody，会被静默漏选，建议明确约定或改自定义 Channel。
3. **候选截断顺序**：Overlap 结果不保证按距离排，物体密集且超过 32 个时可能截掉该选的那个，密集场景需压测。
4. **`BP_MagneticProp` 的 `MagneticMesh` 子组件用途待定**。
5. **策划案的大头还没写**：PCG 整关生成、追猎者 AI 及其物理受击反馈、胜负/重开流程。当前只有磁力交互这一条垂直切片 + 临时测试关。

---

## 8. 结论

当前代码是一个**质量很高、边界清晰、可直接试玩**的磁力交互垂直切片。工程习惯（状态归属、可回退、数据化配置、注释）达到可作团队样板的水准。下一步的重点不在打磨已有代码，而在：先把 PCG 生成算法和追猎者的"接口/委托契约"定下来，再动手实现，避免后期耦合返工。
