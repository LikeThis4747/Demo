# Fab 素材调研：科幻/废料/工业 + 恐怖氛围 + PCG 迷宫

> 2026-07-22 调研稿（v2，已补全价格与扩展品类）。需求来源：《零号逃亡》策划案 4.3 资源策略 —— 统一的模块化科幻工业/废料设施风格，优先免费/已购/AI 辅助资源。
> 额外约束：追逐战需恐怖/地牢氛围；PCG 运行时拼接地牢房间/迷宫，要求素材**严格模块化、连接口规整**。
>
> 🔥 **时效提醒：Fab 夏季大促进行中，绝大多数候选 3~7 折，2026-07-30 03:59 UTC 截止（北京时间 07-30 中午）。要买的话促销期内决策。**
> 价格说明：个人版（Personal）美元价，由 Fab API 价格档位推断，页面实际显示港币；个人学习/独立开发 Personal 许可即可。

## 打分维度与权重

| 维度 | 权重 | 说明 |
|---|---|---|
| 模块化/PCG 适配 | 25% | 连接口标准、墙/地板/天花板成体系，适合运行时拼接 |
| 风格匹配 | 20% | 科幻 + 工业 + 废料，非明亮干净风 |
| 恐怖/阴暗氛围 | 20% | 做旧、污渍、昏暗灯光、官方恐怖定位 |
| 内容丰富度 | 10% | 网格/材质/贴花/蓝图/音效数量 |
| 引擎兼容 | 10% | UE 5.7.4、Lumen/Nanite |
| 性价比 | 10% | 促销价 vs 内容量 |
| 口碑成熟度 | 5% | 评分数量、作者背景 |

## 一、主体环境套件排名（核心决策，三选一）

| # | 资产 | 综合 | 促销价(个人版) | 一句话结论 |
|---|---|---|---|---|
| 1 | **Modular Sci-Fi Research Facility** (stcn) | 8.7 | $59.99→**~$42 (7折)** | horror 定位 + 原生 UE5.7，最对口 |
| 2 | **Modular Brutalist Sci-Fi Laboratory** (stcn) | 8.4 | $78.99→**~$55 (7折)** | 同作者，混凝土巨构更"地牢"，内容量未公布 |
| 3 | **Perimeter: Sci-Fi Modular Pack** (Pavel I.) | 8.2 | $69.99（无折扣） | 做旧参数最强，但无折扣性价比掉队 |
| 4 | **Sci-Fi Industrial Lab - Cyberpunk** (kaseng lui) | 8.0 | $29.99→**~$15 (5折)** | 性价比之王：178网格+音效+蓝图，需调暗霓虹 |
| 5 | **Modular SciFi Station** (Martin Milz) | 7.8 | $44.99→**~$22.5 (5折)** | 口碑最强（21×5星），偏干净空间站风 |
| 6 | Sci-Fi Modular Environment (GhostMachineArt) | 7.0 | $69.99→**~$21 (3折)** | 便宜但烘焙光照与追逐动态灯光冲突 |
| 7 | Modular Sci-Fi Factory Environment (Ru-Shan) | 6.3 | $99.99→~$50 (5折) | 精简版，内容量存疑 |
| 8 | Modular Horror Buildings (Bright Tower) | 6.0 | 未查 | 非科幻，仅氛围参考 |
| 9 | Modular Sci-Fi Corridor Kit (LowPolyCraft) | 4.5 | 未查 | ❌ 仅 glb 格式非原生 UE，不推荐 |

### 详情与链接

**1. Modular Sci-Fi Research Facility — stcn ⭐首选**
- https://www.fab.com/listings/4812a333-7e6d-40fb-92d0-667b24d15e9b
- 官方标注适用 **horror sci-fi scenes / Horror laboratory levels**；69 网格、18 贴花、5 主材质+62 实例、233 贴图(4K)、LOD+碰撞+Nanite；**明确兼容 UE 5.7**
- 标签：industrial、underground、corridor
- 缺点：仅 1 条评分(5.0)，贴花偏少需 Megascans 补

