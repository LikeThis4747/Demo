# 《零号逃亡》游戏音效需求与素材检索清单

> 状态：当前音效素材搜集与后续灰盒接入的正式需求清单。  
> 更新日期：2026-08-12  
> 当前边界：本文只定义要找或生成的声音、触发时机、建议长度、听感和验收标准；不表示这些音效已经接入 C++、Blueprint、关卡或 UE 音频资产。

## 1. 文档目的与证据边界

本项目是一款“废弃科幻工业设施中的第三人称物理追逐游戏”。音效首先服务以下判断：

1. 玩家能否只凭声音判断追猎者的方位和攻击阶段。
2. 玩家能否听懂地刺、冲锤、摆锤和发射器的危险节奏。
3. 磁力抓取、投掷、碰撞和破碎是否具有清晰的操作确认与重量感。
4. 出口、胜负、暂停和菜单是否形成完整反馈。
5. 在上述信息清晰后，再由环境底噪和音乐补足废弃工业恐怖氛围。

当前源码中未检出正式 `USound`、`PlaySound` 或 `AudioComponent` 接线。内容目录存在 HydroLab 的风扇、室外风、门、水管、音乐等 Sound Cue/Sound Wave，以及陷阱包的圆锯声音；这些只能作为候选库存。由于本次官方 UE5.8 MCP 在资产回读阶段发生 HTTP 传输断开，本文不把候选库存写成“关卡当前正在播放”。正式接入前仍需通过官方 MCP 回读 Blueprint、关卡引用和资产类型。

本文中的时间分为两类：

- **事件时间**：当前玩法代码已有的真实阶段或判定时机。
- **建议素材长度**：为了搜索、裁切或 AI 生成而给出的目标范围，不是当前已经落地的数值。

## 2. 优先级与总体混音原则

本文使用的 P0/P1/P2 是项目内部制作优先级，不是 Unreal Engine 官方概念：

| 优先级 | 含义 |
|---|---|
| P0 | 直接影响躲避、攻击确认、机关判断和胜负闭环，第一批必须具备 |
| P1 | 明显提升移动反馈、空间感和界面完整度，P0 通过后补齐 |
| P2 | 音乐与高阶变化，不阻塞第一版可玩音频闭环 |

混音优先级统一为：

1. 追猎者攻击预警和机关危险提示。
2. 玩家磁力操作、命中和受击确认。
3. 追猎者脚步与局部环境声源。
4. UI 与结算。
5. 环境底噪。
6. 背景音乐。

通用原则：

- 追猎者、机关、投掷物和局部机器使用来自物体位置的 3D 空间声音；菜单、准星确认、结算提示使用非空间声音。
- 穿墙后的追猎者与机关声音可适度变闷，但不能让攻击预警完全消失。
- 可频繁重复的脚步、碰撞、破风声准备多条变化；同类警告音保持固定声纹，让玩家能够学习。
- 攻击落空只播放起手和破风，不播放命中音。
- 循环声必须在状态结束、物体销毁、弹体休眠或关卡退出时停止。
- 危险提示播放时，可以短暂压低环境和音乐约 2～4 dB，但不应把所有声音同时做得更响。

## 3. P0：第一批必须寻找或生成的音效

### 3.1 追猎者

当前攻击时序来自 `Source/Demo/Public/Data/PursuerConfig.h`：近战判定约在起手后 0.45 秒；跑跳攻击约在 0.65 秒离地、飞行 0.65 秒后落地，落地后恢复约 0.75 秒。

