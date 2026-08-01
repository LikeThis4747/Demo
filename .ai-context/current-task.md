# Current Task

- 当前目标：以 Level0 三层 V2 静态样板为依据，先完成玩家与真实追猎者可走通的验证，再讨论多层 WFC 数据合同；未经新授权不修改 PCG/WFC 代码。
- 冻结基线：V1 `HydroLab_ThreeFloorPCGSceneV1`=1836 Actor；V2 `HydroLab_ThreeFloorPCGSceneV2_Refine`=1857 Actor，Level0 当前非脏。
- 已验证静态结果：外墙、斜栏杆、平台护栏、高厅和 Preview 标记已完成白天精修；V2 96 个灯实例采用关卡内 Movable 覆盖，第三方灯具蓝图模板仍为 Static。
- 当前阻塞：Level0 的 NavMeshBoundsVolume X 轴只覆盖 -17600..22400 cm，而 V2 已核对墙体约在 X=45000 cm；V2 三层 Recast、玩家胶囊连续实走和真实追猎者上下楼尚未验证。
- 代码边界：现有运行时生成合同仍是二维 FIntPoint GridSize 与四方向 OpeningMask；正式 GameMode 只有开局生成/放置，没有 Exit、死亡结算或重开入口。
- 最短下一步：白天先让导航体覆盖 V2，显示 Recast 并完成玩家/追猎者三层路线验收；随后完成正式一局闭环，再确认多格垂直宏块、上层保留占用、带类型接口和共享边唯一所有者的数据合同。
- 实施门禁：任何代码/资产修改继续遵循讨论方案、确认方案、代码预览、用户明确授权、联合验证与用户验收。
