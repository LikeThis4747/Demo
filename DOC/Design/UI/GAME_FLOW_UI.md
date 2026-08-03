# 局流程与 UI 系统

> 状态：已落地并验收（2026-08-03）。本文是正式参考文档；历史规划稿留在 `claude/plans/`（GAME_LOOP_PLAN.md、PAUSE_MENU_PLAN.md、PAUSE_MENU_CHANGES.md），仅供追溯。
> 范围：主菜单 → 进入 PCG 关卡 → 胜负判定 → 结算 / 暂停 → 下一把 / 重开 / 选择关卡 / 回主菜单的完整单机闭环。

## 1. 闭环总览

```
L_MainMenu（WBP_MainMenu：种子+难度+开始/退出）
   │ OpenLevel
   ▼
L_Game（BP_ZeroEscapeGameMode：读 GameInstance 参数 → 驱动 PCG → 摆玩家/追猎者/Exit）
   │ 游戏中 ESC ──────────────► WBP_PauseMenu［继续/选择关卡/返回主菜单］
   │
   ├─ 玩家进入 Exit 触发器 ──► GameState.SetRoundWon ──┐
   ├─ 玩家 Health 归零 ──────► GameState.SetRoundLost ─┤
   ▼                                                    ▼
                              GameMode 订阅 OnRoundStateChanged：暂停游戏 + UI-only 输入
                              + 弹 WBP_ResultMenu［下一把(胜)/重开(负)/选择关卡/返回主菜单］
```

- 局状态唯一真相源是 `AZeroEscapeGameState`（`InProgress/Won/Lost`），`TransitionTo` 只允许 `InProgress` → 终态，转换时广播 `OnRoundStateChanged`；日志关键字 `ZE_ROUND_RESULT result=Win/Lost`。
- 重开/下一把/选择关卡统一走 `OpenLevel` 整关重载，不做原地清理重生成；参数（种子/难度）由 `UZeroEscapeGameInstance` 跨关卡持有。
- 快速路径约定：**下一把 = 新随机种子 + 沿用难度**；**重开 = 同种子 + 同难度**。

## 2. C++ 类与分工

分工原则：C++ 写全部逻辑（`NativeConstruct` 里绑事件，零蓝图连线），蓝图只做控件布局与资源装配。所有界面控件用 `BindWidget` 绑定，WBP 中控件命名必须与 C++ 成员完全一致。

| 类 | 职责 |
|---|---|
| `UZeroEscapeGameInstance` | 跨关卡持有本局 `FZeroEscapeGenerationRequest`（种子/难度） |
| `AMainMenuGameMode` | 主菜单关卡专用：UI-only 输入、创建主菜单 Widget |
| `AZeroEscapeGameMode` | 开局编排；`PlaceExit` 放出口；绑 `OnExitReached`/`OnHealthDepleted` 转发到 GameState；订阅 `OnRoundStateChanged` 弹结算界面并暂停 |
| `AZeroEscapeGameState` | 局状态机 + `OnRoundStateChanged` 广播 |
| `AZeroEscapeExitVolume` | 出口 Actor（球形触发器 + 占位 Mesh），`Activate(Transform)` 后启用，玩家首次进入广播 `OnExitReached` |
| `AZeroEscapePlayerController` | `SetupInputComponent` 绑 `IA_Pause` → 弹/管暂停菜单；切输入模式 |
| `UHealthComponent` | 生命归零时广播 `OnHealthDepleted`（供判负） |
| `UMainMenuWidget` | 主菜单：种子输入/随机/难度三键/开始/退出 |
| `UResultMenuWidget` | 结算界面：`ShowResult(bWon)`；主层［主按钮(下一把/重开)/选择关卡/返回主菜单］，内嵌 `ULevelSetupWidget`；失败标题固定为"失败" |
| `UPauseMenuWidget` | 暂停菜单：主层［继续/选择关卡/返回主菜单］，内嵌 `ULevelSetupWidget`；`NativeOnKeyDown` 捕获 Esc = 继续 |
| `ULevelSetupWidget` | **共用"选择关卡"组件**：种子输入/随机/难度三键/开始/返回；`StartLevelWith(Seed, Difficulty)` = 写 GameInstance + 取消暂停 + OpenLevel；`OnBackRequested` 委托由宿主决定返回目标 |

## 3. 关键机制

### 3.1 胜负链

