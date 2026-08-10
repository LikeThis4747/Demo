# Latest Error

- 已解决：用户首次 PIE 中两个机关完全不触发。
- 根因：Level0 两个旧摆放实例仍序列化 `Muzzle -> SceneRoot`；Blueprint 默认模板虽已是 `Muzzle -> AimPivot`，运行时实例仍命中严格父级校验并在绑定 Trigger 前进入 Disabled。
- 修复：两个 Level0 实例已正式保存正确父级；C++ 增加旧实例层级自愈、最终复核和详细失败日志。
- 验证：Demo `-NoLink` 和正式链接成功；定点 PIE 覆盖两个实例 `armed -> BeginOverlap -> warning -> fired`，无 disabled/failed，运行后资产非 Dirty。
- 当前没有未解决的机关构建、链接、Blueprint、资产保存或触发链错误。仍未闭合的是用户画面/玩法验收与薄墙、反弹专项；不得把这部分写成已通过。
