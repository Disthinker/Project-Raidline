# Project Raidline 当前状态

最后核对：2026-08-27。

## Git 与交付基线

- `origin/main@dc19745` 已包含完整 Core Extraction Alpha、Survival Loadout、Combat、Raid Pressure、Base Growth、区域地图情报、Raid World Vertical Slice、可扩展性能基础、Regional Operations 失物/寻回闭环，以及已接受的区域路线与轻量哨所基础。
- 当前开发分支：`codex/regional-outpost-disruption-restoration`，从 `origin/main@dc19745` 创建。
- 当前活动计划：`doc/exec-plans/active/regional-outpost-disruption-restoration-v1.md`。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main；正式 Grab/Scratch/Bite 图像及所有新正式美术生产继续暂停。用户于 2026-08-21 仅授权当前 ArtWorkbench P0 音效包接入。

## 当前产品里程碑

Core Extraction Alpha、Survival Loadout、Combat、Raid Pressure、Base Growth、区域地图情报、Raid World Vertical Slice、可扩展性能基础、**Regional Operations — Loss & Recovery** 和区域路线/轻量哨所基础均已进入主线。当前宏切片让 Online 哨所随已结算的捷径行动累积威胁，在阈值后中断，并通过对应清剿 Raid 成功撤离恢复；人工验收前不合并。

1. **Persistent Base**：PR #58 已合入，Profile/AssetRegistry、可行走 Base、Stash/三槽配装、固定经济/救济、schema v1 与跨进程恢复成为接受基线。
2. **Extraction Loop**：PR #59 已通过本地自动化、exact-head CI 与用户 7/7 集中真实窗口验收，并以 merge commit `ed45baa` 进入 main。
3. **Alpha Hardening**：PR #60 已以 merge commit `50849d5` 进入 main；本地 645/645、精确 head CI 与用户最终正常游玩验收通过。
4. **基础防具与命中部位**：PR #61 已由用户正常游玩验收，并以 merge commit `733b597` 进入 main。
5. **流血、疼痛与战地医疗**：PR #62 已通过 exact-head CI 和用户正常游玩验收，并以 merge commit `ea918ab` 进入 main。
6. **武器耐久、故障与维护**：PR #63 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `b8ddbe3` 进入 main。
7. **多武器配装与切换**：PR #64 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以 merge commit `4c16596` 进入 main。
8. **防具维护**：PR #65 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以 merge commit `755fa00` 进入 main。
9. **逻辑弹道与落点反馈 v1**：PR #66 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以 merge commit `7877d71` 进入 main。
10. **准星运动、逻辑弹道与开发调参 v1**：PR #67 已通过用户验收并以 merge commit `881c034` 进入 main。
11. **输入捕获、后坐力曲线与 P0 音频 v1**：PR #68 已通过 exact-head CI 与用户验收，以 merge commit `ba3375e` 进入 main。
12. **直接瞄准、距离散布与高速曳光 v2**：PR #69 实现常规瞄准同帧直跟、准星移动/距离散布、内容弹速、超有效射程投影与纯短线曳光；验收加固进一步修复 Raid 装备拖放，分离真实随机散布半径与玩家可读准星半径，加粗准星/曳光，加入 Base/Raid 统一 Esc 暂停菜单，并把默认曳光长度收敛为 30px。精确 head CI 与用户正常游玩验收通过，以 merge commit `f593719` 进入 main。
13. **动态散布模型与准星稳定性 v3**：PR #71 完成距离包络、四源 Bloom、走跑即时扩散、连续横向后坐力与准星—真实随机弹道权威半径统一；精确 head CI 和用户正常游玩验收通过，以 merge commit `33da892` 进入 main。
14. **射击表现收尾**：PR #72 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `795b644` 进入 main。
15. **固定地图差异化 v1**：PR #73 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `a32c476` 进入 main。
16. **武器切换准星连续性**：PR #74 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `6138da8` 进入 main。
17. **持续高危阶段 v1**：PR #75 已通过 exact-head Windows/Ubuntu CI 与用户正常游玩验收，以 merge commit `773443b` 进入 main。
18. **主动高危与高级资源区 v1**：PR #76 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以 merge commit `bc26337` 进入 main。
19. **高危条件撤离 v1**：PR #77 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `d106193` 进入 main。
20. **Base 资源分配与基础需求 v1（历史）**：PR #78 把成功带回的新 Loot 放入独立待分配区并已以 `ba8283f` 进入 main；用户于 2026-08-25 明确废弃该返还行为，PR #87 已迁移为成功撤离保持原位置，BaseIntake 只兼容旧档。
21. **Base 世界时钟与每日需求 v1**：PR #79 建立 Base/Raid 共享的权威分钟时钟，把四项需求迁移到每日 00:00 幂等结算；暂停、模态页、主菜单、结果页和离线时间不推进，未结算 Raid 的时间随出击前存档回滚。已通过 exact-head CI 和用户正常游玩验收，以 merge commit `5d2a11a` 进入 main。
22. **Raid 往返行动耗时 v1**：PR #80 为三图增加版本化出发/正常返程/失败归队时间，出击前显示抵达昼夜预览；旅行、有效 Raid 时间与跨日需求作为同一活动事务提交，异常退出精确回到出发前时钟和资源。已通过 exact-head CI 和用户正常游玩验收，以 merge commit `defaac0` 进入 main。
23. **Base 枪匠全面维护服务 v1**：PR #81 已通过 exact-head CI 与用户正常游玩验收，并以 merge commit `ace7c69` 进入 main。
24. **Base 周期愿望与物资提交 v1**：PR #82 已通过 exact-head Windows/Ubuntu CI 与用户正常游玩验收，以 merge commit `eca7d62` 进入 main。
25. **Base 运营状态与即时枪械维护 v1**：PR #83 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `20d9f48` 进入 main。
26. **Base 付费医疗服务 v1**：PR #84 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `c01d431` 进入 main。玩家只支付货币即可立即恢复生命并清除流血，不消耗个人医疗物、不推进世界时间。
27. **Base 居民、床位与睡眠 v1**：PR #85 新增宿舍设施、8 名普通居民/10 个床位的迁移默认值、按人口计算的每日口粮和最多 12 小时的原子休息事务；已通过 CI 和用户验收并以 `2377035` 合入 main。
28. **Raid 普通幸存者安全转移 v1**：PR #86 为三张固定图加入一次性普通幸存者点；连续按住 F 2 秒后立即幂等接纳，后续死亡、主动退出或异常关闭均保留该人口事实，其余 Raid 状态仍回滚。已通过 CI 与用户正常游玩验收并以 `ee9ba48` 合入 main。
29. **Base 宿舍扩建与分类自动供给 v1**：PR #87 允许从统一自有资产显式加工建材并完成宿舍 1→2 级项目；成功撤离保持全部随身物原位置，食物/医疗/娱乐/安全菜单按物品定义保存自动供给授权，只有每日需求不足时才消费。已通过 exact-head CI 和用户正常游玩验收，以 `1be94bf` 进入 main。
30. **Base 居民伤病与医疗所治疗 v1**：PR #88 让 Ashworks 救援接纳一名受伤普通居民；医疗所从玩家明确授权为医疗供给的基地可访问自有物品中预览并原子消费准确数量，经过权威世界时间后恢复居民。玩家付费医疗仍是独立的货币即时服务。已通过 exact-head CI 与用户正常游玩验收，以 `987dc6b` 进入 main。
31. **Base 基础制造队列 v1**：PR #89 为损坏工坊提供一个生产槽；以 1 个废旧零件、1 个损坏电子元件和 1 名健康劳动力经过 6 小时制造一件真实武器维护包。已通过 exact-head CI 与用户正常游玩验收，以普通 merge commit `194f910` 进入 main。
32. **Base 正式士气与周期事件 v1**：PR #90 已通过 exact-head CI 与用户正常游玩验收，以普通 merge commit `1af0e56` 进入 main。
33. **Base 聚合岗位、专业人口与设施升级 v1**：PR #91 已通过 exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `12a2fa6` 进入 main。
34. **区域地图与对局情报 v1**：PR #92 已通过 exact-head Windows/Ubuntu CI，并按用户连续交付授权以普通 merge commit `bf8baf3` 进入 main。三图出击板提供难度警告、交通图/物资清单/敌情档案购买与选择，部署原子消耗并冻结权限；Raid 中 `M` 打开不暂停战术地图，显示本局探索迷雾、已发现撤离点及所选粗粒度情报。用户正常游玩验收留待后续集中进行。
35. **程序化室外空间基础 v1**：PR #93 已通过本地 1033/1033、exact-head Windows/Ubuntu CI 和用户集中验收，以普通 merge commit `1404b41` 进入 main。第四图 `Frontier Exchange` 使用稳定 PCG32 生成室外掩体并冻结 schema v21/content v29 布局、连通性与确定性回退合同。
36. **独立室内空间 v1**：PR #94 已通过本地 1039/1039、exact-head Windows/Ubuntu CI 和用户集中验收，以普通 merge commit `62ebd8a` 进入 main。`Frontier Exchange` 交换站办公室使用稳定空间 ID、独立尺寸/障碍/敌人/Loot 和 F 入口/返回 Socket；占位表现仅使用双语文字与代码几何。
37. **特殊地点随机合法放置 v1**：PR #95 已通过本地 1043/1043、exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `d2ceb59` 进入 main。content v31 为交换站办公室声明 6 个候选位置，Deploy 以独立 PCG32 流过滤本局动态冲突并确定性选择，最终坐标继续冻结在 schema v22。
38. **特殊地点发现与战术地图投影 v1**：PR #96 已通过本地 1048/1048、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `de3402c` 进入 main。玩家接近入口约 145 世界单位后才在本局显示精确世界入口和 `M` 地图特殊地点标记；同帧进入会先记录发现。
39. **建筑内部图永久情报 v1**：PR #97 已通过本地 1058/1058、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `a7b3cc2` 进入 main。content v32 为交换站办公室声明 180 货币的一次性内部图，schema v23 保存永久空间 ID 授权并冻结到 pending Raid。
40. **空间战术可靠性 v1**：PR #98 已通过 Windows Debug、1072/1072 CTest、exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `95fcd23` 进入 main。当前空间 LOS、最后已知位置、确定性绕障、贴墙合法接近点、成功实弹击发枪声刺激和近战遮挡均已接受。
41. **第二个代表性地点 v1**：content v33 在 `Frontier Exchange` 增加独立 `Freight Service Bay`，rules v15 冻结两个地点并保留 v12～v14 首办公室 pending Raid 兼容。多入口投影、返回点可达锚点、独立 Actor/Loot/内部图和双语代码占位均已通过用户正常游玩验收；多人追击热点修复也完成复验。PR #99 已以普通 merge commit `1d2fea1` 进入 main。
42. **Raid World 可扩展性能基础 v1**：PR #100 已通过 Windows Debug、1097/1097 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `d7c231b` 进入 main。公平轮转、敌人近邻格、每空间静态障碍索引、双导航后端、结构化压力门槛和双语 F9 性能面板成为接受基线。
43. **多敌人追击与攻击意图隔离**：PR #102 已通过 Windows Debug、233/233 定向回归、1108/1108 完整 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `1c62064` 进入 main。最多 10 名敌人可同时攻击，超额成员继续施压并稳定轮转；0.25 秒受伤保护避免同一瞬间连续跳血。
44. **失物记录与行动老化 v1**：PR #101 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `7185d55` 进入 main。死亡/主动退出迁移携带资产树，异常退出继续回滚，schema v24/content v34 保存独立记录、老化窗口与 RaidResult 关联。Base Raid Gate 提供双语代码占位记录页和出击前到期二次确认。
45. **NPC 寻回任务与 Raid 自行寻回 v1**：PR #103 已通过 exact-head CI 和用户统一正常游玩验收，以普通 merge commit `f31d91a` 进入 main。schema v26/content v35 支持整单 NPC 委托与来源地图内自力寻回，两条路径共享唯一资产所有权。
46. **区域路线与轻量哨所基础**：PR #104 已通过 exact-head CI 与用户统一正常游玩验收，以普通 merge commit `dc19745` 进入 main。content v36/schema v27 建立稳定区域节点、路线和哨所 ID；满员 Old Service Relay 把 Riverside/Industrial/Frontier 单程最短时间从 90/150/210 分钟降为 55/95/125 分钟。
47. **哨所中断与恢复 v1（当前开发）**：content v37/schema v28 数据化 3 次安全捷径行动阈值和 Riverside 清剿地图。只有冻结路线实际使用哨所且正式 Settlement 完成才增长一次威胁；阈值结算后下一局关闭捷径。清剿 Raid 消灭全部初始敌人前锁定所有撤离，完成并成功撤离才原子恢复；失败保留中断，异常退出恢复出击前状态。区域页和 Raid HUD 使用中英文文字/几何投影。

