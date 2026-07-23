# TASK-20260721-005 — 电磁抓取 Physics Handle 基线

- Owner：Codex / 当前会话
- Status：handoff
- Stage：后续能力优化（基础攻击链可供闭环接入；释放、过早投掷与手感仍未验收）
- Created：2026-07-21
- Updated：2026-07-23

## 目标与验收

- 目标：先用 UE 官方 Physics Handle 实现可玩的第三人称电磁抓取基线，获得真实手感与性能数据，再决定是否用 Physics Control 或自研控制器扩展。
- 验收：宽容选取；右键吸取/持有、松开掉落、持有时左键投掷；物体保持抓取瞬间朝向，碰撞后自然旋转并由角阻尼停止；屏幕中心显示四段圆弧加中心点准星；无穿墙、卡死或明显持续抖动。
- 本轮追加验收：疯狂交替按鼠标左右键后松开全部输入，角色不得持续移动；准星采用可调的过肩构图；输入资源与磁力手感分别由两份互不依赖的 DataAsset 管理。
- 非目标：首步不做强制盾牌姿态、手动旋转、正式敌人、PCG、破坏系统、自研 PD 求解器和最终特效。

## 修改范围

- 允许修改：`Demo.uproject`（仅启用 Physics Control）；DailyPlan、本任务卡、代码注释规范；DailyPlan 明确列出的源码与 `/Game/ZeroEscape/` 独立资产。
- 共享/潜在冲突：保留当前文档重组、第三人称内容包、输入配置和其他活跃任务改动；不修改旧任务卡和第三方资产。
- 并行拆分/依赖：磁力组件先冻结磁性物体配置契约；后续 PCG 只生成这些配置化 Actor，可独立实现。Physics Control A/B 依赖 Physics Handle 基线验收数据，不并行提前实现。
- 用户已授权范围：确认本方案为 Plan；允许启用 Physics Control，并已明确授权磁力 C++、中心 HUD、蓝图和独立原型关卡实现；UE 编辑器已关闭。
- 本轮用户已授权范围：专用 PlayerController、输入 DataAsset、移动取消路径、过肩相机基线、磁力 Tuning DataAsset 与现有参数迁移；暂不加入蓄力投掷和新物理算法。
- 本轮用户已授权并已落盘：GameMode/磁性道具改为“C++ 原生诊断后备 + 蓝图资源装配”，移除两处项目资产路径查找；未来 PCG 可选内容再使用软引用资源池。
- 2026-07-22 用户已授权：在现有 Physics Handle 基线上实现确定性吸取曲线；只修改磁力 Tuning DataAsset 类与抓取组件四个 C++ 文件，不扩展随机性、质量倍率、投掷或近墙算法。
- 2026-07-22 用户授权记录计划：先讨论并解决普通释放限速与稳定吸附前禁止投掷，再讨论蓄力投掷；本条仅授权写入讨论计划，尚未授权修改玩法代码或资产。

## 计划与检查点

- [x] 核对 Physics Handle 与 Physics Control 的本机 UE 5.7.4 实现边界。
- [x] 用户确认 Physics Handle 基线优先、自然旋转、不强制姿态。
- [x] 写入当日 DailyPlan 并启用 Physics Control 插件配置。
- [x] 在对话中展示第一检查点拟实现代码。
- [x] 获得用户明确授权后实现 Physics Handle 基线。
- [x] 构建、蓝图/资产编译保存、PIE 烟测与静态边界检查。
- [x] 检查点 A：输入 DataAsset、专用 PlayerController、移动取消路径与过肩相机；独立构建验证。
- [x] 检查点 B：磁力 Tuning DataAsset 接管现有手感参数；独立构建验证。
- [x] 用户按步骤创建并装配两份 DataAsset，重新编译保存角色蓝图。
- [ ] 用户试玩并决定保留、调参、Physics Control A/B 或自研扩展。
- [x] 确认资源装配去硬编码边界，并核对真实 GameMode/磁性道具蓝图结构。
- [x] 在对话中展示本检查点拟修改代码和人工装配步骤，并取得用户明确落盘授权。
- [x] 完成最小 C++/Config 改动、静态检查和两次完整构建。
- [x] 用户装配 GameMode 与磁性道具蓝图，并共同完成蓝图编译、PIE 和日志验证。
- [ ] 确认普通释放的线速度/角速度上限，以及自动安全释放是否采用同一上限。
- [ ] 确认“稳定吸附完成”的位置、相对速度和持续时间门槛，并在对话中展示拟实现代码。
- [ ] 用户明确授权后，串行实现释放限速与投掷就绪门禁，再做构建、PIE 和快速甩视角边界验证。
- [ ] 前置问题验收后，单独讨论蓄力时长、质量映射、摇晃表现、伤害/击退数据与未来体力接口。

