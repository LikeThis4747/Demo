# 2026-07-21 电磁抓取 Physics Handle 基线

## 目标与边界

- 先用 UE 官方 `UPhysicsHandleComponent` 完成可试玩基线，再依据手感和性能决定调参、Physics Control A/B 或自研控制器。
- 物体保持抓取瞬间朝向；持有时不施加目标旋转，碰撞可令其自然旋转，并通过角阻尼逐渐停止。
- 屏幕中心增加轻量霰弹式准星：四段圆弧包围中心点，为宽容选取和投掷方向提供稳定参照。
- 首步不做强制盾牌姿态、手动旋转、正式敌人、PCG、破坏、自研 PD 和最终电弧特效。
- 用户已确认本方案并授权落盘实现；UE 编辑器已关闭，可开始源码、资产与独立原型关卡实施。

## 玩家交互基线

- 准星附近宽容选取可见磁性物体，遮挡物不可抓取。
- 按住右键：抓取质心并保持在玩家前侧安全距离。
- 松开右键：解除约束，自然下落并保留速度。
- 持有时按左键：朝准星命中点投掷；投掷后需松开右键才能再次抓取。
- 被障碍持续阻挡、距离目标过远时安全断开，不允许穿墙或持续积累约束误差。

## 职责与状态 Owner

- `AZeroEscapeCharacter`：第三人称相机、移动、输入转发和组件装配，不持有磁力规则。
- `AZeroEscapeHUD`：只绘制四段圆弧与中心点准星，不持有磁力玩法状态。
- `UElectromagneticGrabComponent`：唯一持有抓取状态、选取、Physics Handle 目标、放下/投掷、遮挡断开和调试数据。
- `UMagneticObjectComponent`：声明物体可被抓取，并保存抓取优先级、允许质量和持有角阻尼等配置；释放时恢复物体原始参数。
- Chaos/Physics Handle：刚体、碰撞、约束、速度积分和投掷后物理。
- Physics Control：先启用但基线不引用；只有 Physics Handle 实测存在明确缺口时才做 A/B。

## 预计文件与资产

### 当前配置

- `D:\UE5projects\Demo\Demo.uproject`：启用 `PhysicsControl`，重启 UE 后生效。

### 本次已授权实施

- `D:\UE5projects\Demo\Source\Demo\Public\Characters\ZeroEscapeCharacter.h`
- `D:\UE5projects\Demo\Source\Demo\Private\Characters\ZeroEscapeCharacter.cpp`
- `D:\UE5projects\Demo\Source\Demo\Public\Components\Magnetism\ElectromagneticGrabComponent.h`
- `D:\UE5projects\Demo\Source\Demo\Private\Components\Magnetism\ElectromagneticGrabComponent.cpp`
- `D:\UE5projects\Demo\Source\Demo\Public\Components\Magnetism\MagneticObjectComponent.h`
- `D:\UE5projects\Demo\Source\Demo\Private\Components\Magnetism\MagneticObjectComponent.cpp`
- `D:\UE5projects\Demo\Source\Demo\Public\UI\ZeroEscapeHUD.h`
- `D:\UE5projects\Demo\Source\Demo\Private\UI\ZeroEscapeHUD.cpp`
- `D:\UE5projects\Demo\Source\Demo\Public\Actors\Magnetism\MagneticPrototypeProp.h`
- `D:\UE5projects\Demo\Source\Demo\Private\Actors\Magnetism\MagneticPrototypeProp.cpp`
- `D:\UE5projects\Demo\Source\Demo\Public\GameFlow\ZeroEscapePrototypeGameMode.h`
- `D:\UE5projects\Demo\Source\Demo\Private\GameFlow\ZeroEscapePrototypeGameMode.cpp`
- `D:\UE5projects\Demo\Source\Demo\Demo.Build.cs`：仅增加实际需要的 Runtime 依赖；基线不添加 Physics Control 模块。
- `D:\UE5projects\Demo\Config\DefaultEngine.ini`：只设置原型 GameMode；默认地图路径保持不变。
- `/Game/ZeroEscape/Input/`：磁力抓取与投掷 Input Action/Mapping。
- `/Game/ZeroEscape/Characters/BP_ZeroEscapeCharacter`
- `/Game/ZeroEscape/Interaction/Magnetism/BP_MagneticProp`
- `/Game/ZeroEscape/Prototype/L_MagnetismPrototype`：当前 MCP 不支持创建/复制关卡，先由原型 GameMode 在未修改的 `Level0` 中运行时生成测试道具；后续补建独立地图。

## 实施检查点

1. C++角色装配、输入与宽容选取；构建验证。
2. Physics Handle 质心抓取、自由旋转、角阻尼恢复、掉落和投掷；构建与运行验证。
3. 遮挡断开、霰弹式中心准星和独立原型关卡；蓝图编译保存与 PIE 验证。
4. 用户试玩后决策：保留/调参 → Physics Control A/B → 有明确缺口才自研。

## 验证与回退

