# 暂停菜单 + 选择关卡组件提炼 —— 详细改动规划

> 状态：规划稿（待用户确认后一次性落盘）
> 关联：PAUSE_MENU_PLAN.md 的实施细节落地版
> 最后更新：2026-08-03

## 0. 现状核实（已查证）

- GameMode `playerControllerClass` = `/Script/Demo.ZeroEscapePlayerController`（C++ 类，无蓝图子类）
- 输入走 EnhancedInput，IMC 由角色 `ApplyInputMappingContexts` 管理（`IMC_ZeroEscape` 等）
- 角色 `SetupPlayerInputComponent` 已绑定 Move/Look/Jump/磁力等动作
- ResultMenuWidget 当前自带种子/难度/StartLevel 全套逻辑（待重构移出）
- GameInstance 有 `GetPendingRequest()` / `SetPendingRequest()` / `SetPendingSeed()`

## 1. 文件改动总览

| 文件 | 类型 | 改动 |
|---|---|---|
| `UI/LevelSetupWidget.h/.cpp` | 新增 | 共用"选择关卡"组件 |
| `UI/PauseMenuWidget.h/.cpp` | 新增 | 暂停菜单 |
| `UI/ResultMenuWidget.h/.cpp` | 重构 | 删子层逻辑，改内嵌 LevelSetup |
| `GameFlow/ZeroEscapePlayerController.h/.cpp` | 修改 | ESC 输入 + 暂停菜单创建/销毁 |
| `WBP_LevelSetup` | 新增 | 蓝图 |
| `WBP_PauseMenu` | 新增 | 蓝图（内嵌 WBP_LevelSetup） |
| `WBP_ResultMenu` | 修改 | 删 SetupPanel，内嵌 WBP_LevelSetup |
| `IA_Pause` | 新增 | InputAction 资产 |
| `IMC_ZeroEscape` | 修改 | 加 Esc→IA_Pause 映射 |
| `BP_ZeroEscapeGameMode` | 修改 | 若需要 PlayerControllerClass 指向新 BP（暂不需要，PC 是 C++ 类直接可用） |

## 2. ULevelSetupWidget（新增共用组件）

### 职责
采集种子 + 难度 → 调 `StartLevelWith(Seed, Difficulty)` 开新局；提供"返回"委托。

### .h 关键设计
```cpp
class ULevelSetupWidget : public UUserWidget
{
    // —— 宿主调用的公共接口 ——
public:
    // 用本局请求初始化种子/难度初值（宿主打开子层时调）
    void InitializeFromCurrentRequest();

    // 直接开新局（宿主的"下一把/重开"快捷按钮调这个）
    void StartLevelWith(int32 Seed, EZeroEscapeDifficulty Difficulty);

    // 返回委托（宿主订阅，决定返回到自己的主层）
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSetupBackRequested);
    UPROPERTY(BlueprintAssignable) FOnSetupBackRequested OnBackRequested;

    // —— BindWidget 控件（与现 ResultMenu 子层一致，迁移过来）——
    UPROPERTY(BindWidget) UEditableTextBox* SeedInput;
    UPROPERTY(BindWidget) UButton* RandomButton;
    UPROPERTY(BindWidget) UButton* DiffEasyButton;
    UPROPERTY(BindWidget) UButton* DiffNormalButton;
    UPROPERTY(BindWidget) UButton* DiffHardButton;
    UPROPERTY(BindWidget) UButton* StartButton;
    UPROPERTY(BindWidget) UButton* BackButton;

protected:
    UPROPERTY(EditDefaultsOnly) FName GameLevelName;

private:
    // 从 ResultMenuWidget 迁移过来的全部私有逻辑：
    // HandleSeedCommitted / HandleRandomClicked / HandleDifficulty*Clicked
    // RefreshDifficultyHighlight / StartLevel
    int32 SelectedSeed;
    EZeroEscapeDifficulty SelectedDifficulty;
};
```