每个宏切片内部按领域、服务、客户端和证据形成可回滚提交，但不再为单个技术边界中断玩家功能交付。人工验证统一放在自动化和 CI 之后，由用户执行。

## 已接受能力

- `MainMenu → Base → Raid → RaidResult → Base` V0 流程和进程内多局会话。
- RL-INV-001/002/003：原子交换、Ctrl/Shift 锁定数量拖拽、同定义堆叠合并与 60 发上限。
- `ShotCommand → ShotResolution → HitResult` 窄边界；生产路径已使用非场景实体的 `LogicalBallisticFlight`，冻结本发并连续扫掠目标。
- 四个生产库、唯一业务源码编译所有权、强类型 DefinitionId、版本化 JSON ContentRegistry 和仓库本地 nlohmann-json overlay。
- Persistent Base 的长期 Profile、唯一 AssetRegistry、Stash/三槽配装、固定经济/救济、可行走三设施 Base、首次环境目标链和原子存档。

## 已接受的 Extraction Loop

- `ProfileState::AssetRegistry` 在 Base、Deploy、Raid Loot 与 Settlement 全程唯一拥有资产；装备根、容器子资产、已安装弹匣和 Raid 地面位置均使用稳定实例 ID。
- content v14 提供三张可选择的固定 Raid 地图；每图各有 3 组出生/撤离配对、3 组 4～6 敌人部署、10 个三路线 Loot 插槽、独立障碍和冻结的往返行动耗时。每局冻结 6～9 个有效 Loot，PCG32 命名随机流和所选 MapDefinitionId 写入 pending Raid 快照。
- schema v2 保存当前 HP、弹匣有序弹药、枪膛、Settlement 幂等记录和最近 RaidResult，并能读取旧 pending Raid；新生产 Deploy 不再把运行中 pending Raid 覆盖到磁盘。schema v1 可显式迁移。
- Base 与 Raid 共用按住拖拽库存交互；格子移动/交换/堆叠/配装均由领域预览和命令提交。Base 可将弹药拖到弹匣即时压弹、将弹匣拖到武器安装并按条件自动上膛；Raid 可拖动弹药到弹匣执行 0.2 秒/发的可中断压弹，也可拖动指定弹匣到武器并执行 2 秒换弹。
- 卸弹、显式上膛和 Medkit 使用物品右键情境菜单，不再依赖 `FILL MAG / INSTALL / CHAMBER / USE MED` 等验收按钮。Base 卸弹即时回到 Stash；Raid 弹匣卸弹为 3 秒可中断动作，完成时原子写入背包或胸挂通用格。`F`/`Ctrl+右键` 保留为 Base 快速转移捷径；可穿戴物在对应栏位为空且领域查询合法时优先快速装备，否则沿用容器转移。
- 玩家为 100 HP；Medkit 每件 3 次、每次恢复最多 30 HP，Raid 内治疗 5 秒且中断不消耗。
- 生产 Raid 的常规阶段为 180 秒；归零只进入无终局倒计时的持续高危，不产生时间失败。E 拾取真实 Loot，随身库存可移动和整理，打开时禁止射击/换弹/开始治疗但允许普通移动。
- 3 秒撤离成功保留合法随身资产、HP 以及每件资产的精确装备槽/容器格位；未来载具货物沿用同一位置保持合同。死亡和主动放弃仍是当前出击全损并恢复 100 HP，但当前分支不再删除实例，而是把携带树根迁移到不可直接使用的独立失物记录。关闭程序或异常退出不会结算、不会创建/老化记录，重开后加载出击前的完整 Profile；正式成功/失败结果仍使用唯一 Settlement ID 幂等提交。
- Raid 世界支持 Shift 奔跑，速度为普通移动的 1.5 倍；当前不引入耐力条、负重或复杂移动消耗。
- RaidResult 显示结果、成功带回物和货币变化；失败只提示失物记录已建立，详细资产在 Base 独立记录页查看。生产 Alpha 路径不再使用 V0 柜体、无限弹或 Timeout 结算。