| 编号 | 需要的声音 | 事件时间 / 建议素材长度 | 目标听感 | 空间与变化 | 推荐搜索词 |
|---|---|---|---|---|---|
| PUR-01 | 金属地面脚步 | 每次真实落脚；单条 0.15～0.4 秒 | 沉重金属靴、装甲轻震，不像普通运动鞋 | 追猎者位置 3D；5～8 条变化 | `heavy armored footsteps metal floor`、`industrial monster footsteps` |
| PUR-02 | 发现玩家 / 追逐开始提示 | 状态切入瞬间；0.5～1.2 秒 | 短促低频威胁提示，不使用长警报 | 可用轻微居中层，3D 主体仍来自追猎者 | `dark chase start stinger`、`enemy detection cue sci-fi` |
| PUR-03 | 近战起手 | 攻击开始；0.2～0.4 秒 | 握柄、装甲和衣物蓄力，让玩家知道攻击已开始 | 追猎者位置 3D；声纹固定 | `armored melee windup`、`axe grip movement` |
| PUR-04 | 斧击破风 | 约在 0.45 秒判定前通过玩家位置；0.15～0.35 秒 | 明确但不过分尖锐的重型斧刃破风 | 追猎者位置 3D；2～4 条变化 | `heavy axe swing whoosh`、`large weapon passby` |
| PUR-05 | 近战命中 | 只有 Sweep 真正命中时；0.3～0.8 秒 | 身体闷响、装甲碰撞和少量低频冲击 | 命中点 3D；至少轻/重两层 | `heavy melee body impact armor` |
| PUR-06 | 跑跳蓄力与离地 | 起手 0～0.65 秒；蓄力 0.4～0.65 秒，离地短音 0.15～0.35 秒 | 全身准备、地面蹬踏，提示玩家开始横向躲避 | 追猎者位置 3D；提示必须清楚 | `armored creature jump windup`、`heavy jump takeoff metal` |
| PUR-07 | 空中掠过与下砸落地 | 飞行约 0.65 秒；落地主音 0.5～1.2 秒 | 空中风压逐渐接近，落地为沉重冲击和短尾音 | 飞行声跟随追猎者；落地在真实 Landed 触发 | `heavy monster air whoosh`、`armored slam landing impact` |

### 3.2 磁力、投掷与物理碰撞

当前磁力吸取最短时长约 0.35 秒，实际时长随物体距离变化；正式投掷后的攻击性标记最多维持约 2.5 秒，但不应因此连续播放 2.5 秒的响亮投掷声。

| 编号 | 需要的声音 | 事件时间 / 建议素材长度 | 目标听感 | 空间与变化 | 推荐搜索词 |
|---|---|---|---|---|---|
| MAG-01 | 磁性目标选中 | 锁定目标瞬间；0.08～0.18 秒 | 简洁电子确认，不像菜单按钮 | 玩家侧非空间声音 | `sci-fi target lock short`、`magnetic object select UI` |
| MAG-02 | 吸取启动 | 从选中进入吸取；0.1～0.3 秒 | 电磁场启动和金属被牵引 | 物体位置 3D | `electromagnetic pull start`、`metal telekinesis start` |
| MAG-03 | 吸取循环 | 吸取期间循环；制作 1～2 秒无缝循环 | 受控电磁嗡鸣和轻微不稳定电弧 | 跟随物体；距离/质量可改变音高或低频 | `electromagnetic field seamless loop`、`telekinesis pull loop` |
| MAG-04 | 稳定持有确认与低强度循环 | 物体稳定到位；确认音 0.12～0.3 秒，循环保持克制 | 比吸取阶段稳定、低调，表明已经可以投掷 | 跟随物体；不能盖住机关预警 | `magnetic lock stabilized`、`energy tether idle loop` |
| MAG-05 | 普通释放 | 松开右键或安全中断；0.1～0.3 秒 | 能量断开、金属恢复自由状态 | 物体位置 3D | `energy tether release`、`magnetic disengage short` |
| MAG-06 | 正式投掷 | 左键投掷瞬间；0.2～0.5 秒 | 快速能量释放、重量感和方向明确的掠空 | 起点与物体位置 3D；2～4 条变化 | `heavy metal telekinesis throw`、`energy launch whoosh` |
| MAG-07 | 金属物碰撞 | 每次合格接触；0.15～0.8 秒 | 分为轻碰、正常撞击、重击；铁板和小物不能完全相同 | 命中点 3D；各强度至少 3～4 条变化 | `sheet metal impact concrete`、`heavy steel impact industrial` |
| MAG-08 | 投掷物破碎 | 正式投掷首次合格命中并破碎；0.6～1.5 秒 | 主撞击、金属裂解、较轻的碎片散落尾音 | 命中点 3D；限制碎片并发 | `metal fracture debris scatter`、`industrial object break apart` |