### .cpp
- `NativeConstruct`：绑 8 个按钮事件（同现 ResultMenu 的子层部分）
- `InitializeFromCurrentRequest`：读 GameInstance->GetPendingRequest() 回填
- `StartLevelWith`：写 GameInstance + 取消暂停 + OpenLevel（现 StartLevel 逻辑，改为 public）
- 其余 Handle* 从 ResultMenu 原样迁移

## 3. UResultMenuWidget（重构）

### 删除（移到 LevelSetup）
- SetupPanel / SeedInput / RandomButton / DiffEasy/Normal/HardButton / StartButton / BackButton
- HandleSeedCommitted / HandleRandomClicked / HandleDifficulty*Clicked / RefreshDifficultyHighlight
- HandleBackClicked / HandleStartClicked / StartLevel / SelectedSeed / SelectedDifficulty

### 保留（主选择层）
- ChoicePanel / ResultTitle / PrimaryButton / PrimaryButtonLabel / SelectLevelButton / MainMenuButton
- ShowResult(bWon) / HandlePrimaryClicked / HandleSelectLevelClicked / HandleMainMenuClicked
- WinTitle / LoseTitle / NextRunLabel / RetryLabel / MainMenuLevelName / bWonResult

### 新增
- `BindWidget ULevelSetupWidget* LevelSetup`（内嵌的子控件实例）
- `HandleSetupBackRequested()`：订阅组件返回委托 → 切回主选择层
- `HandlePrimaryClicked` 改为：胜→`LevelSetup->StartLevelWith(随机, 当前难度)`；负→`LevelSetup->StartLevelWith(当前种子, 当前难度)`
  - "当前种子/难度"从 GameInstance 读（不再用本地成员）
- `HandleSelectLevelClicked` 改为：`LevelSetup->InitializeFromCurrentRequest()` + 切显示 LevelSetup

### ShowResult 改动
- 不再本地存 SelectedSeed/SelectedDifficulty（移到组件）
- 显示主选择层、隐藏 LevelSetup（`LevelSetup->SetVisibility(Collapsed)`）

## 4. UPauseMenuWidget（新增）

### 职责
ESC 弹出的暂停菜单，主层[继续/选择关卡/返回主菜单]，内嵌 LevelSetup。

### .h 关键设计
```cpp
class UPauseMenuWidget : public UUserWidget
{
public:
    virtual void NativeConstruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent& KeyEvent) override;
    void ShowPauseMenu(); // PC 调用：初始化 + 切UI输入 + 暂停

    UPROPERTY(BindWidget) UWidget* ChoicePanel;
    UPROPERTY(BindWidget) UTextBlock* PauseTitle;
    UPROPERTY(BindWidget) UButton* ResumeButton;
    UPROPERTY(BindWidget) UButton* SelectLevelButton;
    UPROPERTY(BindWidget) UButton* MainMenuButton;
    UPROPERTY(BindWidget) ULevelSetupWidget* LevelSetup;

protected:
    UPROPERTY(EditDefaultsOnly) FName MainMenuLevelName;

private:
    void HandleResumeClicked();      // 取消暂停+移除+切游戏输入
    void HandleSelectLevelClicked(); // 显示 LevelSetup
    void HandleMainMenuClicked();    // 取消暂停+OpenLevel
    void HandleSetupBackRequested(); // 回主层

    void ShowMainChoice(); // 切回主选择层
};
```

### NativeOnKeyDown
- 捕获 Escape 键 → 等价"继续"（关闭暂停菜单）
- 这样暂停时再按 ESC 能关掉

## 5. AZeroEscapePlayerController（修改）