## 当前自动化证据

- 当前多敌人攻击意图修复已完成 Windows Debug 全目标、233/233 定向回归和 1108/1108 完整 CTest；32 敌人压力约 119 ms、最慢约 1.45 ms，100 敌人压力约 172 ms、最慢约 1.96 ms。开发代理未启动游戏。
- Windows Debug 当前树全目标构建成功，`Project_Raidline.exe` 已生成但未由开发代理启动。
- PR #89 已通过 exact-head Windows/Ubuntu CI 与用户正常游玩验收，并以普通 merge commit `194f910` 进入 main。
- PR #90～#104 均已进入 main。当前哨所中断/恢复分支已完成三个玩家闭环步骤；领域、服务、Simulation、存档、客户端和双语定向回归 209/209、Windows Debug 全目标与完整 1161/1161 CTest 通过，exact-head CI 待运行，开发代理未启动游戏。
- ProfileCombatDomain、ContentRegistry、SaveRepository、HitResolution、GameplayWorld、InventoryDomain、RaidLifecycle 与 AlphaExtractionSession focused 通过。
- PR #61 的 Windows Debug 全目标、663/663 CTest、exact-head Windows/Ubuntu CI 和用户正常游玩验收均通过。
- PR #62 的医疗切片 Windows Debug、680/680 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收均已通过。
- 新长序列自动化覆盖 10 次混合成功/失败 Raid、至少 3 次跨进程重载、三组出生/撤离、三组敌人部署、三路线 Loot、重复 Settlement 和保存失败阻断。
- PR #63 的最终 exact-head Windows/Ubuntu CI 与用户正常游玩验收已通过并合入 main。
- PR #64 的最终 exact-head Windows/Ubuntu CI 与用户正常游玩验收已通过并以 `4c16596` 合入 main。
- PR #65 的防具维护 Windows Debug 全目标、718/718 CTest、exact-head Windows/Ubuntu CI 和用户正常游玩验收均已通过并合入。
- PR #66 的逻辑弹道切片已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，并以 `7877d71` 合入 main。
- PR #67～#71 的射击手感切片均已通过对应 CI 与用户正常游玩验收。PR #72 的 Windows Debug 全目标、79 项定向回归、788/788 CTest、exact-head CI 与用户验收均已完成，并以 `795b644` 进入 main。当前固定地图分支已完成 Windows Debug 全目标编译和 793/793 全量 CTest；开发代理未启动游戏。
- PR #73 的 Windows Debug 全目标、793/793 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收均已完成，并以 `a32c476` 进入 main。PR #74 通过 795/795、exact-head CI 与用户验收后以 `6138da8` 进入 main。PR #75 已完成 Windows Debug 全目标、807/807 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，并以 `773443b` 进入 main。当前主动高危切片相关定向回归、Windows Debug 全目标和完整 CTest 814/814 通过；开发代理未启动游戏。

## Raid 持续高危阶段 v1 当前实现

- `RaidSession` 唯一拥有常规/高危阶段、普通/信号撤离路线、一次普通撤离宽限和无终局倒计时状态；时间归零不能产生 Settlement 失败。
- content v10 为三张固定图定义 180 秒常规阶段、12 秒信号撤离、压力出生点、首波/间隔、单波数量与活动敌人上限；schema v6 显式兼容读取 v9 存档。
- `GameplayWorld` 按冻结 map ID 与 seed 稳定轮转出生点，跳过玩家附近、活敌占用或障碍位置；新敌人使用单调非零 `CombatTargetId`，同时存活上限为 8。
- App 只读显示 30 秒预警、普通撤离关闭、持续高危、信号撤离进度和当前压力；所有区域当前仍为代码 fallback，不修改资源 manifest。

## Raid 主动高危与高级资源区 v1 当前实现

- `RaidSession::triggerHighRisk()` 是主动切换的唯一权威入口；每图一个控制地标要求持续按住 F 4 秒，松开、离区、受伤、被控制或打开模态界面会清空进度。
- content v11 为三图定义控制地标、一个高级资源区、两个冻结插槽和独立高级 Loot 表；普通 Loot 仍保持 6～9 个且不受新增随机流影响。
- `RaidLootSnapshot::requiresHighRisk` 随 schema v6 往返并进入 Profile 指纹。高级资产在 Deploy 时已经获得稳定 ID，常规阶段不可见不可拾取，自然或主动进入高危后只解除访问限制。
- SDL client 只读绘制控制点、读条和资源区锁定/开放 fallback；没有新增或修改正式美术、音频与资源 manifest。

## Combat 武器切换准星连续性当前实现

- 武器切换仍重建新武器的 `WeaponFireState`，因此射击冷却和动态散布瞬态不会跨武器泄漏。
- `WeaponAimState` 改为原位重配置武器参数，保留实际准星世界位置、相对输入锚点、控制速度与有界后坐力运动。
- 该修复不改变 Profile、存档 schema、内容版本、输入键位、射击参数、命中解析、音频或资源 manifest。

