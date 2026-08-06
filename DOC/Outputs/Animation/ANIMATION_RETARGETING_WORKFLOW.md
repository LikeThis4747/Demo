# 动画重定向工作流（IK Retarget）

> 状态：2026-08-06 从现有资产反推整理，作为项目动画重定向的标准化流程。
> 适用：Mixamo / 第三方人形动画导入到项目两套角色骨架（追猎者、玩家）时的标准做法。
> 依据：项目内已存在的 `IK_XBot`、`IK_SciFi08`、`RTG_XBot` 与重定向产物 `MM_SciFi_Character_08_*` 的官方 MCP 实测配置。

## 1. 为什么需要这份文档

之前追猎者反应动画（`standing_react_large_*`）做过一次 Mixamo → 追猎者的重定向，但当时没有留下文字记录。本次找回并固化流程，避免下次（如倒地起身动画）重复摸索。

## 2. 当前项目资产全景（官方 MCP 实测，2026-08-06）

### 2.1 三套骨架关系

| 角色 | Mesh | 骨架 | 与 Mixamo 关系 |
|---|---|---|---|
| 源（Mixamo XBot） | `X_Bot` | `X_Bot_Skeleton`（Mixamo 命名：Hips/Spine/Spine1/…/RightUpLeg…） | 第三方 Mixamo 骨架 |
| 追猎者 | `SK_Sci_Fi_Character_08_Full_01` | `UE4_Mannequin_Skeleton` | 第三方素材自带，UE4 Mannequin 系 |
| 玩家 | `SK_SciFITrooper_Man_03` | `CH_SciFiTrooper_Man_03_Skeleton` | UE4 Mannequin 系变体 |

关键事实：
- **追猎者和玩家都是 UE4 Mannequin 系**（主干骨骼名完全一致，玩家仅多 8 根 `*_Muscle` 辅助骨骼）。
- **官方 Mannequin 的 `MM_Death_*` 挂在 Manny 新骨架（`SK_Mannequin`）上，与两套角色骨架不同，不能直接用**。
- 一套 UE4 Mannequin 系动画经重定向后可同时供两个角色使用（见 §6 说明）。

### 2.2 现有重定向三件套（都是可复用的）

| 资产 | 路径 | 实测配置 |
|---|---|---|
| 源 IK Rig | `/Game/MeleeAnimation/IK_XBot` | previewSkeletalMesh = `X_Bot`（Mixamo 骨架） |
| 目标 IK Rig | `/Game/MeleeAnimation/IK_SciFi08` | previewSkeletalMesh = `SK_Sci_Fi_Character_08_Full_01` |
| Retargeter | `/Game/MeleeAnimation/RTG_XBot` | source = `IK_XBot`，target = `IK_SciFi08`；sourcePreview = `X_Bot`，targetPreview = `SK_Sci_Fi_Character_08_Full_01` |

### 2.3 上次重定向的产物命名规范

- 原始 FBX 导入 `Content/MeleeAnimation/`（挂在 `X_Bot_Skeleton`），如 `standing_react_large_from_left`。
- 重定向后产物移到 `/Game/ZeroEscape/Enemies/Animation/`，命名 `MM_SciFi_Character_08_<原名>`，挂在 **`UE4_Mannequin_Skeleton`**。
- 蒙太奇：`MM_SciFi_Character_08_<原名>_Montage`，供追猎者 AnimGraph 的 DefaultSlot 使用。

## 3. 标准重定向流程（Mixamo → 项目角色）

### 步骤 0：Mixamo 下载设置（硬要求）

1. 在 Mixamo 选中动画后，右侧 **Character 必须选 XBot**（与现有 `IK_XBot` 源一致）。
2. **Format 选 FBX Binary**。
3. **Skin 选 "Without Skin"**（动画不需要网格，UE 自己绑骨骼）。
4. **勾选 In Place**（倒地/起身类原地动画必须勾；根位移动画按需）。
5. 帧率保持默认（与项目一致）。
6. 下载动画文件（如 `GetUp_A.fbx` / `GetUp_B.fbx`）。

### 步骤 1：导入 FBX 到 MeleeAnimation

