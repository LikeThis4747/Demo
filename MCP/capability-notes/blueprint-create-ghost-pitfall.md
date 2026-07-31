# blueprint.create / set_parent_class 幽灵蓝图坑（2026-07-31）

## 症状
1. `blueprint.create`（及 `widget.create`）传入自定义 C++ 父类路径（`/Script/Demo.XxxClass`）时，返回 success，但实际父类被设成默认基类（Actor / GameModeBase / UserWidget）。
2. 这些"新建"的蓝图实际**复用了之前会话残留的同名脏蓝图**：带不该有的 EventGraph、Tick/BeginPlay/Overlap 事件节点（连 GameInstance 这种非 Actor 都带了 ReceiveActorBeginOverlap），父类也是旧的。
3. `blueprint.set_parent_class` 对这些脏蓝图：返回 success，但部分生效、部分不生效，不可预测。
   - 案例：BP_ZeroEscapeGameMode（原父类非法 MainMenuWidget）set 后验证通过；
   - 案例：BP_MainMenuGameMode（原父类合法 GameModeBase）set 返回 success + 显示新父类，但 get_summary 仍读旧父类，compile 后也不变。
4. `blueprint.set_property`（设 CDO 属性）报 "Property not found"——只扫蓝图本地变量，**不扫 C++ 父类继承的 UPROPERTY**（EditDefaultsOnly 字段设不了）。

## 根因
记忆 67701887 的重演：上一轮崩溃/退出会话留下同名损坏蓝图（内存幽灵 + 磁盘 .uasset 都在但内容脏），`blueprint.create` 复用而非新建。

## 当前能力边界确认（已按规范走完流程）
1. ✅ 查 capability-notes：本文件
2. ✅ ue_actions_search 多词搜索："delete asset remove blueprint"、"set inherited property CDO"、"get properties list variables" —— **本地 MCP 没有"删除普通蓝图资产"的 action**（只有 widget.delete 删 Widget 蓝图）；没有设置继承自父类的 CDO UPROPERTY 的可靠 action。
3. ✅ ue_actions_schema 确认：blueprint.set_property 只接受 blueprint_name/property_name/property_value，无 asset_path 选项；property_value 是 string。
4. ✅ 实际调用验证：set_parent_class 假成功（见上）；set_property not found；get_actor_properties 只返回 Transform 不返回 UPROPERTY 详情。
5. 三次尝试无果。本地 MCP 无法可靠完成"建干净蓝图 + 设 C++ 父类继承属性"。

## 规避方案（已采用）
- **蓝图父类**：用户在蓝图编辑器手动改（Class Settings → Parent Class，勾选 Show All Classes 找自定义 C++ 类）。最稳。
- **C++ 父类继承的 EditDefaultsOnly UPROPERTY**（如 PursuerClass / MainMenuWidgetClass / GameLevelName）：
  - 方案A：用户在蓝图 Class Defaults 手动指定（最稳）。
  - 方案B：本地 MCP `blueprint.set_property` 不行；可尝试官方 ue58-official-mcp 的 AssetTools（session 恢复后再试）。
- **避免 blueprint.create 复用幽灵**：新建蓝图前，先确认目标路径无同名资产；若有，用户在 Content Browser 手动删除后再 create。

## 教训
- `blueprint.create` / `widget.create` / `set_parent_class` / `set_property` 的 success 返回值不可信，**必须用 get_summary 验证真实父类 + list_dir 验证磁盘落盘**。
- 并行调用 `set_parent_class` 会产生竞态（两个蓝图父类设串）——**set_parent_class 必须严格串行**。
- 设继承自 C++ 父类的 UPROPERTY，本地 MCP 当前无可靠手段，需用户手动或依赖官方 MCP。