## Combat 逻辑弹道与落点反馈 v1 当前实现

- `ShotCommand` 新增最大飞行距离；`ShotResolution` 在成功击发时冻结规范化方向、速度、最大距离和最终落点，后续鼠标或角色移动不能修改本发。
- `GameplayWorld` 不再创建可渲染/可碰撞的 Projectile 场景对象；`LogicalBallisticFlight` 只保存本发冻结值和已飞距离，不具有资产、场景或存档身份。
- 弹道速度由版本化 WeaponUse 内容定义；当前 Pistol/Rifle 分别为 4200/7200 世界单位/秒。命中解析仍连续扫掠本帧已飞线段并选择最近候选，高速大帧不会因离散采样穿过薄目标。
- PR #67 将冻结终点修订为武器最大射程或世界边界，而不是准星落点；弹道选择已飞区段内最近敌人或数据化 BallisticBlocker，未接触时到达最大距离形成一个 `Ground HitResult`。App 的弱曳光钳制在已经飞过的区段，不能提前显示未来路径。
- 普通命中、Obstacle 与 Ground 命中不显示准星 X；爆头/弱点继续只由领域 `HitSemantic` 触发专用标记。没有生成、发布或接入新美术/音频，也未修改 manifest。

## Combat 准星运动、逻辑弹道与开发调参 v1 当前实现

- `WeaponAimState` 保存实际准星世界位置、输入锚点、可推移控制目标、玩家控制速度和后坐力速度。当前腰射和按住鼠标右键的瞄准状态都以 `Direct` 模式同帧消费相对鼠标位移；未来合法高倍镜才切换为 `HighMagnificationInertial` 速度/加速度追赶。玩家朝向、准星表现和成功击发都消费同一个实际准星，而不是原始鼠标点。
- 击发按“枪口到实际准星”方向刷新一份有界后坐力初速度并加入少量 PCG32 横向偏转；再次击发替换旧后坐力速度而非叠加，随后按人机工效派生反向加速度让速度减到零。后坐力同步推移准星和控制目标，鼠标静止时不会自动回正，必须反向移动鼠标压枪。
- PR #67 接受基线中的 Pistol/Rifle 后坐力控制为 55/42。当前切片按用户调参反馈将两者人机工效提高到 100、隐藏最大准星速度提高到面板上限 5000 像素/秒，并显著提高控制加速度映射；运行时仍可通过 F10 面板按武器实例调整。
- 实际准星只确定总体射击方向；每发使用确定性 PCG32 在当前权威散布内偏移并冻结最终方向。精准度控制最小散布，稳定性控制最大散布及射击/快速移准增长，操控速度控制停火收缩；最小/最大包络随准星距离平滑增长。v3 中距离主要定义上下文包络，并以默认 10% 的有效射程 Bloom 小幅直接贡献当前扩散；移动、快速甩动与击发分别形成独立有界 Bloom。
- 按住鼠标右键进入当前瞄准状态；它降低移动速度并改善最小/最大散布。当前尚无瞄具倍率内容，因此不把这个输入另行解释为“基础 ADS”；它仍显示短、快、弱且只覆盖已飞区段的曳光。高倍 `None` 策略已建立，但等待合法瞄具消费者。换弹保持右键瞄准输入，并把散布锁定到当前瞄准状态的最大值。
- 奔跑不能直接击发；奔跑中按射击会先结束奔跑，经过由操控速度决定的短促举枪准备后只提交一次原射击意图，准备完成前不消耗弹药。
- 超过有效射程后准星立即以领域投影标红，散布和伤害质量继续平滑恶化，到最大射程只保留 25% 基础伤害。五项属性、基础伤害、射程和逻辑弹速来自版本化 WeaponUse 内容定义，App 不按名称猜测。
- Alpha 首图从 JSON 读取三个代码表现障碍；玩家和逻辑弹道不能穿过，内容加载拒绝越界、重复 ID 或与任一合法敌人部署重叠的障碍。敌人移动阻挡和正式墙/车辆视觉仍不是通用物理系统。
- Raid 中按 `F10` 打开开发者武器面板，使用上下选择、左右微调、Shift 大步长、R 重置。面板覆盖按当前武器实例隔离，立即重配置射击瞬态，但不修改 ContentRegistry、Profile、revision、存档、结算或 manifest；关闭进程即清除。
- F10 在原有参数外新增逻辑弹速、移准扩散率、近距散布比例、有效射程 Distance Bloom 与弱曳光寿命；准星 UI 直接消费 simulation 的当前/最小/最大角散布和世界半径投影。
- App 不再绘制移动弹头矩形、光球或余烬；每个 Weak 曳光是等宽五像素的亮黄外沿与白亮核心连续短线，并且只覆盖逻辑弹道已经推进过的区段。验收后的默认曳光长度为 30px，F10 仍可会话内调整；准星使用三像素粗、15 像素长的四臂表现。

## Combat 动态散布模型与准星稳定性 v3 当前实现

- `WeaponFireState` 分别保存移动、移准、击发和距离 Bloom。四种贡献通过 `1 - Π(1-bloom)` 饱和合成，既能同时生效，也不会无界相加；距离在有效射程默认贡献 0.10，仍主要通过包络表达射程关系。
- 移准 Bloom 不使用非零激活下限；单帧相对鼠标速度经过 120～1800 px/s 连续软阈值，持续快速甩枪按 `24-0.08×stability` 派生速率展开。走路目标为 0.75 且首帧至少进入其 80%，奔跑目标为 0.95 且首帧至少进入其 95%，随后以 4.0 fraction/s 补齐。操控速度派生恢复采用 `3+0.15×handling`。击发 Bloom 按 `spreadPerShot / baseEnvelope` 增长并保留既有恢复延迟。
- 默认横向后坐力比例由 0.30 提升到 0.55，最大目标偏角由 60° 提升到 70°，弯曲时间由 80ms 调整为 60ms；它仍从径向初速连续弯曲，不发生单帧横向跳点，也不自动回正。
- 原先 `WeaponAccuracyProjection` 把最多约 70px 动态增量只用于可读显示，导致大准星下随机弹道仍贴近中心。现在该增量由 `WeaponFireState::spreadRadiusAtDistance` 纳入 simulation 权威半径；随机射线和 `worldRadius` 共用该值，准星四臂只额外保留固定 10px 中心留白。App 不再把半径截断到 160px。
- F10 新增 `Sprinting spread fraction`，与 `Moving spread fraction` 分开调整；两者只覆盖当前武器实例的会话瞬态，不修改内容定义或存档。
- 该切片不修改武器内容版本、Profile schema、弹药、逻辑弹道、命中语义、音频或资源 manifest。

## Combat 射击表现收尾当前实现

- `ShotFeedbackPresentationState` 只接受已经解析成功的 ShotResolution，并以稳定 ShotId 保存最多八个短寿命表现记录；无弹、故障拒绝和冲刺阻断均不创建反馈。
- 每发提供约 0.05 秒代码绘制枪口焰、0.22 秒且最大 18% 不透明度的三个小烟团，以及中心 10% alpha、92px 半径、外缘透明的柔边暖色局部闪光。它们不进入正式照明、敌人感知或美术资源系统。
- 归一化抖动由 simulation 有界输出，SDL client 只把它映射为最大约 2px 的世界 viewport 偏移；准星、库存、HUD、暂停菜单与开发面板在恢复 viewport 后绘制，实际瞄准和命中不变。
- 本切片不制作角色或枪械射击动画，不生成/发布正式资源，不修改内容版本、Profile schema、音频或 manifest。

