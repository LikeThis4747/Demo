# 一局游戏构成 —— 全局方案（粗颗粒草稿）

> 状态：讨论稿。只描述"大概要做什么"，不锁死实现细节；边做边按需求调整。
> Owner：本对话（完成"一局游戏构成的要件"）。Code review 由另一对话负责。
> 最后更新：2026-07-30

## 0. 目标一句话

从主菜单选 Seed + 难度进入 PCG 关卡，玩一局（追逃 + 受击），分出胜负后结算，
能"下一把 / 重开 / 回主菜单"，过程中能暂停。全程单机。

## 1. 已锁定的方向决策（不轻易推翻）

- **关卡分离**：`Level0` 永久做调试场（不动）；正式游戏用新建的干净关卡。
- **重开方式**：统一走 `OpenLevel` 整关重载，不做原地清理重生成。理由：世界重建天然归零，省清理代码。
- **参数传递**：用 `UGameInstance` 子类跨关卡持有 Seed + 难度（复用现成的 `FZeroEscapeGenerationRequest`）。
- **删除 RoundFlow**：其职责错位（关卡 Actor 却干开局编排 + 局状态）。拆分为 GameMode(开局编排) + GameState(局状态/胜负)。等新流程验证后再删。
- **GameMode 新建不改造**：新建正式 `ZeroEscapeGameMode` 绑正式关卡；`PrototypeGameMode` 留给 Level0。
- **环境光**：PCG 关卡不放静态环境光（设计需求）；运行时只生成"依赖布局"的东西（如跟走廊走的顶灯）。
- **出生位置**：追猎者在 Start，玩家离两格出生防突脸；第一版怎么简单怎么来（直线距离）。
- **分工**：C++ 写逻辑，蓝图只做 UI 布局/资源装配。

## 2. 关卡与核心类（粗略蓝图）

关卡：
- `L_MainMenu`：纯 UI，主菜单。
- `L_Game`：正式游戏关卡，放 PCG Generator + 玩家出生；无静态环境光。
- `Level0`：调试场，保留不动。
- `L_PCG_RuntimeTest`：旧 PCG 测试图，保留或废弃（新建 L_Game 后基本不用）。

核心类（按职责，具体成员边做边定）：
- `UZeroEscapeGameInstance`：跨关卡存本局请求参数（Seed/难度）。
- `AZeroEscapeGameMode`（正式）：开局编排——读参数→驱动 PCG 生成→摆玩家+追猎者。
- `AZeroEscapeGameState`：本局状态机（进行中/胜/负），承接胜负判定入口。
- `AMainMenuGameMode`：主菜单专用，进 UI-only 输入、创建主菜单 Widget。
- UI 基类（C++ 逻辑 + 蓝图布局）：主菜单 / 结算 / 暂停。
- 复用现有：`AZeroEscapePlayerController`(空壳，后续加暂停输入)、`AZeroEscapeHUD`(准星)、
  `AZeroEscapeRuntimeLevelGenerator`(不动)、`UHealthComponent`(加死亡广播)。

## 3. 分阶段推进（顺序按用户要求）

### 阶段一：主菜单 → 进入游戏（先做）
做出：主菜单选 Seed/难度 → 存 GameInstance → OpenLevel(L_Game) → 正式 GameMode 读参数
驱动 PCG 生成 → 摆好玩家和追猎者。退出按钮能关游戏。
产出可玩验证：从菜单进游戏，PCG 按所选参数生成，人物就位。

### 阶段二：重开 / 局流转 + 暂停菜单
做出：胜负后的结算入口（先不接真实胜负，用占位触发亦可）+ 结算菜单
[下一把/重开/回主菜单]，都走 OpenLevel；ESC 暂停菜单[继续/改参重开/回主菜单]。
把 RoundFlow 正式退役。

### 阶段三：伤害 / 胜负结算（最后，逻辑较简单）
做出：HealthComponent 归零→判负；到达 Exit→判胜；由 GameState 统一裁决并触发阶段二的结算 UI。

## 4. 暂不决定 / 边做边定（避免过早固化）