- 使用不同质量和不规则形状物体，检查偏离准星选取、碰撞旋转、快速转身、门框阻挡、掉落、投掷和连续抓取。
- 对比空闲与持有时的 Game/Physics 帧耗时；组件仅在选取预览或持有期间更新，禁止每帧全局 Actor 搜索。
- 新源码和资产集中在独立目录，不改默认地图；任一检查点失败可移除该步新增文件并恢复 `Demo.Build.cs`。
- Physics Control 插件可单独从 `.uproject` 移除，不影响 Physics Handle 基线。
- 功能状态始终为“待用户验收”，只有用户试玩通过后才能完成。

## 后续扩展门槛

- Physics Handle 仅需调参即可满足：不切换方案。
- 明确需要最大力/扭矩、阻尼比、目标速度或多刚体管理：做 Physics Control A/B。
- 两套 UE 方案都无法满足且有稳定复现、性能数据和明确目标：再实现质量归一化 PD/弹簧控制层。

## 已确认的资源装配去硬编码检查点

- 保留 `Config/DefaultEngine.ini` 中唯一的 GameMode 启动资产路径，但改为 `BP_ZeroEscapePrototypeGameMode`；这是工程装配入口。
- `AZeroEscapePrototypeGameMode` 不再通过项目资产路径查找角色蓝图；原生角色类只作为非空诊断后备，完整输入与磁力资源仍由 GameMode 蓝图默认值配置。
- `AMagneticPrototypeProp` 不再通过项目资产路径查找正式材质；`BP_MagneticProp` 直接配置继承的 `MagneticBody`，并移除重复的 `MagneticMesh`。
- 本检查点不引入 AssetManager、PrimaryDataAsset 或 PCG 资源池；未来大量可选生成物再使用 `TSoftClassPtr` 与受控预加载。
- 顺序依赖：先修改并构建 C++，再由用户在编辑器中装配蓝图属性、编译保存，最后共同 PIE 验证。源码与资产可分别回退。

## 首次试玩反馈后的已确认修订

### 目标与非目标

- 修复交替点击鼠标左右键后角色可能持续移动的问题，并移除第三人称示例 PlayerController 对输入上下文的隐式管理。
- 使用独立的输入 DataAsset 集中装配 Mapping Context 与 Input Action；使用另一份磁力 Tuning DataAsset 集中管理当前 Physics Handle 手感参数，两者不得互相引用。
- 相机先提供 `300 cm` 弹簧臂与右肩偏移的可玩基线，最终构图由用户在 `BP_ZeroEscapeCharacter` 的 `CameraBoom` 上直接调整。
- 本轮只迁移现有磁力参数，不加入左键蓄力、颤动、UI/特效、锚点限速或新物理算法；这些需在基线重新验收后单独确定方案。

### 检查点 A：输入与相机

- `D:\UE5projects\Demo\Source\Demo\Public\Data\Input\ZeroEscapeInputConfig.h`
- `D:\UE5projects\Demo\Source\Demo\Private\Data\Input\ZeroEscapeInputConfig.cpp`
- `D:\UE5projects\Demo\Source\Demo\Public\GameFlow\ZeroEscapePlayerController.h`
- `D:\UE5projects\Demo\Source\Demo\Private\GameFlow\ZeroEscapePlayerController.cpp`
- `D:\UE5projects\Demo\Source\Demo\Public\Characters\ZeroEscapeCharacter.h`
- `D:\UE5projects\Demo\Source\Demo\Private\Characters\ZeroEscapeCharacter.cpp`
- `D:\UE5projects\Demo\Source\Demo\Private\GameFlow\ZeroEscapePrototypeGameMode.cpp`

输入 DataAsset 是输入资源唯一来源；专用 PlayerController 只管理游戏输入模式、鼠标捕获和按键状态清理。角色只负责应用/移除上下文、绑定动作及转发意图。

### 检查点 B：磁力参数迁移

- `D:\UE5projects\Demo\Source\Demo\Public\Data\Magnetism\MagneticGrabTuningData.h`
- `D:\UE5projects\Demo\Source\Demo\Private\Data\Magnetism\MagneticGrabTuningData.cpp`
- `D:\UE5projects\Demo\Source\Demo\Public\Components\Magnetism\ElectromagneticGrabComponent.h`
- `D:\UE5projects\Demo\Source\Demo\Private\Components\Magnetism\ElectromagneticGrabComponent.cpp`
- `D:\UE5projects\Demo\Source\Demo\Public\Components\Magnetism\MagneticObjectComponent.h`
- `D:\UE5projects\Demo\Source\Demo\Private\Components\Magnetism\MagneticObjectComponent.cpp`

磁力 Tuning DataAsset 是玩家全局抓取手感的唯一来源；`MagneticObjectComponent` 只保留单个物体的磁性资格、选取优先级和投掷倍率。资产缺失或参数非法时明确报错并停用对应功能，不使用构造期路径或组件内第二套参数兜底。

### 验证与人工装配

1. 两个检查点分别完成 `DemoEditor Win64 Development` 构建和 `git diff --check`。
2. C++ 类型构建通过后，由用户在 `/Game/ZeroEscape/` 创建并填写两份 DataAsset，再分别赋给角色和磁力组件。
3. 用户调整并保存角色 Mesh、AnimBP 与 CameraBoom，随后共同 PIE 验证移动卡键、输入取消、宽容选取、自然掉落、转身松手速度和投掷基线。
4. 未完成上述实际试玩前，任务保持“待用户验收”。