## PR #69 第二轮验收加固

- pending Raid 的 `carriedRootAssetIds` 现在约束“仍在随身所有权树”，不再错误要求根资产永久停留在原装备槽；Raid 内可通过共用拖放规则移动、卸下和重新装备武器，拒绝操作继续保持原子不变。
- Base 与 Active Raid 空闲时按 Esc 打开统一暂停菜单并冻结世界，提供继续、设置、退出到主菜单和退出到桌面；库存、情境菜单、设施、医疗轮盘与 F10 面板仍优先消费 Esc。暂停时释放相对鼠标捕获；Raid 返回主菜单不结算，Continue 复用既有跨进程回滚合同恢复出击前 Profile。

## Combat 输入捕获、后坐力曲线与 P0 音频 v1 当前实现

- Active Raid 使用 SDL 相对鼠标模式，将每帧 `xrel/yrel` 作为明确 `aimMotionDelta` 交给 simulation；准星不再依赖可移出窗口的 OS 光标坐标。库存、医疗轮盘、F10 面板、终局或失焦都会释放捕获并恢复系统光标。
- 后坐力横向随机从“径向速度加侧向速度”改为即时径向初速后在短弯曲时间内转向有界随机角度；横向比例只改变目标偏角，不再造成单帧斜向跳点。连续开火刷新一段运动，不叠加无界冲量，也不自动回正。
- F10 新增准星控制加速度和后坐力弯曲时间；最大准星速度默认值与上限均提高到 5000 像素/秒，便于把剩余延迟集中由加速度控制。
- SDL client 使用 `GameAudioOutput` 加载 `assets/audio/v1/sound_events.json`，将成功击发、Enemy/Obstacle/Ground `HitResult`、感染者警觉、玩家受伤和 GameSession 换弹/医疗/清障/拾取语义事实映射为稳定 Sound Event。运行时 WAV 统一为 48 kHz、16-bit、mono；事件定义集中控制变体、增益、并发、冷却与循环，总音量由 bank master gain 控制。
- 当前精选包来自 ArtWorkbench 的 `freeweaponsounds.zip` 与 Sonniss GDC 2026 五卷中的少量素材；只提交 43 个处理后的 WAV（合计约 7 MB）、来源清单和可复现脚本，不提交源 ZIP。Base 已将不合适的灯泡/线圈电流拾音替换为经过 90～3200 Hz 收束、低响度处理的室内烟囱风声，事件增益为 0.28；自动化对 Base 循环的过零率设置上限，防止尖锐电流噪声回归。
- SDL client 在打开设备前请求 512 sample-frame 缓冲；48 kHz 下游戏侧目标约为 10.7 ms，但 SDL/平台可以调整或忽略该请求，远程桌面音频重定向仍会叠加编码、网络和客户端缓冲。音频设备或 bank 加载失败时游戏静默降级。
- 本授权和实现都不包含 PNG、美术 manifest、正式攻击动画、霰弹枪、消音枪、广播、车辆或 P1 环境细分。

## Survival Loadout 防具维护切片当前实现

- content v5 新增基础甲修包与类型化 ArmorMaintenance；容量 50.00 点，占 `1x2`。基础头盔为复合材料、基础护甲为软质材料，每恢复 1 点分别消耗 1.50/1.00 维修点；金属材料合同预留 2.00 点且已有领域覆盖。
- `queryArmorMaintenance` 同时计划实际恢复、点数消耗、当前最大耐久变化和动作时长；`executeArmorMaintenance` 在候选 Profile 中原子提交，失败不改变 revision、指纹或稳定 ID 高水位。
- Base 可将甲修包拖到任意合法自有防具即时维修；Raid 可维修装备槽或随身容器中的防具，关闭库存后执行六秒动作。期间可按基础速度的 45% 缓慢移动；战斗/冲刺/受伤/库存/控制中断且零修复零消耗。该移动规则是用户对外部只读 GDD 原地维修描述的最新修订。
- 固定供应新增甲修包，并收束 PR #64 已声明但客户端列表漏列的 Pistol/15 发弹匣；供应按钮改为三列五行，避免与右侧 Stash 回收区重叠。
- Base/Raid 每恢复 1 点分别按 10%/20% 降低当前最大耐久，最低保留出厂最大耐久的 20%；点数不足时自动选择能够完整支付的最大整数修复量，零耐久防具可恢复。
- schema 继续为 v6：既有字段已保存防具当前/最大耐久与甲修包剩余点数；加载显式接受 PR #64 的 `survival-loadout-content-4` 档案，不为空结构变化增加版本。
- 甲修包使用代码 fallback；未生成、发布或接入正式资源，未修改美术 manifest。

## Survival Loadout 多武器切片当前实现

- Equipment 扩展为第一长枪、第二长枪、手枪、头盔、护甲、胸挂和背包七槽。武器定义声明兼容槽集合；快速装备按稳定顺序选择第一个空兼容槽，显式拖放继续使用 InventoryDomain 原子查询与提交。
- 新 Profile 提供基础 Pistol 与两只 15 发手枪弹匣。Pistol 与 Rifle 共用当前 9mm 普通弹，但两类弹匣不能互换；Pistol 使用已批准既有资源，未发布手枪弹匣继续使用代码 fallback，未修改美术 manifest。
- Raid 以第一长枪→第二长枪→手枪的顺序选择首把可用武器。`1/2/3` 触发 0.65 秒长枪或 0.35 秒手枪切换；切换期间可普通移动，冲刺、射击、换弹、治疗或受控会中断且不会改变当前槽。
- Rifle 保持按住自动射击，Pistol 只消费新的射击边沿。射击配置、切换耗时、磨损、弹匣、枪膛、故障和维护均按当前武器实例/内容定义解析，不再由 App 假设主武器槽。
- Base/Raid 共用七槽页面，三个武器槽分别显示自己的枪膛、弹匣和耐久；Raid HUD 提供 `1/2/3` 当前高亮。拖匣、卸匣、换弹、清障和状态显示均指向正确武器实例。
- content v4 增加类型化 WeaponUse；schema v6 保存新装备槽并继续读取 v1～v5。旧 v5 中尚无耐久的 Pistol 会迁移到合法出厂状态，已保存的 Rifle 耐久/故障不被覆盖。

## Survival Loadout 武器状态切片当前实现