1. 把 FBX 拖入 `Content/MeleeAnimation/`（或新建 `MeleeAnimation/Mixamo/` 子目录放原始文件）。
2. 导入对话框确认：
   - **Skeleton：选 `X_Bot_Skeleton`**（不新建骨架）。
   - 勾选 Import Animation。
   - 取消 Import Mesh（纯动画导入）——若 Mixamo 已带网格可忽略网格。
3. 导入后 AnimSequence 挂在 `X_Bot_Skeleton` 下，位置即 `MeleeAnimation/`。

### 步骤 2：创建/确认 IK Rig

- **源 Rig（已有 `IK_XBot`）**：若 Mixamo 动画仍用 XBot，直接复用，无需重做。
  - 若换新 Mixamo 角色，需新建源 Rig：右键 `X_Bot` 网格 → Create IK Rig，自动生成骨骼链（Hips→Spine→…→Head、四肢链），命名如 `IK_<角色>`。
- **目标 Rig**：
  - 追猎者：复用 `IK_SciFi08`。
  - 玩家：**需新建** `IK_SciFiTrooper`（右键 `SK_SciFITrooper_Man_03` → Create IK Rig），因为现有目标 Rig 绑的是追猎者网格。

### 步骤 3：创建/确认 Retargeter

- **追猎者**：复用 `RTG_XBot`（已配置 source=`IK_XBot` → target=`IK_SciFi08`）。
- **玩家**：新建 Retargeter，source=`IK_XBot`，target=新建的 `IK_SciFiTrooper`，命名 `RTG_XBot_To_SciFiTrooper`。

创建 Retargeter：`右键新导入动画或任意网格 → Create IK Retargeter`（或在 Asset 面板新建），在窗口中：
1. Set Source IK Rig = `IK_XBot`；Set Target IK Rig = 目标 Rig。
2. 视口核对骨骼链对齐，自动生成的链映射一般够用；检查手臂/腿方向链是否交叉。

### 步骤 4：执行重定向生成副本

对每个需要重定向的 AnimSequence：

1. 在 Content Browser 选中源动画（可多选，一次批量处理）。
2. 右键 → **重定向动画资产（Retarget Anim Assets）**。
3. 在「重定向动画（Retarget Animations）」对话框按下表填写。

**中英对照与填写表（2026-08-06 中文版 UE5.8 实测）**

| 中文界面 | 英文原名 | 填什么（追猎者示例） |
|---|---|---|
| 源 → 源骨骼网格体 | Source → Source Skeletal Mesh | `X_Bot` |
| 目标 → 目标骨骼网格体 | Target → Target Skeletal Mesh | `SK_Sci_Fi_Character_08_Full_01` |
| 重定向器 → 自动生成重定向器 | Retargeter → Auto Generate Retargeter | **取消勾选** ⚠️ |
| 重定向器 → 重定向资产 | Retargeter → Retarget Asset | `RTG_XBot` |
| 前缀 | Prefix | `MM_SciFi_Character_08_` |
| 后缀 / 搜索替换 | Suffix / Search & Replace | 留空 |
| 文件夹 | Folder | `/Game/ZeroEscape/Enemies/Animation` |
| 底部按钮 | Retarget | 执行 |

**关键点1：必须取消勾选「自动生成重定向器」。** 勾着它UE 会临时生成一个新 retargeter，骨骼链映射是自动猜的；取消后「重定向资产」字段才解锁，才能选已调好并验证过的 `RTG_XBot`。复用它能保证新动画与既有 `MM_SciFi_Character_08_standing_*` 的链映射**完全一致**，避免"这批动画手臂对、那批手臂歪"。

**关键点 2：善用「前缀」+「文件夹」字段。** 直接填对就一步到位生成规范命名并落到目标目录，不需要事后再重命名和移动资产（省掉重命名可能触发的 redirector 问题）。

**关键点 3：执行完必须 Save All并查磁盘。** 见步骤 5。

### 步骤 5：验证（必须做，避免"看起来能用实际错位"）

**5.0 落盘验证（最先做，2026-08-06 实际踩坑）**