### 3.3 角色受击与倒地恢复

| 编号 | 需要的声音 | 事件时间 / 建议素材长度 | 目标听感 | 空间与变化 | 推荐搜索词 |
|---|---|---|---|---|---|
| HIT-01 | 站立轻受击 | Standing Impact 成功提交时；0.2～0.5 秒 | 身体闷响、装甲偏转、短促呼气；明显弱于重击 | 角色位置 3D；玩家可补很轻的居中层 | `light body impact armor`、`character stagger hit` |
| HIT-02 | 重击首次接触 | Heavy Impact 的首次真实 Chaos 接触；0.5～1.5 秒 | 大质量金属撞击和身体失衡，强调一次主冲击 | 接触点 3D；同一冲击不得重复主音 | `massive metal body impact`、`heavy ragdoll hit` |
| HIT-03 | 倒地翻滚小接触 | 物理身体后续明显接触；0.15～0.5 秒 | 布料、护甲小碰撞，远弱于首次主撞击 | 角色位置 3D；限制并发和最短间隔 | `armored body roll foley`、`ragdoll armor movement` |
| HIT-04 | 起身动作 | 起身动画阶段；约 0.8～2.5 秒，可拆成衣物/撑地短音 | 身体撑地、护甲摩擦和恢复呼吸 | 角色位置 3D；使用动画事件对齐 | `armored character get up foley` |

### 3.4 地刺

当前时序为：升起 0.4 秒、伸出停留 1.5 秒、隐藏停留 2.0 秒。

| 编号 | 需要的声音 | 触发与长度 | 目标听感 | 推荐搜索词 |
|---|---|---|---|---|
| SPI-01 | 升起机械声 | `StartRising`；约 0.4 秒 | 快速机械摩擦、导轨移动 | `mechanical spike trap rise`、`metal spikes extend` |
| SPI-02 | 完全伸出锁止 | `EnterExtended`；0.1～0.3 秒 | 清晰金属锁扣，确认危险相位开始 | `metal mechanism lock clack` |
| SPI-03 | 收起机械声 | `StartLowering`；约 0.4 秒 | 比升起稍低、更安全的缩回摩擦 | `mechanical spikes retract` |

当前地刺没有独立的提前预警阶段，因此声音最多只能随升起提供约 0.4 秒预警。如果试玩要求更长预警，应另行讨论玩法阶段，不能仅把声音提前播放到一个不存在的事件上。

### 3.5 自动周期冲锤

当前时序为：收回等待 1.8 秒、预警 0.7 秒、伸出 0.25 秒、缩回 0.8 秒。

| 编号 | 需要的声音 | 触发与长度 | 目标听感 | 推荐搜索词 |
|---|---|---|---|---|
| RAM-01 | 预警蓄压 | Warning 阶段；约 0.7 秒 | 液压/气动压力逐渐上升，建立可学习节奏 | `hydraulic piston warning charge`、`pneumatic pressure build` |
| RAM-02 | 快速伸出 | Extending 阶段；约 0.25 秒 | 快速气动释放和沉重机械移动 | `pneumatic ram extend fast` |
| RAM-03 | 锤头命中 | 真实接触；0.4～1.2 秒 | 大质量钢铁撞击，命中角色和墙体可有不同层 | `industrial battering ram impact`、`heavy steel slam` |
| RAM-04 | 缓慢缩回 | Retracting 阶段；约 0.8 秒 | 液压回流、链条或导轨摩擦 | `hydraulic piston retract` |
| RAM-05 | 完全收回锁止 | Finish Retraction；0.1～0.35 秒 | 机械卡榫落位，帮助玩家识别安全等待开始 | `industrial mechanism reset clunk` |