- 基础步枪以 0.01 精度保存 100.00 耐久；只有成功击发磨损 0.10。61～100 无随机故障，31～60、11～30、1～10 分别使用 0.5%、3%、12% 基础故障率；型号可靠性乘数和故障权重来自版本化内容定义。
- 本切片只启用 Stovepipe：故障发生在成功击发后，子弹与耐久已经消耗，但不会自动送入下一发；故障时射击被领域拒绝且不改变 Profile。0 耐久武器不能消耗枪膛弹药。
- 故障类型不直接显示；HUD 只报告通用 `MALFUNCTION`。玩家保持瞄准并在一秒内完成四次、每段至少 36 逻辑像素且夹角至少 120° 的鼠标反向扫动即可清障。射击、换弹、冲刺、库存和受控状态会重置手势，普通受伤不会。
- 新增 25.00 容量的基础武器维护包。将维护包拖到武器：Base 即时恢复当前耐久且不损失最大耐久；Raid 启动 8 秒可中断维护，完成时按实际修复量损失 10% 当前最大耐久，最低不低于出厂上限的 20%。维护期间可以基础速度的 45% 缓慢移动；受伤、战斗、冲刺、库存或受控仍会中断，零进度、零消耗。
- 武器未装备时由物品卡片显示耐久；装备到主武器栏位后，禁止内层物品卡片重复绘制，只保留栏位的单行精确耐久。
- Medkit、Bandage、Tourniquet 和 Painkiller 在 Raid 内使用时均允许以基础速度的 45% 缓慢移动；预览与会话仍消费同一版本化医疗定义。
- schema v5 保存武器当前/最大耐久与故障；schema v1～v4 为旧武器补全满耐久、无故障默认值。拒绝命令继续保证指纹、revision、货币和稳定 ID 高水位不变。
- 维护包使用代码 fallback 表现；未生成、发布或接入正式资源，未修改美术 manifest。

## Survival Loadout 医疗切片当前实现

- Scratch 有 35% 概率造成轻度流血，Bite 有 75% 概率造成重度流血；判定在护甲与最终伤害之后执行一次，使用独立 PCG32 命名随机序列。
- 轻度流血 1 HP/秒、40 秒自然结束；重度流血 2 HP/秒且不会自然停止。流血不能把玩家降到 1 HP 以下。
- 疼痛由流血派生；未被止痛药压制时移动和当前武器操作速度降低 10%。首次疼痛及后续 15～25 秒叫声会显式刺激附近敌人，不生成或播放正式音频。
- 新增 Bandage、Tourniquet、Painkiller 与类型化医疗能力；Medkit 仍为 3 次、5 秒连续恢复最多 30 HP，首个实际恢复点消耗一次，中断保留已恢复生命。
- Raid 按住 `5` 打开胸挂医疗轮盘，释放执行；随身医疗可右键使用。Base 个人页右键即时治疗，且 Base 不推进流血与止痛药计时。
- schema v4 保存医疗状态与入场快照；成功撤离保留状态，死亡/主动放弃清除状态并恢复 100 HP，关闭程序继续恢复出击前档案。
- 新增医疗定义复用已批准 Medkit 占位图；未生成、发布或接入正式美术/音频，未修改美术 manifest。

## Survival Loadout 当前实现

- ContentRegistry 新增 ProtectiveGear、Helmet、BodyArmor 及基础头盔/基础护甲定义；两项均使用代码占位表现，没有生成或发布资源，也未修改美术 manifest。
- AssetRegistry 防具实例保存出厂最大、当前最大与当前耐久；schema v3 往返保存，并能从 v1/v2 为防具补全合法满耐久默认值。
- Base/Raid 个人页启用主武器、头盔、护甲、胸挂、背包五槽；拖拽与快速装备继续走统一 InventoryDomain。固定供应提供基础防具，部署快照、成功保留和死亡全损均包含两新装备根。
- 敌方 Scratch 产生躯干命中，Bite 产生头部命中；GameSession 消费模拟事实并在同一 Profile 事务内提交 HP 与护甲磨损，再同步 Raid World。渲染层不推断命中部位或减伤。
- 玩家射击在击发时冻结实际准星覆盖的 Raid 局部目标 ID 与部位意图，再按碰撞落点稳定解析 Head/Torso/Legs；只有同一目标、同一部位双重匹配才写入 Headshot/WeakPoint。准星在目标前后而射线穿过头部时只形成普通命中。普通命中不显示 X，爆头/弱点才显示短促专用标记。受击边缘反馈区分普通伤害与护甲实际减伤。

## Alpha Hardening 当前实现

- 最低出击能力与救济资格统一统计散装弹药、弹匣有序弹药和枪膛弹药，避免已有 30 发可用弹药时误发救济。
- Deploy 在交换 Raid 运行时前再次原子保存出击前 Profile；Raid 内整理、弹药动作、治疗、战斗和 Loot 只修改内存。关闭程序后主档与安全备份均恢复出击前状态；旧版本留下的 pending Raid 存档会清理该局生成 Loot 并无损返回 Base。
- 固定供应内容加载校验 Alpha 25% 向下取整、最低 1 的回收价基线。
- 双份损坏存档明确失败；Deploy 保存失败不交换 Profile、不进入 Raid。
- Base `Tab` 与仓储 `E` 打开同一个“左侧角色/配装/随身容器，右侧 Stash”界面；Raid `Tab` 使用同一拖拽内核，不暴露 Stash，但允许随身弹药拖到弹匣执行限时压弹。
- Base 与 Raid 的弹匣右键菜单都保持卸弹入口可发现。Base 即时卸入 Stash；Raid 关闭库存并启动 3 秒动作，优先卸入背包、再尝试胸挂通用格。空弹匣、随身空间不足或中断均不改变 Profile。
- 拖动需超过 4 像素；原物留在原位，虚像跟随鼠标，绿色/蓝色/红色与 `MOVE/SWAP/MERGE/LOAD/INSTALL/BLOCKED` 同时表达真实领域预览。Ctrl=1、Shift=向上取半在按下时锁定，Ctrl+Shift 无操作。
- Base 与 Raid 世界复用已批准主角资源；个人页显示同一资源的静态预览。左右移动复用六帧资源，上下移动与静止暂用静态图，RL-ANIM-001 的正式补全仍延期。
- 用户已明确修订外部 Alpha 规格中的三项旧限制：Raid 允许拖匣换弹、允许局内压卸弹，关闭程序后回滚到出击前存档而非异常全损；同时要求 Raid 支持奔跑。GDD 资料库保持只读，本仓库仅记录冲突与实现结果。

## Base/Raid 客户端验收加固当前实现

- BaseWorld 保存最后一次水平朝向；静止状态不再由渲染器强制回到左向。共享 `collision` 模块对 Base 设施和 Raid `BallisticBlocker` 执行 X/Y 两轴连续首次接触解算；玩家及敌人的四向、斜向、长帧与攻击前冲均停在障碍边缘，未被阻挡轴仍可贴墙移动。
- SDL client 的全部玩家可见文本绘制统一经过 `UiTextRenderer` 和 `localizeUiText`；内容名称、动态计数、领域拒绝原因、菜单、HUD、库存、Base、RaidResult 与 F10 面板均支持 English/简体中文。
- 首次运行默认简体中文；主菜单与 Base/Raid 暂停菜单的设置页可点击语言项切换，`L` 是同一设置页快捷键。选择写入独立 `settings.json`，损坏或未知设置安全回退到简体中文，不改变游戏存档。
- Windows 客户端使用系统微软雅黑生成并缓存 Unicode SDL 纹理，不提交字体文件、不生成美术资源、不修改 manifest；纯数字物品数量继续使用 SDL 内建数字字形。

## Base 世界时钟与每日需求 v1 当前实现

