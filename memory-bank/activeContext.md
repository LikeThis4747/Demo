# Active Context — Demo

> 当前任务详情以 .ai-context/current-task.md 为唯一来源；此处只保留迭代焦点与任务索引。

## 当前迭代焦点

Level0 V2 全部楼梯已完成隐藏坡面、护栏收口、Recast、真实追猎者通行与玩家手感所需的碰撞职责精修；当前等待用户关闭玩家“保持水平地面速度”并手动实走。通过后进入多层 WFC 数据合同讨论；正式 Exit/死亡/重开闭环与回归基线仍后置。

## 当前索引

- Level0 V1/V2：V1 冻结；V2 共有 12 跑，中央塔 4 跑，A/B/C/D 各 2 跑。
- 可见楼梯：`StairsB` 负责外观与非 Pawn 碰撞，`Pawn=Ignore`，不影响导航。
- 隐藏坡面：约 393.23×205×8cm、34.90°；端点高出基准约 0.5cm；`Pawn=Block`、`Visibility/Camera=Ignore`、影响导航，是 Pawn/Recast 唯一连续楼梯面。
- 平台：每一跑独立一块坡面；180° 转向继续使用真实水平地板。
- 护栏：四座双层楼梯各一处尾栏缺口已闭合；楼梯入口与模块接口保持开放。
- 验证：精修后 A 上行与中央塔四跑双向通过；保存重载 12/12 属性与 Transform 一致，PIE 停止、临时 Actor 为 0、Level0 非脏。
- 玩家：需在 BP_ZeroEscapeCharacter 的 Character Movement 关闭 `Maintain Horizontal Ground Velocity` 后实走；Camera Lag 暂不改。
- WFC：楼梯、坡面、平台、净空、接口与栏杆是不可拆分的多格宏模块；隐藏坡面不是独立 Tile。

## 当前边界与风险

- 本轮只写入 Level0、任务/当日计划、PCG 模块记录与项目记忆；没有修改第三方资产、蓝图、C++、DataAsset、其他地图或 Recast 全局配置。
- 自动化与 AI 通行不等于玩家手感验收；任务仍保持 Active。