## 验证

- 命令/场景：`DemoEditor Win64 Development` 完整构建；三个新蓝图编译/保存；`Level0` PIE 启动、持续运行、停止；错误日志检查；`git diff --check`；Tick 使用范围检查。
- 结果：首次 UHT 仅发现不支持的单位元数据，移除后两次构建均 `Result: Succeeded`；PIE 使用 `ZeroEscapePrototypeGameMode`，运行期间无 Error；角色/道具/GameMode 蓝图均 `UpToDate`；Physics Control 已加载但 Runtime 模块无插件依赖。
- 本轮检查点 A：新增 `UZeroEscapeInputConfig` 与 `AZeroEscapePlayerController`，移除角色内输入/角色资源路径硬编码，补齐 Move 的 Completed/Canceled 清理；修正一次 `UInputMappingContext` 完整类型 include 后，独立构建 `Result: Succeeded`。
- 本轮检查点 B：新增 `UMagneticGrabTuningData`，现有全局磁力参数只从该资产读取，`HeldAngularDamping` 从单物体配置迁入全局手感；UHT 与完整链接 `Result: Succeeded`。
- 本轮自查：DataAsset 内部字段改用 UE DataAsset 常规的 `EditAnywhere + BlueprintReadOnly`，角色/组件上的资产引用仍为 `EditDefaultsOnly`；输入资产额外校验 Axis2D/Boolean 值类型。最终重建 `Result: Succeeded`，`git diff --check` 无空白错误（仅已有行尾转换提示）。
- 本轮只读审查：移除批量 IMC 修改中的逐项强制同步重建；补齐 GameMode 头文件中文注释；依据 UE 5.7 Physics Handle 源码，把 `HandleLinearStiffness` 与 `HandleInterpolationSpeed` 的合法下限改为正数，避免“参数校验通过但功能实际停用”。修正后最终构建 `Result: Succeeded`。
- 资产：`/Game/ZeroEscape/` 下已生成 7 个输入、角色、道具、GameMode 与材质资产；默认地图路径不变，GameMode 在运行时生成 5/20/45/70 kg 四种测试体。
- 工具边界：本轮不再用 C++ 构造路径替代 UObject 资源装配。两种 DataAsset 类型已经可编译，但独立资产及继承组件引用需要用户在 UE 编辑器内创建/指定；完成后再由 AI 检查资产、蓝图编译和运行日志。
- 用户操作（现在）：打开编辑器，按交接步骤创建并填写 `DA_ZeroEscapeInput` 与 `DA_MagneticGrabTuning`，在 `BP_ZeroEscapeCharacter` 分别指定输入资产与磁力组件 Tuning，并检查 Mesh/AnimBP/CameraBoom 后编译保存。
- 用户验收重点：准星不必精确压住物体中心；物体被撞后可自然旋转；快速转身不应持续抖动；投掷后必须先松开右键才能再次抓取；不同质量的跟随与投掷手感是否可接受。
- 用户验收：待验收。

### 2026-07-21 首次 DataAsset 装配复查

- 用户现象：移动、转视角和跳跃正常，鼠标左键/右键磁力交互无效果。
- 已确认：`BP_ZeroEscapeCharacter` 已保存 `DA_ZeroEscapeInput`；`IMC_ZeroEscape` 包含 LeftMouseButton、RightMouseButton 与两个磁力 Input Action。
- 根因证据：运行日志连续报告 `ElectromagneticGrab 尚未指定 MagneticGrabTuningData，磁力功能已停用`；角色蓝图资产中也不存在 `DA_MagneticGrabTuning` 引用。
- 当前处理：不改代码，由用户在角色蓝图的原生 `ElectromagneticGrab` 组件模板上指定 Tuning Data，编译保存后重新 PIE；随后由 AI 再读日志确认。

### 2026-07-21 DataAsset 装配恢复确认

- 用户确认：重新装配后左右键磁力交互已经可用，输入与磁力两份 DataAsset 均进入实际玩法链路。
- 当前状态：基础交互可用，但吸取质量感、普通释放速度上限和蓄力投掷仍需按独立方案讨论、预览代码并授权后实施。
- 本轮最小整理：仅把 Actor、HUD 与 Build.cs 中遗留的英文说明改为中文，不改变玩法逻辑。
- 验证：`git diff --check` 通过，英文说明复查只剩 Epic 版权行与中文注释中的必要技术名词；完整构建的 UHT 和源码编译通过，但 UE 编辑器占用 `UnrealEditor-Demo.dll` 导致最终链接失败，需关闭编辑器后补跑，不能记为构建成功。

## 阻塞、风险与下一步