**2. Modular Brutalist Sci-Fi Laboratory — stcn**
- https://www.fab.com/listings/8a2001b3-c816-4c42-a2f8-c5b4a5fc9179
- 同作者同 horror 定位；粗野主义混凝土巨构 + 地下掩体，"地牢压抑感"全场最强；橙/青电影级灯光
- 缺点：网格数量未公布（22 张预览图+预告片，需眼见为实）；UE 版本未标注

**3. Perimeter: Sci-Fi Modular Environment Pack — Pavel Inozemtsev**
- https://www.fab.com/listings/5730177a-3cba-4377-b076-d0185ffe18fd
- 做旧能力最强：父材质调污渍/磨损/划痕 + 污渍贴花 + 蒸汽/火花/灰尘粒子；蓝图动画门/电缆/灯具；UE4烘焙+UE5 Lumen 双版本；标签含 Horror/Interior
- 作者有 Mars Sanctuary、Omega 等成熟前作；缺点：Fab 0 评分且**不参与促销**

**4. Sci-Fi Industrial Lab - Modular Cyberpunk — kaseng lui（性价比之王）**
- https://www.fab.com/listings/b9a8b0f5-de96-4dfb-8aba-b4442b5c1a9d
- 178 网格、22 蓝图（风扇/门/交互灯）、67 材质实例、160 张 4K、**14 个工业音效（风扇轰鸣/金属脚步/开门声——追逐氛围直接可用）**、1 Niagara；Lumen+Nanite
-  gritty 工业底 + 霓虹皮，需调暗；0 评分。**$15 买这个内容量，预算方案首选**

**5. Modular SciFi Station — Martin Milz（CIG 高级环境美术）**
- https://www.fab.com/listings/0667e321-31a7-40ed-b85c-6db5bbc4366b
- 147 网格、4 主材质+44 实例、4K、LOD；UE4.20+/5.0+；**21 个评分 100% 五星（全场口碑最强）**，CGChannel 等媒体报道
- 曾限免（2026-06-16 截止，已过期）——**先查账户库，领过就是免费**
- 缺点：干净空间站风，恐怖氛围全靠打光和做旧

**6. Sci-Fi Modular Environment — GhostMachineArt**
- https://www.fab.com/listings/a086d818-d205-40e1-803f-f38cf9f0d615
- 标签含 Industrial+Horror；模块化+预构建多房间；3 条满分
- ⚠️ 硬伤：烘焙静态光照设计，UE5 需关 Lumen——与追逐战动态灯光（闪烁灯、追猎者光源）冲突

**7~9 简评**
- #7 Modular Sci-Fi Factory（https://www.fab.com/listings/5db9a742-c21d-4920-a3ec-b44d48d782f9）：Battle Royale 包精简版，内容量存疑
- #8 Modular Horror Buildings（https://www.fab.com/listings/a7a7e4aa-6c76-4071-ad60-f34fe2a1d943）：写实寂静岭风非科幻，只看氛围
- #9 Modular Sci-Fi Corridor Kit（https://www.fab.com/listings/cfd5f5b9-6631-488e-90f7-5379f357e101）：❌ 仅 glb/gltf/usdz，不推荐

## 二、扩展品类（策划案配套需求）

### 废料/道具（物理可投掷物 + 场景杂物）
| 资产 | 价格 | 说明 |
|---|---|---|
| **Junkyard Environment Kit** ⭐ | $54.99→**~$27.5 (5折，至08-03)** | https://www.fab.com/listings/07f1fea7-bab3-499b-b9bd-eed41654e5f1 —— 废料堆/坦克残骸/电缆，写实 PBR；"废料"风格最直接来源，可做投掷物网格参考。偏户外，非室内主体 |
| Industrial Environment Props Pack | **~$1 (HKD 7.76)** | https://www.fab.com/listings/6fdabdf4-9cb9-462d-990a-0da125e22864 —— 20 个低模工业道具（油桶/气瓶/管道），≈白送但仅 glb 格式，需手动导入 |

### 追猎者角色
| 资产 | 价格 | 说明 |
|---|---|---|
| **SciFi Monsters Pack 2** (Diplodok) | $129.99→**~$39 (3折)** | https://www.fab.com/listings/e782f7cc-3162-4bfd-a1de-f7c32451ef51 —— 3 怪物（恶魔/外星蛇/变异体），4K PBR，Epic Skeleton 绑定可重定向；⚠️ **不含动画**，需配动画包或重定向 |