- `ProfileState::WorldClockState` 以第 1 日 08:00 起算的整数世界分钟作为唯一权威时间；Base 与 Active Raid 使用相同的暂定 60 倍时间缩放，日、时分与昼夜只由领域投影生成。
- 未暂停 Base 世界和 Active Raid 才推进。Base 库存/设施模态页、Esc 暂停、MainMenu、RaidResult 与离线时间冻结；Base 每 30 秒有效模拟时间检查点保存，离开 Base 前显式保存。
- 四项需求改为每日 00:00 结算。多日跨度以常数时间补算，`resolvedDemandCycleCount` 保证同一日界线只结算一次；短缺仍不阻止游玩或损坏资产。
- schema v8 保存时钟与已结算日数；v1～v7 迁移到初始时刻且不重放旧 Raid 次数。Raid 内时间只随成功、死亡或主动退出 Settlement 提交，关闭程序或异常退出恢复出击前时间。
- Base、资源分配页与 Raid HUD 只读显示双语时间；未增加调试按钮、新资源、音频或 manifest 修改。

## Raid 往返行动耗时 v1 已接受实现

- `MapDefinition::travel` 为 Greyline Depot、Riverside Checkpoint、Ashworks Yard 分别配置 `45/45/90`、`90/90/180`、`150/150/300` 世界分钟的出发/正常返程/失败归队开发值；ContentRegistry 拒绝零值、过大值和失败归队短于返程。
- `queryRaidTravel` 无副作用投影出发与抵达时间；Base 出击面板显示抵达昼夜和三项耗时，RaidResult 显示本局实际提交的返程或归队时间。
- Deploy 快照冻结地图旅行值、出发前时钟和 Base 资源；成功撤离与主动退出使用正常返程，死亡使用失败归队。旅行跨越 00:00 时复用每日需求幂等结算。
- schema v9 保存旅行快照与结算耗时；schema v8 旧存档和旧 pending Raid 显式迁移。异常退出不能走 Settlement，只能恢复出发前 Profile，保证时钟、需求周期、资源与资产一起回滚。
- 当前不实现夜间视野、路线状态、旅行遭遇、哨所、情报、人口/床位/口粮、精力或睡眠；分钟值是集中内容调参，不声明为最终平衡。

## Base 枪匠全面维护服务 v1 历史基线

- PR #81 的 content v15 曾数据化全面维护价格和 240 世界分钟时长；这是已合入的历史存档合同，当前 PR #83 已按用户新决策替换新维护路径。
- `BaseServiceJobId`、`BaseServiceAssetLocation` 和单项 `GunsmithMaintenanceJob` 明确服务期间的唯一资产所有权。送修只接受 Stash 根层受损武器，原子扣款并保存；所有拒绝和保存失败保持 Profile 指纹、revision、货币与高水位不变。
- schema v10/v11 继续保存旧任务、高水位、冻结报价/完成点和服务资产位置；PR #83 不删除这些字段，以便旧存档中的武器仍由唯一位置持有并可安全领取。
- 当前新维护不会再产生这种任务；旧任务在加载后立即可领取，Stash 空间不足时仍零修改保留服务所有权。

## Base 周期愿望与物资提交 v1 当前实现

- content v16 提供五日周期和三个稳定愿望定义，分别要求可乐、废旧零件或损坏电子元件，并只奖励既有基地资源；显示名和数值仍是开发期内容。
- `BasePriorityState` 保存当前稳定定义 ID、周期索引、完成状态和累计错过周期。周期从新 Profile 的初始世界分钟起算，任意大跨度时间均常数时间轮换；完成或错过现在写入下一次每日士气账本，不会在同一天被重复提交刷取。
- 愿望提交只消费玩家明确选中的基地可访问自有资产；正常流程不再要求物品先进入 BaseIntake。Stash、装备与随身容器不会被静默扫描或自动提交，query/execute 共用匹配、数量与容量规则。
- schema v11 保存愿望状态；schema v10 及更早版本按当前世界时间确定性初始化。Pending Raid 同时冻结出发前愿望，异常退出跨周期也会精确回滚。
- Allocation 页使用双语文字和几何占位显示当前愿望、剩余时间、指定物资、资源收益、完成/错过状态及手动提交入口；愿望仍是显式提交，不会被日常自动供给策略代替。

## Base 运营状态与即时枪械维护 v1 当前实现

- content v18 以四项资源各自每日需求为分母，只配置紧张/充足储备日数；已无消费者的枪匠耗时和运营耗时倍率已删除。
- `BaseOperationalProjection` 只读计算各项整日储备、精确最短板和运营档位，不进入 Profile、revision 或存档。首版状态仅作风险可读投影。
- 玩家枪械全面维护只消费货币：候选 Profile 原子恢复当前/最大耐久到出厂值并清除故障；不推进世界时间、不创建任务、不移动武器，也不改变枪膛和已装弹匣。运营资源不影响报价或资格。
- schema 维持 v11，并显式接受 content v16/v17 存档；旧计时任务立即可领取。Allocation 与 Supply 以中英文显示运营状态、最短板和即时维护反馈。
- 当前四项池仍只是早期运营储备；正式居民士气已由独立三档状态实现，不复用旧 `morale` 资源字段。人口、口粮/床位和制造已成为实际消费者，自动防守仍延期。

## Base 居民、床位与睡眠 v1 当前实现

- `ProfileState::BasePopulationState` 只保存普通居民聚合人数和床位容量；玩家与未来具名 NPC 不计入该池，也没有逐居民姓名、情绪、装备或日程模拟。
- 旧档迁移为 8 名普通居民、10 个床位；每人每天消耗 1 单位口粮，因此旧固定食物需求仍为 8，升级不会改变既有每日消耗。原 `morale` 池在界面改称运营支持，仍不是完整版三档居民士气。
- Base 新增文字/几何占位宿舍。玩家可查看居民、床位/拥挤、口粮储备和下次日结，并正常选择休息 1、6 或 12 小时；休息不治疗玩家。
- `queryBaseRest/executeBaseRest` 在候选 Profile 上推进唯一 WorldClock、按人口结算跨越的日界线、同步五日愿望并原子保存。非法时长、Raid pending、过期 revision、重复事务和保存失败均不产生部分提交。
- schema v12/content v20 保存人口与床位；schema v11 及更早版本使用确定性默认值迁移。Base 实时流逝和 Raid 往返跨日也消费同一人口口粮需求。
- PR #86 已把普通幸存者接纳接入聚合人口；PR #87 增加一项真实建设项目；当前士气切片把聚合居民接入三档正式士气。岗位/专业、精力、具名 NPC 和疾病模拟继续延期。

## Raid 普通幸存者安全转移 v1 当前实现

- content v21 为三张固定图各声明一个稳定、一次性的普通幸存者救援点；内容加载拒绝重复 ID、未知类别、越界以及与障碍、高危区或撤离区重叠的定义。
- 玩家在区域内持续按住 F 2 秒完成安全转移；松键、离区、受伤、受控制或打开背包都会取消进度。界面只使用双语文字和几何占位，显示接纳后的居民、床位缺口和日口粮预测。
- `ProfileState::committedRescues` 与 schema v13 幂等保存已完成的稳定救援 ID。接纳立即增加聚合普通居民；床位或口粮不足只预警，不阻止接纳，同图不能重复增加人口。
- `GameSession` 同时维护活动 Raid 候选和不含 pending Raid 的干净恢复候选。救援写盘只保存人口事实；后续死亡、主动退出或异常关闭仍保留居民，但装备、HP、战利品、资源和 Raid 时间继续恢复到出击前状态。写盘失败则人口、账本和世界确认均不提交。
- 本切片没有新增或修改正式美术、音频与 manifest；具名 NPC、护送 AI、职业、伤病和逐人模拟继续延期。

