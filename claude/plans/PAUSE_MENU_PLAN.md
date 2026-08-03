# 暂停菜单 + 选择关卡组件提炼 —— 实施规划

> 状态：规划稿（待用户确认后按步落盘）
> 关联：GAME_LOOP_PLAN.md 阶段二剩余项
> 最后更新：2026-08-03

## 1. 目标

1. 新增 **ESC 暂停菜单**：[继续 / 选择关卡 / 返回主菜单]，游戏流程闭环最后一块。
2. 把"选择关卡"（种子输入 + 随机 + 难度三键 + 开始）**提炼成独立可复用组件** `ULevelSetupWidget`。
3. 让**结算界面**和**暂停菜单**都内嵌这个组件，消除重复（这是"两个真实消费者反推抽象"的时机，不再是过早抽象）。

## 2. 架构

```
ULevelSetupWidget（新，独立可复用组件）
├─ 控件：SeedInput / RandomButton / Diff三键 / StartButton / BackButton
├─ 能力：种子采集、随机、难度高亮、StartLevelWith(seed,difficulty)=写GameInstance+取消暂停+OpenLevel
├─ 配置：GameLevelName
└─ 委托：OnBackRequested（返回目标由宿主决定）

被两个宿主内嵌复用：
┌─ ResultMenuWidget（重构：移除自带子层，改内嵌 LevelSetup）
│   主层：标题(胜/负) + 主按钮(下一把/重开) + 选择关卡 + 返回主菜单
│   下一把/重开 → 调 LevelSetup->StartLevelWith(...)
│   选择关卡 → 显示 LevelSetup + InitializeFromCurrent；订阅 OnBackRequested→回主层
│
└─ PauseMenuWidget（新）
    主层：标题(暂停) + 继续 + 选择关卡 + 返回主菜单
    继续 → 取消暂停+移除界面+切游戏输入
    选择关卡 → 显示 LevelSetup + InitializeFromCurrent；订阅 OnBackRequested→回主层
    ESC 再按 → NativeOnKeyDown 捕获→等价"继续"
```

## 3. ESC 输入方案

- 打开暂停：游戏进行中走 EnhancedInput。新增 `IA_Pause`，在游戏 IMC 加 `Esc → IA_Pause` 映射。
- 绑定位置：`AZeroEscapePlayerController`（暂停是会话级功能，不属于角色移动）。给 PC 加 `PauseAction`(UInputAction*) + `PauseMenuWidgetClass`，重写 `SetupInputComponent` BindAction。
- 关闭暂停：暂停后是 UI-only 输入，游戏 EnhancedInput 收不到键；由 `PauseMenuWidget::NativeOnKeyDown` 捕获 Esc 触发"继续"。
- 待实现时用 MCP 确认：GameMode 的 PlayerControllerClass 是否指向 `BP_ZeroEscapePlayerController`（不存在则新建）、PC 是否用 EnhancedInputComponent、游戏 IMC 资产路径。

## 4. 文件清单

**新增 C++：**
- `Public/UI/LevelSetupWidget.h` + `Private/UI/LevelSetupWidget.cpp`
- `Public/UI/PauseMenuWidget.h` + `Private/UI/PauseMenuWidget.cpp`

**修改 C++：**
- `UI/ResultMenuWidget.h/.cpp`（移除种子/难度/StartLevel 逻辑与 SetupPanel 控件，改内嵌 LevelSetup；下一把/重开改调组件）
- `GameFlow/ZeroEscapePlayerController.h/.cpp`（ESC 输入 + 暂停菜单创建/销毁 + 输入模式切换）

**新增资产（MCP）：**
- `WBP_LevelSetup`（父类 ULevelSetupWidget）
- `WBP_PauseMenu`（父类 UPauseMenuWidget，内嵌 WBP_LevelSetup）
- `IA_Pause`（InputAction）
- 可能新增 `BP_ZeroEscapePlayerController`

**修改资产（MCP）：**
- `WBP_ResultMenu`（删除 SetupPanel，改内嵌 WBP_LevelSetup）
- 游戏 IMC（加 Esc→IA_Pause）
- `BP_ZeroEscapeGameMode`（若新建 PC，则设 PlayerControllerClass）

## 5. 分步实施（每步独立验证，隔离重构风险）

**第 1 步：提炼 ULevelSetupWidget（纯新增，不碰现有）**
- 写 C++ 组件 + 构建
- 搭 WBP_LevelSetup + 样式
- 不接宿主，暂不可单独验证行为，但保证编译通过

**第 2 步：重构 ResultMenuWidget 内嵌组件（有回归风险，单独验证）**
- C++ 移除子层逻辑，改内嵌 + 委托订阅
- WBP_ResultMenu 删 SetupPanel，放 WBP_LevelSetup 实例
- 构建 + **回归验证**：胜利下一把/选择关卡/开始/返回、失败重开，全部照旧工作

**第 3 步：新建 PauseMenuWidget（新增）**
- 写 C++ + WBP_PauseMenu（内嵌 WBP_LevelSetup）
- 构建

**第 4 步：接 ESC 输入（PlayerController）→ 端到端验证**
- IA_Pause + IMC 映射 + PC 绑定 + BP 配置
- 验证：游戏中按 ESC 弹暂停→继续/选择关卡开新局/返回主菜单；再按 ESC 关闭

## 6. 风险点

- **R1 重构结算界面回归**：第 2 步单独验证胜负全流程，避免连带损坏已验证功能。
- **R2 ESC 开关对称**：打开走 EnhancedInput、关闭走 Widget OnKeyDown，两条路径分开。
- **R3 PlayerController 输入前提**：BP_PC 是否存在、是否 EnhancedInputComponent、IMC 路径——第 4 步开始前用 MCP 核实。
- **R4 内嵌 UserWidget 的 BindWidget**：宿主用 `BindWidget` 绑一个 UserWidget 类型子控件，WBP 里放子控件实例时命名需与 C++ 成员一致。

## 7. 不做（本轮范围外）

- UI 审美统一（后置到项目打磨阶段，用户已确认）
- 暂停时的音频淡出、动画过场
- 存档/设置页