追猎者不阻塞开发：前期用免费 Manny/Quinn 或 Lyra 角色占位，风格定了再买。

### 危险区陷阱（策划案加分项）
| 资产 | 价格 | 说明 |
|---|---|---|
| **Trap Pack Blockout + VFX** ⭐ | **$6.99（无折扣但便宜）** | https://www.fab.com/listings/68b4a642-42ce-4039-b3f8-5eec33a3b247 —— 43 网格：地刺/移动锯/压碎机/激光/断头台/电击/绞肉机 + 3 VFX（灰尘/血溅/锯火花）；轴心碰撞已优化，蓝图可控，覆盖策划案全部危险区需求 |

### 磁力电弧 VFX（抓取表现层）
| 资产 | 价格 | 说明 |
|---|---|---|
| **Niagara Examples Pack**（Epic 官方）⭐ | **免费** | https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600 —— UE 5.7 专用 50+ Niagara 系统，最佳实践示例，必领 |
| Lightning / Electricity VFX Pack (Rimaye.Std) | $34.99→**~$17.5 (5折)** | https://www.fab.com/listings/c59ade4c-4429-4b9b-b53c-7cd5c5524c61 —— **两点间放电/永久电弧/电光环**，正是磁力抓取的"手到物体电弧"效果；4.9 分（24×5星，口碑好） |

### 免费必领（白嫖清单）
1. **Niagara Examples Pack**（见上）
2. **Scifi Hallway**（Epic 官方，免费，5.0 分）：https://www.fab.com/listings/e3cadcef-7709-4e6d-9f56-d6fb2156cb67 —— 教学示例，拆材质/打光参考
3. **Quixel Megascans**（Fab 免费）：锈迹/污渍/混凝土贴花与材质，恐怖做旧主力
4. 检查账户库：**Modular SciFi Station** 若 6 月限免领过则已免费拥有

## 三、采购方案（促销期价格，07-30 前）

| 方案 | 组合 | 总价 | 适合 |
|---|---|---|---|
| **A 最对口** | #1 主体 + Junkyard + Trap Pack + Lightning VFX + 免费三件套 | **~$94**（≈HKD 735） | 一步到位，风格精准 |
| **B 预算优先** ⭐ | #4 主体($15) + Trap Pack($7) + 免费三件套 | **~$22**（≈HKD 172） | 先把迷宫跑起来，不够再补 |
| **C 品质优先** | #1 或 #2 + #5 + Junkyard + Trap + Lightning VFX + 免费三件套 | **~$120~155** | 双主体套件混拼，变化最丰富 |
| 追猎者 | SciFi Monsters Pack 2（3折 $39） | 可延后 | 先用占位角色，风格定了再买 |

个人建议：**方案 B 起步 + 追猎者延后**。理由：三周工期，#4 的 178 网格 + 音效足够搭出可玩的阴暗工业迷宫，省下的时间比省钱更值；Junkyard 等 PCG 结构跑通后按实际缺口再补（它促销到 08-03，时间更宽）。

## 四、对 PCG 的素材技术要求（采购前最后核对清单）

买主体套件前，在页面/预告片里确认：
1. **模块连接口**：墙/地板模块是否统一网格尺寸（如 2m/4m 模数），拼接缝是否规整 —— 运行时拼接的硬需求
2. **轴心点**：模块 pivot 是否在连接口或角落，而非几何中心
3. **碰撞体**：是否自带 UCX 简化碰撞（策划案物理玩法需要）
4. **门洞模块**：是否有独立门框/门洞件，供房间连接关系使用
5. **天花板**：是否有完整天花板件（"地牢感"需要封闭空间，#4 需重点确认）

## 下一步

1. 用户确认方案（A/B/C）→ 促销期 **07-30 前下单**
2. 先领免费三件套（Niagara Examples Pack / Scifi Hallway / Megascans 收藏）
3. 查账户库是否已有 Modular SciFi Station
4. 到货后记录资产名称/来源/许可到项目资产清单（策划案 4.3 要求）