### 3.6 自由摆锤

摆锤由真实物理速度决定节奏，不使用固定秒数音轨。

| 编号 | 需要的声音 | 触发与长度 | 目标听感 | 推荐搜索词 |
|---|---|---|---|---|
| PEN-01 | 两端结构吱响 | 接近摆动端点；0.2～0.7 秒 | 轴承、约束和金属结构受力 | `large metal hinge creak`、`industrial pendulum strain` |
| PEN-02 | 最低点高速破风 | 高速经过中线；0.25～0.6 秒 | 大体积重物推动空气，速度越高越强 | `large object swing whoosh`、`heavy pendulum passby` |
| PEN-03 | 摆锤碰撞 | 真实接触；0.4～1.2 秒 | 千克级钢铁冲击、短促结构共鸣 | `massive steel pendulum impact` |

### 3.7 预判抛射发射器

当前预警阶段约 0.55 秒；弹体最长存活约 8 秒，但飞行声只应在弹体运动且玩家可听范围内存在。

| 编号 | 需要的声音 | 触发与长度 | 目标听感 | 推荐搜索词 |
|---|---|---|---|---|
| LAU-01 | 伺服瞄准 | 预警瞄准期间；可做 1 秒循环 | 金属炮架转向、伺服电机轻响 | `industrial turret servo aim` |
| LAU-02 | 发射预警充能 | Warning 阶段；约 0.55 秒 | 逐渐升高、结尾明确的危险提示 | `sci-fi launcher warning charge`、`electromagnetic cannon charge short` |
| LAU-03 | 离膛发射 | 发射瞬间；0.15～0.4 秒 | 压缩气体或电磁释放，不必像火药枪 | `pneumatic projectile launch`、`electromagnetic cannon fire` |
| LAU-04 | 弹体飞行与近身掠过 | 运动期间低强度循环；近身另播 0.15～0.4 秒 | 空气摩擦、旋转金属和近身方向感 | `heavy projectile flight loop`、`projectile passby whoosh` |
| LAU-05 | 弹体撞击 | 首次有效阻挡接触；0.4～1.2 秒 | 重型金属弹体撞击墙、地或角色 | `heavy metal projectile impact` |

### 3.8 出口、胜负与局流程

| 编号 | 需要的声音 | 触发与长度 | 目标听感 | 空间与变化 | 推荐搜索词 |
|---|---|---|---|---|---|
| FLW-01 | PCG 关卡准备完成 | 游戏真正可操作时；0.4～1.0 秒 | 设施供电恢复或系统就绪，不是胜利音 | 非空间提示或入口附近 3D | `sci-fi system online short` |
| FLW-02 | 出口激活 / 解锁 | Exit 可用时；1～2 秒 | 电力接通、机械门锁解除，给出明确目标方向 | 出口位置 3D，可补轻微居中提示 | `industrial blast door unlock`、`facility power restored` |
| FLW-03 | 进入出口 | 玩家满足胜利条件进入出口；0.4～1 秒 | 穿越、门禁确认或撤离确认 | 出口位置 3D | `sci-fi extraction confirm` |
| FLW-04 | 胜利结算 | 结算面板出现；2～4 秒 | 向上但克制的完成提示，保留工业基调 | 非空间声音 | `dark sci-fi victory stinger` |
| FLW-05 | 失败结算 | 生命耗尽或失败结算；2～4 秒 | 低沉收束，不使用过长悲壮音乐 | 非空间声音 | `industrial game over stinger`、`dark failure cue` |

## 4. P1：移动、界面和空间完整度