- 胜利：`AZeroEscapeExitVolume.OnExitReached` → GameMode `HandleExitReached` → `GameState.SetRoundWon`。
- 失败：`UHealthComponent.OnHealthDepleted` → GameMode `HandlePlayerDeath` → `GameState.SetRoundLost`。
- 结算：GameMode `HandleRoundStateChanged` → `SetGamePaused(true)` + UI-only 输入 + 显示鼠标 + 创建 `ResultMenuWidgetClass` 并 `ShowResult`。

### 3.2 暂停 ESC 双路径

- **打开**：游戏输入态，EnhancedInput 生效——`IMC_ZeroEscape` 中 `Esc → IA_Pause`，`AZeroEscapePlayerController::HandlePausePressed` 弹暂停菜单（已存在或已暂停则忽略）。
- **关闭**：暂停后是 UI-only 输入，EnhancedInput 收不到键——由 `UPauseMenuWidget::NativeOnKeyDown` 捕获 Esc，等价"继续"。

### 3.3 LevelSetup 复用

"选择关卡"被结算界面和暂停菜单两个真实消费者使用后才提炼成 `ULevelSetupWidget`（遵循"两个真实案例反推抽象"，非过早抽象）。宿主打开子层时调 `InitializeFromCurrentRequest()` 回填当前种子/难度；订阅 `OnBackRequested` 切回自己的主层。快捷按钮（下一把/重开）直接调 `StartLevelWith(...)`。

## 4. 资产清单

| 资产 | 路径 | 说明 |
|---|---|---|
| 主菜单 | `/Game/ZeroEscape/UI/WBP_MainMenu` | 父类 `UMainMenuWidget` |
| 结算界面 | `/Game/ZeroEscape/UI/WBP_ResultMenu` | 父类 `UResultMenuWidget`，内嵌 WBP_LevelSetup |
| 暂停菜单 | `/Game/ZeroEscape/UI/WBP_PauseMenu` | 父类 `UPauseMenuWidget`，内嵌 WBP_LevelSetup |
| 选择关卡组件 | `/Game/ZeroEscape/UI/WBP_LevelSetup` | 父类 `ULevelSetupWidget` |
| 暂停输入 | `/Game/ZeroEscape/Input/Actions/IA_Pause` + `IMC_ZeroEscape` 加 Esc 映射 | Digital(bool) |
| 控制器 | `/Game/ZeroEscape/GameFlow/BP_ZeroEscapePlayerController` | CDO 配 `PauseAction=IA_Pause`、`PauseMenuWidgetClass=WBP_PauseMenu` |
| GameMode | `/Game/ZeroEscape/GameFlow/BP_ZeroEscapeGameMode` | 已配 GameStateClass/ExitActorClass/ResultMenuWidgetClass/PlayerControllerClass |
| GameState | `/Game/ZeroEscape/GameFlow/BP_ZeroEscapeGameState` | — |
| 出口 | `/Game/ZeroEscape/GameFlow/BP_ZeroEscapeExitVolume` | Mesh=Sphere×0.5 |

## 5. 已验证清单（2026-08-03 用户验收）

- 胜利链：进入出口触发器 → `ZE_ROUND_RESULT result=Win` → 结算界面。
- 失败链：生命归零 → `ZE_ROUND_RESULT result=Lost` → 结算界面。
- 结算界面：下一把（新种子同难度）/重开（同种子同难度）/选择关卡/返回主菜单。
- 暂停菜单：ESC 弹/关、继续、选择关卡开新局、返回主菜单。

## 6. 待办与已知事项

- **审美统一打磨（已排期到最后）**：主菜单、结算、暂停、LevelSetup 统一重做风格；当前样式为可用占位。届时考虑把主菜单也迁移到复用 `ULevelSetupWidget`。
- 暂停/结算时的音频淡出、过场动画未做（本轮范围外）。

## 7. 交接要点（踩坑记录）

- C++ 中 `EditDefaultsOnly/EditAnywhere` 的裸指针资源引用（WidgetClass、InputAction 等）不会自动赋值，必须在蓝图 CDO 配置——"功能完全失效"先查这条。
- WBP 中控件命名与 `BindWidget` 成员名必须完全一致，含内嵌 UserWidget 实例（`LevelSetup`）。
- 构建前必须关闭编辑器（Live Coding 占用会导致链接失败，退出码 6）。
- `UUserWidget` 无默认无参构造函数，子类不要自定义无参构造。