- 阻塞：项目 Memory MCP 已改为 Demo 专用服务名，但当前会话仍暴露旧服务名，疑似未重载；为避免再次错写其他工程，本轮不写项目记忆。源码实施不受影响。
- 风险：移动卡键只能通过原复现步骤确认，`ClearMoveInput`、专用 GameOnly 控制器和按键刷新属于方向性修复，不能用编译结果代替验收。Physics Handle 基线仍可能在重物、快速转身或狭窄门框下出现不满意的延迟/抖动。
- 已记录旧算法风险：墙面距离小于 `MinimumHoldDistance` 时，现有安全目标公式可能短暂把目标设到墙后，随后才按阻挡延迟释放；本轮只做配置迁移，未夹带安全算法改写，联合验收时需重点复现后再单独确定修法。
- 下一步：用户试玩后按现象选择只调参、补候选高亮与电弧、做 Physics Control A/B，或在有明确控制缺口时再实现自研控制层。

### 2026-07-22 吸取曲线手感检查点（已授权，实施中）

- Owner：Codex / 当前会话；沿用本任务卡，避免与磁力基线产生重叠文件 Owner。
- 目标：只增加“吸取阶段的动态曲线锚点”，让 Physics Handle 的目标从物体质心沿小弧线、按距离控制的时长移动到持有点；Chaos 继续决定物体的真实质量、惯性、旋转与碰撞响应。
- 明确非目标：本检查点不加入随机摆动、磁性强弱倍率、Timeline、蓄力投掷、普通释放限速、Physics Control、自研 PD 或近墙安全算法重写。
- 预计修改文件：`Source/Demo/Public/Data/Magnetism/MagneticGrabTuningData.h`、`Source/Demo/Private/Data/Magnetism/MagneticGrabTuningData.cpp`、`Source/Demo/Public/Components/Magnetism/ElectromagneticGrabComponent.h`、`Source/Demo/Private/Components/Magnetism/ElectromagneticGrabComponent.cpp`；方案确认后才创建对应 `DOC/DailyPlan/`。
- 数据边界：全局曲线参数仍只属于 `UMagneticGrabTuningData`；吸取起点、已用时间和吸取/持有阶段只属于 `UElectromagneticGrabComponent`；`UMagneticObjectComponent` 本轮不变。
- 依赖与冲突：依赖现有 Physics Handle 基线和独立 Tuning DataAsset；与近墙安全、释放限速、蓄力投掷共享磁力组件文件，必须串行实施，不做并行修改。
- 已确认方案：新增四项全局参数；采用 `S = T^4(5 - 4T)` 非对称进度、小幅确定性弧线和动态安全持有终点；Physics Handle 约束全程有效，Pulling 只关闭目标插值，曲线结束后恢复并进入 Holding。
- 授权结论：用户已明确要求开始实现；正式范围已写入 `DOC/DailyPlan/2026-07-22-电磁抓取曲线手感.md`。
- 已落盘：DataAsset 新增四项曲线参数及校验；抓取组件新增 Pulling/Holding 状态、非对称曲线、小弧线、动态安全终点、Handle 插值快照/延迟恢复和 PrePhysics Tick 顺序。
- 静态验证：`git diff --check` 通过（仅既有 LF→CRLF 提示）；曲线采样确认 `S(0)=0`、`S(1)=1`、全程单调、速度峰值位于 `T=0.75`；独立代码审查未发现阻塞项。
- 构建检查：用户安全关闭编辑器后重跑 `DemoEditor Win64 Development`，`UnrealEditor-Demo.dll` 链接与目标元数据写入成功，最终 `Result: Succeeded`。
- 编辑器验证：重启后 UE/MCP ready；`BP_ZeroEscapeCharacter`、`BP_ZeroEscapePrototypeGameMode` 与 `BP_MagneticProp` 均 `UpToDate`、零错误零警告。
- PIE 烟测：`Level0` 正常运行，GameMode 为 `BP_ZeroEscapePrototypeGameMode_C`；启动后日志区间无 Error，未出现磁力或输入 DataAsset 配置错误，停止 PIE 后状态正常。
- 当前检查点：机器验证已通过；工具无法可靠模拟真实鼠标抓取，近/中/远距离、5/20/45/70 kg、末段快速转身、Pulling 中松开/投掷与墙体阻挡仍需用户实际试玩，状态保持待用户验收。

### 2026-07-21 资源装配去硬编码检查点