### .h 新增
```cpp
class UPauseMenuWidget;

UCLASS()
class AZeroEscapePlayerController : public APlayerController
{
    // ... 现有 ...

protected:
    virtual void SetupInputComponent() override;

private:
    UPROPERTY(EditDefaultsOnly, Category="ZeroEscape|Pause")
    TObjectPtr<UInputAction> PauseAction;

    UPROPERTY(EditDefaultsOnly, Category="ZeroEscape|Pause")
    TSubclassOf<UPauseMenuWidget> PauseMenuWidgetClass;

    UPROPERTY(Transient)
    TObjectPtr<UPauseMenuWidget> PauseMenuWidget;

    UFUNCTION()
    void HandlePausePressed(); // 弹暂停菜单

    void ClosePauseMenu(); // 给 PauseMenuWidget 调用关闭
};
```

### .cpp
- `SetupInputComponent`：Cast 到 UEnhancedInputComponent，`BindAction(PauseAction, Triggered, this, &HandlePausePressed)`
- `HandlePausePressed`：若已有 PauseMenuWidget 则 return；否则 CreateWidget + ShowPauseMenu() + AddToViewport + SetInputMode(UIOnly) + SetShowMouseCursor(true) + SetGamePaused(true)
- `ClosePauseMenu`：RemoveFromParent() + SetInputMode(GameOnly) + SetShowMouseCursor(false) + SetGamePaused(false)
- PauseMenuWidget 的"继续"按钮调 PC->ClosePauseMenu()

## 6. 资产改动（MCP）

### 新建
- `IA_Pause`（InputAction，默认值 type=Digital, default value=false）
- `WBP_LevelSetup`（父类 ULevelSetupWidget，搭控件树 = 现 ResultMenu 的 SetupPanel 内容，设样式）
- `WBP_PauseMenu`（父类 UPauseMenuWidget，控件树：ChoicePanel/PauseTitle/ResumeButton/SelectLevelButton/MainMenuButton + 内嵌 WBP_LevelSetup 实例）

### 修改
- `IMC_ZeroEscape`：加 Esc→IA_Pause 映射（用 input.add_key_mapping）
- `WBP_ResultMenu`：删除 SetupPanel 及其全部子控件，在 ChoicePanel 同级加一个 WBP_LevelSetup 实例（命名 LevelSetup，设为 variable，与 BindWidget 匹配）
- `BP_ZeroEscapeGameMode`：暂不改 PlayerControllerClass（C++ PC 已直接生效）；但需要确保 PC 的 `PauseAction` 和 `PauseMenuWidgetClass` 在某处赋值——由于 PC 没有 BP 子类，这两个 EditDefaultsOnly 字段需要在 C++ 构造函数赋默认值，或新建 BP_PC。**决策：新建 `BP_ZeroEscapePlayerController`**（和 BP_ZeroEscapeGameMode 一致的模式），在 CDO 里配这两个字段，然后 GameMode 设 PlayerControllerClass=BP_PC。

## 7. 风险与对策

| 风险 | 对策 |
|---|---|
| 重构 ResultMenuWidget 回归 | 重构后单独验证胜负全流程（下一把/选择关卡/开始/返回/重开） |
| BindWidget 内嵌 UserWidget 不匹配 | WBP 里放 WBP_LevelSetup 实例时命名必须与 C++ 成员 `LevelSetup` 完全一致 |
| ESC 在结算界面也触发 Pause | PC 的 HandlePausePressed 检查：若 PauseMenuWidget 已存在或游戏已暂停则 return |
| PauseAction 未赋值（EditDefaultsOnly 裸指针）| 新建 BP_PC 时必须配 IA_Pause 和 WBP_PauseMenu（记忆教训：裸指针是头号失效原因） |

## 8. 落盘顺序（一次性做完）

1. 写 4 个 C++ 新增文件 + 改 2 个（ResultMenuWidget + PlayerController）
2. 构建（用户关编辑器后）
3. MCP 搭资产：IA_Pause / WBP_LevelSetup / WBP_PauseMenu / 修改 WBP_ResultMenu / 修改 IMC / 新建 BP_PC / 配 GameMode
4. PIE 验证：
   - 回归：胜负结算全流程（下一把/选择关卡/开始/返回/重开）
   - 新功能：游戏中按 ESC 弹暂停→继续/选择关卡/返回主菜单；再按 ESC 关闭