重定向产物**不会自动保存**。执行完对话框后产物只在内存中处于 dirty 状态，Content Browser 里已能看到图标，但磁盘上没有 `.uasset`。

1. **Save All**（Ctrl+Shift+S 或 文件 → 全部保存）。
2. 用文件系统查磁盘确认 `.uasset` 真实存在（如 `Content/ZeroEscape/Enemies/Animation/`），**不能只看 Content Browser 有图标就算完成**。

> 踩坑记录：2026-08-06 做 GetUp 起身动画时，编辑器日志已打出产物完整路径（`LogAnimationCompression: EnsureDependenciesAreLoaded ... MM_SciFi_Character_08_Getting_Up_FaceUp`），说明对象已创建，但磁盘查无此文件；Save All 后才落盘。

**5.1 其余验证项**

用官方 MCP 逐项核对：

1. 重定向产物依赖的骨架必须是目标骨架（`get_dependencies` 查 AnimSequence 依赖，确认是追猎者骨架或 `CH_SciFiTrooper_Man_03_Skeleton`）。
2. 在 Skeleton 上打开动画预览，检查：脚掌不穿地、手指自然、骨盆在倒地/起身关键帧符合物理落点高度。
3. 播放时**骨盆/root 不漂移**（In-Place 动画若出现平移说明 Mixamo 没勾 In Place 或根运动残留）。

**5.2 可忽略的无害警告**

```
LogTemp: Warning: No animation curves found for retargeting in Anim Sequence: <名字>
```

Mixamo 动画只含骨骼变换，不含 morph target / animation curve 数据，此警告不影响骨骼动画重定向结果。

### 步骤 6：接入游戏

- 追猎者：建蒙太奇 `MM_SciFi_Character_08_<原名>_Montage`，Slot 用 `DefaultSlot`；从 ABP 的 Downed → Recovering 状态触发。
- 玩家：同上，Slot 用玩家 ABP 的 `DefaultSlot`。

## 4. 本次任务：两套倒地起身动画具体套用（实际执行结果）

1. Mixamo 下载两个 Get Up 动画（仰面 + 俯面），按步骤 0 设置（XBot / Without Skin / In Place）。
2. 导入到 `MeleeAnimation/`，挂 `X_Bot_Skeleton`：`Getting_Up_FaceUp`、`Getting_Up_FaceDown`。
3. **重定向前**先处理源动画：`rateScale=2`（原 8.3s/8.6s 太慢像慢镜头）+ 手动裁掉结尾的拳击idle 姿势。副本自动继承。
4. 追猎者侧复用现有三件套，**完全不用新建任何 IK Rig / Retargeter**。已完成并落盘：
   - `/Game/ZeroEscape/Enemies/Animation/MM_SciFi_Character_08_Getting_Up_FaceUp`
   - `/Game/ZeroEscape/Enemies/Animation/MM_SciFi_Character_08_Getting_Up_FaceDown`
5. 玩家侧待做，见 §6 二选一，产物命名 `MM_Player_Getting_Up_FaceUp/FaceDown` → `/Game/ZeroEscape/Characters/Animation/`。

## 5. 常见坑与规避

| 坑 | 规避 |
|---|---|
| Mixamo 角色选错（不是 XBot） | 源 Rig 与源骨架不匹配，链全红；务必选 XBot |
| 忘记 In Place | 起身动画 root 漂移，物理落点与动画起点对不上 |
| 目标骨架选错 | 产物挂错骨架，角色上播放报错或错位；用官方 MCP 回读依赖验证 |
| 覆盖源动画 | 用新建产物（前缀/文件夹字段），保留 Mixamo 原始文件 |
| 玩家/追猎者动画混淆 | 命名区分前缀（`MM_SciFi_Character_08_` vs `MM_Player_`），目录分离 |
| **忘了保存，以为做完了** | 重定向产物不自动保存。必须 Save All + 查磁盘 `.uasset`，不能只看Content Browser 图标（见步骤 5.0） |
| **勾着「自动生成重定向器」直接执行** | 会用自动猜的链映射而非已验证的 `RTG_XBot`，导致同一角色不同批次动画映射不一致。必须取消勾选后手选Retargeter |
| **凭对话框里出现的资产名推断角色归属** | 2026-08-06 AI 因此把追猎者（`Sci_Fi_Character_08`）误判为玩家，连带否定了本可复用的 `RTG_XBot`。必须用目录结构（`ZeroEscape/Enemies/` vs `Characters/`）或 MCP 回读依赖来确认归属 |
| 源动画需调速/裁剪 | **在重定向之前**改源动画（`rateScale`、裁剪），副本自动继承，只需处理一次 |

