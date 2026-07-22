# Fab 第一轮素材候选 — 2026-07-22

> 目的：先比较《零号逃亡》的视觉感觉和 PCG 适配潜力。本表不代表购买、下载或导入决定。Fab 的实际价格、账户已购状态、许可档位和可添加引擎版本仍需登录用户账户逐项确认。

## 推荐先看的三条视觉路线

### A. 科幻工业骨架 + 废料做旧（当前首选）

用真正模块化的科幻环境承担 PCG 房间、门和连接口，再用废料、管线、警示贴花、故障灯和声音制造恐怖感。优点是“电磁能力”和世界观自然，空间也容易保持清楚；缺点是需要额外做旧，原始画面可能偏干净。

优先看：JUPITER、SF Corridors；搭配 Junkyard。

### B. 废弃工业恐怖 + 少量科幻标识

以废弃工厂或地下掩体为主体，通过供电终端、隔离门、发光标识和电磁特效补足科幻感。优点是恐怖氛围天然成立，废料物体丰富；缺点是容易变成普通废弃工厂，必须保持明确的科幻叙事元素。

优先看：Modular Abandoned Factory、Stylized Abandoned Lab Bunker。

### C. 地下科幻设施/掩体

强调地下层级、连接走廊、电梯和封闭舱室，最接近“科幻地牢”。优点是主题完整；缺点是旧包兼容性、整包规模和模块规范需要重点检查。

优先看：Modular Sci-FI Environment / Exterior / Interior / Underground。

## 核心候选

| 候选 | 视觉感觉与用途 | 对 PCG 有利的证据 | 当前疑点 | 建议 |
|---|---|---|---|---|
| [JUPITER - Sci-Fi Modular Environment Kit](https://www.fab.com/listings/930196d4-132a-4a28-893a-dc8a05edf8fc) | 橙灰色科幻工业设施；适合主环境骨架 | 面向第一/第三人称大场景；Grid Snap；门、箱子、Spline 线缆和智能灯；有源 FBX | 需要确认当前 UE 版本；原始氛围偏干净 | **优先看视觉，第一测试梯队** |
| [SF Corridors](https://www.fab.com/listings/09b8ab0c-ae92-4e3a-b994-f667c86a6b4f) | 科幻走廊、交叉口和大厅；适合地牢连接结构 | 70 个 Mesh；有 X/L/T 交叉、楼梯、大厅、箱子和桶；预览称已更新 5.5 | 需要确认墙/房间模块的完整度及 5.7 实测 | **优先看结构，第一测试梯队** |
| [Modular Sci-FI Environment / Exterior / Interior / Underground](https://www.fab.com/listings/33a2fbdd-12c3-4a85-82fb-02ddb97da89e) | 黑暗地下科幻设施，最接近“科幻地牢” | 室内、地下、室外可以连接；模块化并带电梯系统 | 页面显示约四年前发布；旧蓝图、材质与版本兼容性需验证 | **主题高度匹配，兼容性测试梯队** |
| [Stylized Abandoned Lab Bunker — Complete Modular Environment Kit](https://www.fab.com/listings/47b7b62f-b128-4125-a582-d3068ba5dd8e) | 风格化废弃实验室/掩体；偏 R.E.P.O./Lethal Company 气质 | 250+ 资产；管道、通风、电气、工具、家具；碰撞和 LOD；标注支持 UE 5.2–5.7 | 需要确认是否有足够墙、地面、门洞等主结构，还是更偏室内道具 | **很适合看氛围，可能作为道具/装饰包** |
| [Modular Abandoned Factory Environment](https://www.fab.com/listings/1cc66fae-b1a1-4dd0-9d5c-0b7b154f4827) | 写实废弃工厂、管线和碎屑；恐怖氛围自然 | 有模块墙、地板、结构件、通风和大量工业道具；标注 UE 5.6/5.7 | 科幻感不足；无 LOD；需要确认模块连接类型能否形成地牢房间 | **工业恐怖路线首看** |
| [Junkyard — Quixel Megascans](https://www.fab.com/listings/a984aac1-d20f-4232-8ce8-212e1695aaf6) | 写实废料场；提供可抓取物和遮挡物 | 96 个废料、垃圾、金属和工业资产；页面标为 Free | 场景文件标注 5.4–5.6；高分辨率写实资产可能偏重，也可能与风格化主包不一致 | **作为补充包，不作为 PCG 房间骨架** |

## 只建议作为对照或补充

| 候选 | 可借鉴之处 | 不作为首选的原因 |
|---|---|---|
| [Modular SciFi Industrial Corridors Kit](https://www.fab.com/listings/11d5b2af-679d-4323-968f-5540af154d0f) | 标注 UE 5.7.2；106 个 Grid Snap Mesh；墙、地、顶、门和 118 个贴花比较齐全 | 页面说明依赖 Nanite、DX12/Vulkan、SM6，并推荐 VSM；碰撞为 Complex as Simple。与 Demo 当前轻量渲染和大量运行时模块的基线存在风险 |
| [Modular sci-fi Environment / Corridors](https://www.fab.com/listings/c7f8c0e0-824d-4c7e-a4c1-549629140a5a) | 42 个 Mesh、碰撞和 3 个 Niagara FX，画面氛围强 | 505 张 4K 纹理、166 个材质实例且没有 LOD，三周 Demo 可能过重 |
| [Modular Sci-Fi - Industrial Platform](https://www.fab.com/listings/613e655d-c942-48f5-8a6c-9329e7c8c50a) | 平台、栏杆、管线、坡道和连接件适合追逐房间垂直变化 | 不是完整的墙/房间/走廊套装；页面也提醒大量 2K 资产同时摆放可能消耗较多内存 |
| [Modular Sci-Fi Corridor & Environment Kit](https://www.fab.com/listings/cfd5f5b9-6631-488e-90f7-5379f357e101) | 低模、PBR、通用格式，外观干净 | 主要提供 GLB/glTF/USDZ，不是原生 UE 内容包；需要自行处理导入、材质、碰撞和模块规范 |

## 当前判断

如果现在只选三项让用户先看感觉：

1. **JUPITER**：看“科幻工业主骨架”是否喜欢。
2. **Stylized Abandoned Lab Bunker**：看“风格化恐怖/废弃”是否更接近想象。
3. **Modular Abandoned Factory**：看“写实工业恐怖”是否比科幻走廊更有感觉。

若倾向 JUPITER，再深入比较 SF Corridors 的 PCG 结构；若倾向后两者，则决定是走风格化还是写实，并用少量科幻终端、贴花和灯光补足世界观。
