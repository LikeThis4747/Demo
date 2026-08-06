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

1. 在 Content Browser 选中源动画（如 `MeleeAnimation/GetUp_A`）。
2. 右键 → **Retarget Anim Assets**。
3. 选择 Retargeter（`RTG_XBot` 或新建的玩家 Retargeter）。
4. Target Skeleton 选目标骨架（追猎者 `UE4_Mannequin_Skeleton` / 玩家 `CH_SciFiTrooper_Man_03_Skeleton`）。
5. 勾选 **Create New Assets**（不覆盖源），命名按 §2.3 规范（`MM_SciFi_Character_08_GetUp_A`）。
6. 点击 Create Retargeter/Apply → 产物生成，**移动到目标目录**：
   - 追猎者：`/Game/ZeroEscape/Enemies/Animation/`
   - 玩家：`/Game/ZeroEscape/Characters/Animation/`（目录不存在则新建）

### 步骤 5：验证（必须做，避免"看起来能用实际错位"）

用官方 MCP 逐项核对：

1. 重定向产物依赖的骨架必须是目标骨架（`get_dependencies` 查 AnimSequence 依赖，确认是 `UE4_Mannequin_Skeleton` 或 `CH_SciFiTrooper_Man_03_Skeleton`）。
2. 在 Skeleton 上打开动画预览，检查：脚掌不穿地、手指自然、骨盆在倒地/起身关键帧符合物理落点高度。
3. 播放时**骨盆/root 不漂移**（In-Place 动画若出现平移说明 Mixamo 没勾 In Place 或根运动残留）。

### 步骤 6：接入游戏

- 追猎者：建蒙太奇 `MM_SciFi_Character_08_GetUp_*_Montage`，Slot 用 `DefaultSlot`；从 ABP 的 Downed → Recovering 状态触发。
- 玩家：同上，Slot 用玩家 ABP 的 `DefaultSlot`。

## 4. 本次任务：两套倒地起身动画具体套用

1. Mixamo 下载两个 Get Up 动画（仰面 + 俯面），全部按步骤 0 设置（XBot / Without Skin / In Place）。
2. 导入 → 步骤 2/3 追猎者复用现有三件套；玩家新建目标 Rig + Retargeter。
3. 重定向出 4 个副本：追猎者 2 个 + 玩家 2 个（命名 `MM_SciFi_Character_08_GetUp_Front/Back`、`MM_Player_GetUp_Front/Back`）。
4. 追猎者侧 **完全不用重建任何 IK Rig / Retargeter**；玩家侧只需新建 1 个目标 Rig + 1 个 Retargeter（或见 §6 更省的做法）。

## 5. 常见坑与规避

| 坑 | 规避 |
|---|---|
| Mixamo 角色选错（不是 XBot） | 源 Rig 与源骨架不匹配，链全红；务必选 XBot |
| 忘记 In Place | 起身动画 root 漂移，物理落点与动画起点对不上 |
| 目标骨架选错 | 产物挂错骨架，角色上播放报错或错位；用官方 MCP 回读依赖验证 |
| 覆盖源动画 | 用 Create New Assets，保留 Mixamo 原始文件 |
| 玩家/追猎者动画混淆 | 命名区分前缀（`MM_SciFi_Character_08_` vs `MM_Player_`），目录分离 |
| 玩家需要重做整套 Rig | 不必：玩家与追猎者同为 UE4 Mannequin 系，骨骼名一致，可直接用"Retarget to Another Skeleton"或新建轻量 Rig |

## 6. 玩家侧更省的做法（二选一）

- **方案 A（推荐，规范）**：新建 `IK_SciFiTrooper` 目标 Rig + `RTG_XBot_To_SciFiTrooper`，与追猎者完全对称，后续所有动画都走它。
- **方案 B（最快）**：直接对追猎者重定向副本 `MM_SciFi_Character_08_GetUp_*` 右键 → **Retarget to Another Skeleton**，目标选 `CH_SciFiTrooper_Man_03_Skeleton`（骨骼名匹配，缺失的 Muscle 骨骼自动忽略），一次生成玩家副本。无需新建任何资产。

## 7. 更新记录

- 2026-08-06：创建。基于官方 MCP 实测的 `IK_XBot` / `IK_SciFi08` / `RTG_XBot` 配置与 `MM_SciFi_Character_08_*` 产物反推整理；骨架全景、命名规范、标准流程、玩家省力方案一并固化。