## 6. 换到第二套骨架（玩家侧）的做法

### 6.1 先确认：UE5.8 右键菜单里没有「重定向到另一个骨骼」

2026-08-06 在本项目 UE5.8 实测，AnimSequence 右键菜单**不存在**旧版的「Retarget to Another Skeleton / 重定向到另一个骨骼」独立项。实际可用的相关项是：

- 「查找骨骼」、**「替换骨架」（Replace Skeleton）**
- 「动画修改器」、「重定向动画」（即 §3 步骤 4 那个对话框）
- 「资产操作 >」

### 6.2 先做骨架对比（决定走哪条路的依据）

用官方 MCP `SkeletalMeshTools.get_bone_names` + `get_bounds` 对比两套骨架，实测结果：

| | 追猎者 `UE4_Mannequin_Skeleton` | 玩家 `CH_SciFiTrooper_Man_03_Skeleton` |
|---|---|---|
| 骨骼数 | 68 | 76 |
| 差异 | — | 多 8 根 Muscle 辅助骨骼：`l/r_upperarm_Muscle`、`l/r_forearm_Muscle`、`l/r_thigh_Muscle`、`l/r_calf_Muscle` |
| 骨骼名覆盖 | — | 追猎者 68 根**全部存在**于玩家骨架，仅层级排列顺序不同 |
| boxExtent | (69.22, 21.21, 92.04) | (68.08, 21.87, 92.06) |
| sphereRadius | 117.10 | 116.57 |

→骨骼名 100% 一一对应，整体比例差异 <1.5%。**骨骼名覆盖率与比例是选路径的前提，务必先量再动手。**

### 6.3 三条路径（按推荐顺序）

- **路径 C（最快，本项目已实测成功）**：复制追猎者副本（Ctrl+D，产物自动带 `*_1` 后缀）→ 重命名 → 右键 **「替换骨架」** → 选目标骨架。仅在骨骼名高度匹配时可行。
  > ⚠️ **必须先复制再替换。**「替换骨架」是**原地修改资产**、不生成副本，直接对已有副本操作会毁掉它。
  > ✅ **实测结果**：骨骼名 100% 匹配 + 比例差 <1.5% 时，替换后预览姿态正常，无需再走 IK Retargeter（本项目玩家侧两套起身动画已用此路径完成）。替换后需重命名（UE 自动加 `*_1`）并移到目标目录。
- **路径 D（次选）**：用「重定向动画」对话框，源骨骼网格体=追猎者 mesh，目标=玩家 mesh，**勾选**「自动生成重定向器」。骨骼名标准一致时自动链映射准确率高，省掉手建 IK Rig。
- **路径 A（最规范，兜底）**：新建目标 Rig `IK_SciFiTrooper`（右键 `SK_SciFITrooper_Man_03` → Create IK Rig）+ Retargeter `RTG_XBot_To_SciFiTrooper`（source=`IK_XBot`，target=`IK_SciFiTrooper`），从**源动画**重定向。唯一不经过"副本的副本"的路径，无误差累积，后续玩家动画都能复用。若后续批量做玩家动画，可考虑用它建正规管线。

### 6.4 风险与判据

骨架参考姿势（reference pose）是否一致**无法用官方 MCP 验证**（`SkeletalMeshTools` 没有 `get_bone_transform` 工具）。

判据（本项目已实测）：若替换骨架后预览出现姿态扭曲、手脚朝向错误，即说明参考姿势不同，**退回路径 A**（IK Retargeter 会做姿势对齐，能吸收参考姿势差异；「替换骨架」不会）。本项目追猎者/玩家两套骨架同源（均为 UE4 Mannequin 系），替换后预览正常，参考姿势风险实际不存在。