- 已移除 GameMode 中角色蓝图的 `FClassFinder` 和磁性道具中的项目材质 `FObjectFinder`；C++ 内不再保存 `/Game/ZeroEscape/...` 资产路径。
- `PrototypePropClass` 由 GameMode 蓝图配置；原生角色仅为非空诊断后备，漏配时会输出明确错误，不能冒充完整可玩角色。
- `DefaultEngine.ini` 的唯一启动入口改为 `BP_ZeroEscapePrototypeGameMode`；对应蓝图、角色、磁性道具和两份 DataAsset 均已确认处于 Git 跟踪范围。
- 验证：两次 `DemoEditor Win64 Development` 完整构建均 `Result: Succeeded`；`git diff --check` 通过；只读交叉审查无 UHT/C++ 阻塞。
- 下一步人工装配：GameMode 蓝图指定角色与道具子类；磁性道具蓝图删除重复 `MagneticMesh`，在继承的 `MagneticBody` 上指定材质；之后编译保存并 PIE。
- 人工装配验证：三个相关蓝图均 `UpToDate`、零错误零警告；`BP_MagneticProp` 已无蓝图新增组件；资产依赖包含角色、磁性道具与测试材质。
- PIE 验证：`Level0` 正常运行，日志确认 `Game class is 'BP_ZeroEscapePrototypeGameMode_C'`，本轮未出现角色、道具或磁力 DataAsset 漏配错误；用户已完成装配，本检查点通过机器验证。

### 2026-07-22 普通释放限速、投掷就绪门禁与蓄力投掷（讨论计划）

#### 已确认的试玩问题

1. 右键持有后快速转动视角再松开右键，刚体会保留 Physics Handle 为追赶锚点产生的高线速度/角速度，可能获得超过正式投掷的动能。
2. 当前 `IsHoldingObject()` 在 `Pulling` 阶段已经成立，左键可以在物体尚未稳定到达身前时投掷。

#### 必须先完成的两个前置检查点

- **普通释放能量上限**：玩家主动松开、吸取途中松开和安全断开都不能成为攻击手段；释放时保留方向与少量惯性，但分别钳制线速度和角速度。正式投掷不使用普通释放上限，而是由显式投掷参数决定最终速度。
- **投掷就绪门禁**：曲线结束不等于刚体已经稳定到位。只有物体进入身前允许误差范围、相对持有目标的线速度与自身角速度足够低，并连续稳定一小段时间后，才进入可投掷状态；不满足时左键不投掷、不提前释放物体。
- 两项都由 `UElectromagneticGrabComponent` 持有运行态；阈值继续由 `UMagneticGrabTuningData` 唯一配置。预计仍只涉及以下文件，且必须与当前磁力任务串行修改：
  - `D:\UE5projects\Demo\Source\Demo\Public\Components\Magnetism\ElectromagneticGrabComponent.h`
  - `D:\UE5projects\Demo\Source\Demo\Private\Components\Magnetism\ElectromagneticGrabComponent.cpp`
  - `D:\UE5projects\Demo\Source\Demo\Public\Data\Magnetism\MagneticGrabTuningData.h`
  - `D:\UE5projects\Demo\Source\Demo\Private\Data\Magnetism\MagneticGrabTuningData.cpp`
  - `/Game/ZeroEscape/Data/Magnetism/DA_MagneticGrabTuning`

#### 后续蓄力投掷设想

- 状态顺序暂定为：`Pulling（吸取中） → Settling（到位收敛中） → Ready/Charging（稳定后自动蓄力） → Thrown 或 Dropped`。
- 物体稳定到身前后，立即按左键执行基础投掷：距离较短、动能较低，只造成轻量影响、少量伤害和轻度打断/击退。
- 继续按住右键则自动积累蓄力；蓄力期间随时允许按左键，以当前蓄力比例投出。
- 蓄力越高，物体摇晃越明显；摇晃必须有界且不能依靠持续叠加随机冲量蓄积隐藏动能。正式投掷结果仍由显式的蓄力比例、物体质量/配置和投掷参数决定。
- 达到最大蓄力后，投掷获得该物体允许的最大速度/动能、最远有效距离、最大伤害和击退；不得通过继续等待或甩镜头突破上限。
- 质量如何影响距离、速度、冲量、伤害和击退需要单独确定。现有 `ThrowSpeedMultiplier` 可作为物体配置入口，但最终公式尚未确认。
- 体力属于后续扩展：蓄力消耗体力，耗尽时强制走受限速保护的安全放下，并进入体力恢复；本轮不实现体力属性、UI 或恢复规则。

#### 实施门禁与验收重点

- 当前只记录需求和候选状态机；先确认参数语义与拟实现代码，再由用户明确授权落盘。
- 验收必须覆盖：吸取中左键、刚进入身前但仍高速摆动时左键、稳定后基础投掷、快速左右甩镜头后松右键、快速俯仰后松右键、不同质量物体、墙边自动释放，以及普通放下速度始终低于正式投掷能力。

#### 2026-07-23 优先级收口

- 用户确认上述内容属于后续玩家能力调优与扩展，不阻塞当前“PCG 场景 → 追猎者 → 地图玩法闭环”关键路径。
- 本卡移出 `active/` 但不视为手感验收通过；普通释放能量、过早投掷、蓄力与 Physics Control A/B 均保留为后续优化方向。