## Base 宿舍扩建 v1 当前实现

- content v22 为废旧零件与损坏电子元件声明独立建材价值，并定义唯一宿舍 1→2 级项目：4 建材、3 名聚合劳动力、360 世界分钟、床位 10→14。四项运营资源不承担建筑材料语义。
- 用户于 2026-08-25 明确修订返还合同：成功撤离后，装备、弹药、战利品及其容器关系全部保持撤离瞬间的精确 `AssetLocation`；未来载具货舱也必须遵守同一规则。Settlement 不再把物品迁入 `BaseIntake`，该容器只保留旧存档恢复用途。
- Allocation 页从统一的基地可访问自有资产中显式选择回收物加工为建材；原资产实例不可逆消失。宿舍页显示等级、建材、可用/占用劳动力、剩余时间，并提供正常的开始或取消入口。
- 启动、取消和完成均通过候选 Profile 原子提交；取消返还锁定建材且不回退已流逝时间。Base 实时、休息和 Raid 往返共同推进项目，完成只结算一次。
- Deploy 快照冻结出击前建材、项目和床位；未结算 Raid 异常恢复时回滚建设进度和床位，但不会回滚已经安全转移并落盘的普通居民。
- schema v14 保存当前项目及 Raid 回滚快照；schema v13 确定性迁移为宿舍 1 级、0 建材、无活动项目。客户端继续只使用双语文字与几何占位，没有修改正式美术、音频或 manifest。

## Base 分类自动供给 v1 当前实现

- `BaseSupplyPolicyState` 按物品定义保存唯一供给分类：食物、医疗、娱乐或安全。勾选/取消是候选 Profile 原子事务，只改变授权，不移动、不锁定也不立即消耗任何物品。
- 菜单按分类聚合显示当前拥有且可提供对应贡献的物品定义、总数量和单件贡献。可乐可在食物与娱乐之间选择一种用途；基础药品提供较高医疗贡献，卫生纸提供较低医疗贡献；旧书提供娱乐贡献。新增内容仅使用文字/几何占位。
- 每日需求仅在现有储备不足时，按稳定资产 ID 消耗能够补足缺口的最少完整数量。未勾选物品、不匹配分类及非空容器均不消费；规则在物品耗尽后继续保留，后续获得同定义物品仍适用。
- Pending Raid 期间不自动消耗任何自有资产，使异常退出能恢复精确出击前资产状态；Raid 往返仍结算既有抽象储备消耗。成功返回 Base 后，物品继续保持玩家/容器原位置，下一次 Base 日结才可能按已授权策略使用。
- schema v15/content v23 保存策略并把 v14 旧档迁移为空策略；未知定义、类别无对应贡献、重复条目或坏分类均拒绝加载。该切片已由 PR #87 接受并进入 main。

## Base 居民伤病与医疗所治疗 v1 当前实现

- `BasePopulationState` 在普通居民聚合人数之外保存受伤人数；受伤居民继续占用床位并消耗口粮，但不计入宿舍建设的健康劳动力。当前没有逐人姓名、职业或病症模拟。
- Ashworks 的一次性安全转移冻结并接纳 1 名受伤居民；其余两张固定图当前接纳健康居民。伤病事实与救援账本一起幂等落盘，即使同局随后失败也不会重复或回滚。
- 医疗所同页保留两条独立路径：玩家付费医疗只扣货币并立即完成；居民治疗消耗 360 世界分钟，并从统一 `AssetRegistry` 的真实位置原子消费玩家已授权为医疗供给的物品。没有第二套公共医疗库存。
- 居民治疗预览与提交使用同一确定性计划：贡献高的物品优先，同级按稳定资产 ID；非空容器不可被消费，拒绝、过期 revision 或保存失败均保持 Profile 和资产不变。
- Base 实时、休息及 Raid 往返共同推进活动治疗；到期只结算一次。schema v16/content v24 保存受伤人数、活动治疗和出击前回滚状态；v15 迁移不会追溯制造伤病。
- 客户端继续只使用双语文字和几何占位，没有新增或修改正式美术、音频与 manifest。

## Base 正式士气与周期事件 v1 当前实现

- `BaseMoraleState` 与旧运营支持资源完全分离，保存低迷/稳定/高昂档位、趋势、连续低士气天数、恢复进度和上次每日原因账本。
- 统一日结顺序为需求消耗→士气结算→愿望轮换→事件轮换；愿望和事件写入下一日原因，短缺/床位不足优先，单个世界日最多移动一个档位。
- content v26 提供四个五日轮换事件和 120%/100%/90% 制造耗时系数。事件由 Profile 与周期确定并保存，打开界面或重启不能重抽；任意大时间跨度只按事件集合大小汇总。
- schema v18 保存士气、账本、当前事件及出击前回滚快照；v17 确定性迁移为当前日的稳定士气和事件。异常退出 Raid 精确恢复出发前状态。
- Allocation 显示正式士气、趋势、日结原因、低士气持续时间、当前事件与下次轮换；工坊显示当前士气下新订单耗时。客户端只读取领域投影，不自行推断变化。
- V1 不移除居民、不触发叛乱、不改变战斗难度，也不阻止出击；更复杂的愿望奖励、事件选择和人口后果后置。

## 尚未完成

- 高倍率圆形光学视野等待首个合法高倍瞄具定义、附件安装点和实际内容消费者后独立交付；当前基础开镜不伪造高倍镜。
- Rifle 当前只启用 Stovepipe；Misfire/Double Feed 需要通用的 Raid 动态地面弹药所有权，不能静默销毁或凭空生成退膛/抛出弹药。
- 组件级耐久和改枪台后续独立切片；当前枪匠服务只消费已有武器实例耐久与故障，不建立人口岗位或通用任务框架。
- 疼痛叫声的墙/门遮挡等待正式空间遮挡查询；当前只提供有消费者的距离刺激，不能扩张为通用音频事件总线。
- 旧 V0 `ItemId`/`ItemInstance` 与旧 GameplayWorld 路径仍保留给历史回归；生产 Alpha 已绕过，后续按消费者安全退场。
- Week29 分支继续不整体合并；已接受反馈均由后续独立切片按新边界实现。
- 正式攻击动画及所有新正式美术/音频生产。

## 明确停止扩展的 V0 合同

- 3 HP、180 秒直接失败、V0 只读 Stash、无限弹和旧 RaidSettlement 不是产品终态，不得增加新消费者。
- 生产 Alpha 只通过 Profile Deploy/Settlement 事务进入 Raid；不得重新引入 Profile 与 V0 库存的资产复制桥。
- 生产射击不得重新引入可渲染/可碰撞 Projectile 场景实体；武器、伤害、存档和 App 只能消费射击领域值、逻辑飞行投影与 HitResult。
- 普通命中最终不显示准星 X；爆头/弱点必须由击发准星意图与真实命中部位双重验证的领域结果驱动，App 不得猜测。当前敌人尚无数据化弱点区域，因此只有领域接口预留，不伪造弱点内容。