路径 C/D 都是"副本的副本"，会继承第一次重定向的偏差。若追猎者副本本身质量高则无妨；追求干净就走路径 A。

### 6.5 命名与归位规范（玩家侧）

- 命名：`MM_Player_<原名>`（如 `MM_Player_Getting_Up_FaceUp`），与敌人侧 `MM_SciFi_Character_08_` 前缀区分。
- 目录：`/Game/ZeroEscape/Characters/Animation/`（与敌人侧 `Enemies/Animation/` 对称；`Characters/` 下已有主角蓝图，动画相邻存放，后续玩家动画统一进这里）。

## 7. 官方 MCP 调用要点（2026-08-06 实测）

用官方 MCP（`ue58-official-mcp`，HTTP 8000 `/mcp`）做动画/骨架验证时：

- 必须走统一入口 **`call_tool`**，参数 `{toolset_name, tool_name, arguments}`。直接把全限定名（如 `editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_skeleton`）当工具名调用会报 `Tool not found`。
- 资产引用 `refPath` **必须是完整对象路径** `/Game/Path/Asset.Asset`；只给包路径 `/Game/Path/Asset` 会报 `is not a valid object path`。
- `ObjectTools.get_properties` 的参数名是 **`instance` + `properties`**（不是 `object` + `property_names`）。
- 常用验证组合：
  - `SkeletalMeshTools.get_skeleton` — 查 mesh 绑的骨架
  - `SkeletalMeshTools.get_bone_names` — 骨骼名列表（对比两套骨架用）
  - `SkeletalMeshTools.get_bounds` — 比例对比
  - `ObjectTools.get_properties(instance, ["skeleton","rateScale","bEnableRootMotion"])` — 验证重定向产物

> ⚠️ **判断 MCP 可用性必须实际发起一次调用。** 2026-08-06 AI 两次误报"官方 MCP 不可用"，仅因系统给的工具清单里没列出它；实测端口 8000 监听正常、`list_toolsets` 返回 20+ 工具集，一直可用。清单可能不完整，不能当依据。

## 8. 更新记录

- 2026-08-06（创建）：基于官方 MCP 实测的 `IK_XBot` / `IK_SciFi08` / `RTG_XBot` 配置与 `MM_SciFi_Character_08_*` 产物反推整理；骨架全景、命名规范、标准流程、玩家省力方案一并固化。
- 2026-08-06（修订1）：GetUp 起身动画实操后补充——① 步骤 4 增加中文版 UE 界面中英对照填写表与三个关键点（取消自动生成重定向器 / 善用前缀+文件夹字段 / 执行完必须保存）；② 新增步骤 5.0 落盘验证（重定向产物不自动保存，实际踩坑）与 5.2 无害警告说明；③ §5 补充 4 条坑；④ §6 玩家侧方案 B 标注为待验证。
- 2026-08-06（修订 2）：① §6 重写——UE5.8 实测右键菜单已无「重定向到另一个骨骼」，改为「替换骨架」等项；补入两套骨架 `get_bone_names`/`get_bounds` 实测对比数据（骨骼名 100% 覆盖、比例差 <1.5%）；三条路径 C/D/A 按推荐顺序重排；明确「替换骨架」原地改资产必须先复制。② 新增 §7 官方 MCP 调用要点（`call_tool` 入口、完整对象路径、`instance`/`properties` 参数名、MCP 可用性必须实调验证）。
- 2026-08-06（修订 3）：① §6.3 路径 C 标注**已实测成功**（玩家侧起身动画经「复制+替换骨架」完成，预览正常，参考姿势风险实际不存在）；② 新增 §6.5 玩家侧命名（`MM_Player_<原名>`）与归位规范（`/Game/ZeroEscape/Characters/Animation/`）。
- 2026-08-06（修订 4）：全流程收尾。玩家侧最终产物 `Characters/Animation/MM_Player_Getting_Up_FaceUp/FaceDown` 重命名+移动完成，官方 MCP 回读确认骨架为 `CH_SciFiTrooper_Man_03_Skeleton`；本次起身动画任务（两套骨架共 4 个资产）全部完成。§4 的本次任务清单即为可复用参考样例。