| 编号 | 需要的声音 | 建议长度 / 数量 | 目标听感 | 推荐搜索词 |
|---|---|---|---|---|
| PLY-01 | 玩家金属地面脚步 | 单条 0.1～0.3 秒；步行/跑动共 6～10 条变化 | 比追猎者更轻、更敏捷，避免混淆双方 | `boots footsteps metal floor game` |
| PLY-02 | 跳跃离地 | 0.1～0.3 秒 | 鞋底蹬地、少量衣物 | `character jump takeoff foley` |
| PLY-03 | 普通与重落地 | 0.2～0.8 秒；至少轻/重两级 | 帮助判断落地重量，不伪造伤害 | `boots landing metal floor` |
| UI-01 | 按钮悬停 | 0.05～0.12 秒 | 克制的科幻设备反馈 | `dark sci-fi UI hover` |
| UI-02 | 按钮确认 | 0.08～0.2 秒 | 清楚、短促，不像手机通知 | `industrial sci-fi UI confirm` |
| UI-03 | 无效输入 / 错误 | 0.15～0.35 秒 | 低音或断开的电子反馈 | `sci-fi UI error negative` |
| UI-04 | 打开 / 关闭暂停 | 0.1～0.3 秒 | 快速界面切换，不制造巨大转场 | `sci-fi menu open close` |
| AMB-01 | 工业设施基础底噪 | 45～90 秒无缝循环 | 远处机械、通风、电流和低频空间共鸣；保持稀疏 | `abandoned sci-fi industrial ambience seamless loop` |
| AMB-02 | 局部风扇 | 8～30 秒循环；准备大小至少两类 | 稳定机械声，按房间位置布置 | `industrial ventilation fan loop` |
| AMB-03 | 水管 / 蒸汽 / 泄压 | 5～20 秒循环或间歇短音 | 补充废弃设施生命感，不持续占满频段 | `metal pipe resonance`、`steam pressure release industrial` |
| AMB-04 | 电气设备 | 5～20 秒循环 | 变压器、电流、偶发火花 | `electrical transformer hum loop`、`electrical sparks intermittent` |
| AMB-05 | 远处机械随机事件 | 单条 1～4 秒；准备 5～8 条 | 远处金属落下、门、管道应力，不直接误导为近处机关 | `distant factory machinery random` |

## 5. P2：音乐与高阶变化

| 编号 | 需要的声音 | 建议长度 | 目标听感 | 推荐搜索词 |
|---|---|---|---|---|
| MUS-01 | 主菜单音乐 | 60～120 秒无缝循环 | 废弃科幻、低旋律密度、克制悬疑 | `dark sci-fi industrial menu music loop` |
| MUS-02 | 探索氛围层 | 60～120 秒无缝循环 | 接近环境声的低频 Drone，不遮挡脚步和机关 | `industrial horror ambient drone no melody` |
| MUS-03 | 追逐强度层 | 30～90 秒循环 | 可叠加脉冲和节奏，不切换成完全不同歌曲 | `industrial chase tension pulse loop` |
| MUS-04 | 低生命状态层 | 短脉冲或可控滤波层 | 只增强压力，不持续心跳轰炸 | `subtle low health tension pulse` |

第一轮不以音乐为前置条件。只有 P0 声音已经让玩家听懂追猎和机关后，才接入音乐并检查其遮蔽问题。

## 6. 推荐搜索顺序与素材来源

### 6.1 先试听项目内候选库存

以下资产名已在内容目录发现，但实际类型、许可、引用和播放状态仍需官方 MCP 恢复后回读：

- `/Game/Assets/SciFiHydroLab/Sounds/SC_Machinery_Fan_A`
- `/Game/Assets/SciFiHydroLab/Sounds/SC_Machinery_Fan_A_Low`
- `/Game/Assets/SciFiHydroLab/Sounds/SC_Machinery_Fan_B`
- `/Game/Assets/SciFiHydroLab/Sounds/SC_HydroLab_WindOutdoor`
- `/Game/Assets/SciFiHydroLab/Sounds/SA_WaterPipes`
- `/Game/Assets/SciFiHydroLab/Sounds/SC_ScifiDoor_A`
- `/Game/Assets/SciFiHydroLab/Sounds/SC_ScifiDoor_B`
- `/Game/Assets/SciFiHydroLab/Sounds/SC_Music`
- `/Game/AssortmentOfTraps/Traps/Sounds/8_SawTrap/SW_CircularSaw`

