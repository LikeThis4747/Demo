---
applyTo: "Source/**/*.{h,cpp,cs}"
---

- C++ 承担玩法规则和性能关键逻辑；Runtime 模块不得依赖 UnrealEd。
- 默认关闭 Actor/Component Tick；启用时说明原因和停止条件。
- `UPROPERTY` 只暴露必要配置，优先 `EditDefaultsOnly`、`VisibleAnywhere`、`BlueprintReadOnly`。
- 使用前置声明降低头文件依赖；反射类型、生命周期和委托必须遵循 UE 规则。
- 修改后至少完成 DemoEditor Development 构建。
