---
applyTo: "{Content,Config}/**/*"
---

- 修改 UE 资产前必须通过 UE Editor MCP 读取父类、组件、引用和当前配置。
- 蓝图只做资源装配、UI、关卡配置和 AnimBP 连线，不承载核心玩法规则。
- 配置优先进入 DataAsset/DataTable，不把纯数据散落在蓝图默认值中。
- 修改后编译并保存相关资产，检查日志并在实际关卡验证。