这些第三方资产只能作为试听候选；未经确认不修改原资产，也不把“工程里存在”视为“许可记录已经完备”。

### 6.2 外部素材平台

推荐顺序：

1. [Sonniss GameAudioGDC](https://sonniss.com/gameaudiogdc/)：适合先找真实机械、金属、脚步、碰撞和环境录音。官方说明可在商业媒体项目中使用、无需署名、允许编辑，但不能单独转售，也禁止用于 AI/ML 训练。
2. [Freesound](https://freesound.org/help/faq/)：优先筛选 CC0；使用 CC BY 时必须记录作者和署名；尽量不采用 CC BY-NC，以免未来商业展示或发布产生边界问题。
3. [Pixabay](https://pixabay.com/service/license-summary/)：可用于快速灰盒，允许免费使用、修改且通常无需署名，但不能将原始素材作为独立文件重新分发。
4. [Fab](https://www.fab.com/eula)：适合买一套统一的科幻工业音效包；Standard License 允许随项目使用和修改，不允许单独重新分发素材。

搜索时可组合以下限定词：

- `isolated`：只有目标声音。
- `dry` / `no reverb`：不带现成空间混响，方便在 UE 中统一处理。
- `one shot`：一次播放的短音。
- `seamless loop`：首尾可无缝循环。
- `multiple variations`：同类多种变化。
- `WAV 48kHz`：匹配项目当前 48 kHz 采样率。
- `mono`：适合追猎者、机关、物体等 3D 点声源。

### 6.3 授权记录模板

每个最终采用的外部素材都应记录：

| 字段 | 内容 |
|---|---|
| 项目内名称 | 最终导入 UE 的资产名 |
| 原始文件名 | 下载时的文件名 |
| 来源链接 | 直接素材页或商品页，不只写网站首页 |
| 作者 / 发行方 | 上传者或素材包发行方 |
| 许可证与版本 | CC0、CC BY 4.0、Fab Standard License 等 |
| 下载日期 | 许可证核对日期 |
| 允许范围 | 商用、修改、是否要求署名、是否禁止单独分发 |
| 修改记录 | 裁切、降噪、叠层、变调、转单声道等 |
| 使用位置 | 对应本文编号，例如 `RAM-01` |

“Royalty Free”只表示通常不按使用次数支付版税，不等于没有其他限制。正式采用时保存许可证页面或订单记录。

## 7. AI 生成建议

AI 可以用于本项目音效，但推荐和真实录音混合：

- 优先使用真实录音：脚步、金属碰撞、布料、身体落地、液压机械。
- 优先使用 AI：磁力嗡鸣、发射器充能、UI、远处机械、工业氛围 Drone。
- 最终把真实机械/撞击作为重量基础，再叠加 AI 生成的科幻电子层。

可用工具：

- [ElevenLabs Sound Effects](https://elevenlabs.io/docs/overview/capabilities/sound-effects)：支持指定时长、最长 30 秒和无缝循环。免费方案不包含商用许可；商业用途需按生成时的付费计划与条款核对。
- [Adobe Firefly Sound Effects](https://www.adobe.com/products/firefly/features/sound-effect-generator.html)：支持文字与声音节奏引导。正式发布版本可按 Adobe 当时的商业使用条款评估，仍需保存生成日期和条款记录。

生成规则：

1. 不把“预警—发射—飞行—命中”生成成一个长文件；必须拆为 Start、Loop、Stop/Release、Impact。
2. 可中断事件必须能在任意阶段停止循环声。
3. 频繁重复的声音一次生成 4～8 个变化。
4. 提示词写明时长、材质、空间、强度、是否循环、是否需要干声，以及明确排除项。
5. 保存最终提示词、工具名称、模型/版本、生成日期、账户计划和原始生成文件。

示例提示词：

```text
Seamless 20-second abandoned sci-fi industrial ventilation ambience,
subtle distant machinery, low electrical hum, tense but quiet,
no melody, no alarm, no voices.
```

```text
A 0.55-second sci-fi industrial launcher warning,
mechanical servo rising into a restrained electromagnetic charge,
clear danger cue, no gunshot, isolated, dry.
```

```text
A seamless electromagnetic field loop for holding a heavy metal plate,
low stable hum with slight unstable sparks, no melody, no impact.
```

```text
A short heavy steel plate impact on a concrete-and-metal floor,
powerful initial transient, realistic weight, isolated, dry, no explosion.
```

## 8. 文件交付规格与项目建议命名

UE 5.8 官方支持导入 WAV、OGG、FLAC、AIF、Opus 和 MP3 等格式，并在内部转换；本项目统一优先准备：

- 48 kHz、16-bit WAV。
- 追猎者、机关、投掷物和局部机器：单声道干声。
- 菜单、结算、音乐和全局环境底层：可使用立体声。
- 预留少量峰值空间，不允许削波；不在原文件中烘焙过重混响。
- 每个循环文件必须人工检查首尾，不只相信“AI 标记为 Loop”。

以下命名只是本项目建议，不是 UE 官方命名规则：

```text
SFX_Pursuer_Footstep_Metal_01.wav
SFX_Pursuer_Axe_Whoosh_01.wav
SFX_Magnet_Pull_Start.wav
SFX_Magnet_Pull_Loop.wav
SFX_Hazard_Ram_Warning.wav
SFX_Hazard_Ram_Impact_01.wav
SFX_Hazard_Launcher_Fire.wav
AMB_Industrial_Base_Loop.wav
UI_Confirm.wav
MUS_Exploration_Loop.wav
```

导入 UE 后，原始文件成为官方资产类型 Sound Wave。需要无缝循环、随机变化、按速度/质量调制或接收玩法参数时，再评估使用 MetaSound；不为所有一次性短音强行建立复杂图。

## 9. 第一轮灰盒验收

第一轮不要求最终音质，但必须满足：

1. 玩家背对追猎者时，能区分脚步接近、近战起手和跑跳下砸。
2. 近战落空没有命中声；真正命中时声音与判定帧一致。
3. 地刺、冲锤和发射器的预警声都能对应各自真实危险窗口。
4. 摆锤的破风强度跟随真实速度，不使用固定节奏假装物理结果。
5. 磁力吸取、稳定到位、普通释放和正式投掷可以只凭声音区分。
6. 轻碰、正常撞击、重击和破碎不会全部使用同一个音效。
7. 追猎者或机关隔墙时仍可判断大致方向，但不会像没有墙一样清晰。
8. 胜利与失败音只在结算入口播放一次。
9. 暂停、重开、切关卡和对象销毁后不存在残留循环声。
10. 连续游玩至少 5 分钟后，脚步和撞击的重复感仍可接受。

验收通过后，再加入环境细节和音乐，并重新检查音乐是否遮住追猎者、机关和磁力信息。

## 10. 参考资料

- [Unreal Engine 5.8：Importing Audio Files](https://dev.epicgames.com/documentation/unreal-engine/importing-audio-files?lang=en-US)
- [Unreal Engine 5.8：MetaSounds](https://dev.epicgames.com/documentation/unreal-engine/metasounds-the-next-generation-sound-sources-in-unreal-engine?lang=en-US)
- [Sonniss GameAudioGDC Archive](https://sonniss.com/gameaudiogdc/)
- [Freesound License FAQ](https://freesound.org/help/faq/)
- [Pixabay Content License Summary](https://pixabay.com/service/license-summary/)
- [Fab Standard License](https://www.fab.com/eula)
- [ElevenLabs Sound Effects Documentation](https://elevenlabs.io/docs/overview/capabilities/sound-effects)
- [Adobe Firefly Sound Effects](https://www.adobe.com/products/firefly/features/sound-effect-generator.html)