- "两格"是直线距离还是沿路径：先直线，手感不行再说。
- 难度到底影响什么（目前只影响 WFC 权重）：做胜负时再定要不要挂更多。
- 结算/暂停 UI 的具体字段与美术：做到阶段二再定。
- L_PCG_RuntimeTest 是否删除：阶段一验证后再决定。
- GameState vs 直接在 GameMode 里判胜负：阶段三落地时再定要不要独立 GameState。

## 5. 交接备忘

- 每完成一个阶段，更新本文件"进度"并同步 memory-bank。
- 落盘前先在对话里展示代码给用户过目，用户允许后再写文件。
- 涉及 UE 资产改动先用 MCP 核实真实状态，不凭 C++ 猜。

## 6. 进度

### 阶段一：主菜单进游戏（进行中）
- [x] C++ 第1步：`UZeroEscapeGameInstance`（存 Seed/难度）— 已落盘编译通过
- [x] C++ 第2步：`AZeroEscapeGameMode`（读参数生成PCG+摆人）— 已落盘编译通过
- [x] C++ 第3步：`AMainMenuGameMode` + `UMainMenuWidget` — 已落盘编译通过
  - 注：修过 Unity build 日志类别重名冲突（Widget 用 LogZeroEscapeMainMenuWidget，GameMode 用 LogZeroEscapeMainMenu）
  - 注：Build.cs 加了 UMG 模块依赖
- [x] 蓝图第4步a：4个蓝图已建（BP_ZeroEscapeGameInstance / BP_MainMenuGameMode / BP_ZeroEscapeGameMode / WBP_MainMenu），父类均已用 set_parent_class 修正为自定义 C++ 类
- [x] 蓝图第4步b：4蓝图父类全部正确（用户手动改+MCP官方get_parent核实）；Class Defaults 3字段已设并核实（PursuerClass/MainMenuWidgetClass/GameLevelName=L_Game）
- [x] 关卡第4步c：L_MainMenu（空图）+ L_Game（Generator/Populator/PlayerStart/NavMesh 已搬，无环境光）已建并核实
- [x] 配置第4步d：GameDefaultMap=L_MainMenu；EditorStartupMap=Level0；GlobalDefaultGameMode=BP_ZeroEscapeGameMode；GameInstanceClass=BP_ZeroEscapeGameInstance；L_Game WorldSettings=BP_ZeroEscapeGameMode（已核实）；L_MainMenu WorldSettings=BP_MainMenuGameMode（MCP官方设置）
- [x] WBP_MainMenu 搭建：C++ 改 BindWidget 模式（NativeConstruct 绑全部事件，零蓝图连线）；官方UMG工具搭28控件树（标题/Seed行/三按钮/设置面板含难度三键+返回），全部编译通过
  - 注：Build.cs 加了 Slate/SlateCore（OnTextCommitted 委托签名用 ETextCommit）
  - 注：C++ 难度高亮/设置面板显隐/随机Seed回填 全在 MainMenuWidget.cpp
- [ ] PIE 验证：主菜单选参数 → 进游戏 → PCG生成 → 人物就位 ← 下一步

### 阶段二：重开/流转/暂停（未开始）
### 阶段三：伤害/胜负结算（未开始）

### 关键资产路径备忘（MCP 已核实）
- Generator 蓝图：`/Game/ZeroEscape/Generation/BP_ZeroEscapeRuntimeLevelGenerator`
- 生成配置 DA：`/Game/ZeroEscape/Generation/Data/DA_LevelGenerationProfile`
- 表现配置 DA：`/Game/ZeroEscape/Generation/Presentation/DA_Presentation_SciFiHydroLab`
- 布点器蓝图：`/Game/ZeroEscape/Generation/Population/BP_GameplayPopulator`（配 `DA_Population_Default`）
- 追猎者蓝图：`/Game/ZeroEscape/Enemies/BP_Pursuer`
- Generator TriggerMode 默认 ExplicitOnly（不会自动跑，靠 GameMode 触发，无双重生成风险）
- L_PCG_RuntimeTest 实际 Actor：Generator + RoundFlow(不搬) + PlayerStart + NavMeshBoundsVolume + SkyLight_1 + GameplayPopulator + 2个地板占位(不搬)
