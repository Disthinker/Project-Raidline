# Project Raidline 已知问题与待办

最后核对：2026-09-02。

## 已确认缺陷

| ID | 问题 | 状态/依赖 |
| --- | --- | --- |
| RL-INV-001 | 背包物品合法位置缺少完整原子交换 | PR #58 已合入；新 InventoryDomain 与用户人工验收通过。旧 V0 inventory 仅保留历史回归 |
| RL-INV-002 | Ctrl/Shift 数量选择后的拖拽锁定不完整 | PR #60 已完成返工并通过 exact-head CI 与用户正常游玩验收 |
| RL-INV-003 | 同定义弹药堆叠与 60 发上限 | PR #54 已合入并完成人工验收 |
| RL-UI-001 | Alpha Profile 库存回归为点击来源/目标和验收按钮，Base 缺角色图；Raid 缺压卸弹和奔跑，弹匣右键入口会被执行预查询错误隐藏 | PR #60 已进入 main，精确 head CI 与用户正常游玩验收通过 |
| RL-INV-004 | pending Raid 根资产被校验为必须永久保持装备，导致局内拖放卸装/重装整笔失败 | PR #69 已改为验证根资产仍属于随身所有权树，通过 exact-head CI 与用户验收后以 `f593719` 合入 main |
| RL-UI-002 | Base/Raid 缺少可冻结世界的 Esc 暂停菜单，旧 Raid Esc 会进入双按放弃流程 | PR #69 已加入继续、设置、回主菜单和退桌面菜单，并移除双按 Esc 放弃入口；已通过用户验收并合入 main |
| RL-UI-003 | Base 静止朝向回左；Base/Raid 玩家及敌人未共享连续碰撞，纵向移动与敌人追击可穿过障碍；玩家文本没有中文/语言设置 | PR #78 已修复并通过 exact-head CI 与用户正常游玩验收，以 merge commit `ba8283f` 进入 main |
| RL-UI-004 | 旧 Settlement 把成功带回物迁入 BaseIntake，破坏玩家撤离时选择的背包/装备位置并让物品只在分配页可见 | PR #87 已删除正常返还迁移：所有随身物保持精确原位置，未来载具沿用同一合同；BaseIntake 仅兼容旧档。已通过 exact-head CI 与用户正常游玩验收，以 `1be94bf` 进入 main |
| RL-COMBAT-001 | 普通命中/爆头/弱点缺少领域命中部位合同 | PR #61 已完成 Head/Torso/Legs、Normal/Headshot/WeakPoint、防具接线与代码反馈，并通过 CI 和用户验收后合入 main |
| RL-MED-001 | Raid 缺少流血、疼痛与对应战地医疗闭环 | PR #62 已通过 exact-head CI 和用户正常游玩验收，并以 merge commit `ea918ab` 进入 main |
| RL-MED-002 | 疼痛叫声缺少墙/门声学遮挡 | 当前地图没有正式墙/门遮挡查询；本切片只使用 300 世界单位显式警觉刺激，完整遮挡需在空间领域出现实际消费者后实现 |
| RL-WEAPON-001 | 武器缺少耐久、故障、清障和维护闭环 | PR #63 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `b8ddbe3` 进入 main |
| RL-WEAPON-003 | 生产 Raid 仍假设只有一个主武器实例 | PR #64 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `4c16596` 进入 main |
| RL-ARMOR-001 | 防具受损后缺少资源化维修与 Raid 风险动作 | PR #65 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `755fa00` 进入 main |
| RL-WEAPON-002 | Misfire/Double Feed 需要可保存的动态 Raid 地面弹药所有权 | 当前只启用不需要创建/抛出弹药资产的 Stovepipe；待 Raid 地面任意资产合同建立后独立扩展，禁止吞弹或凭空造弹 |
| RL-COMBAT-004 | 击发时未冻结逻辑飞行且缺地面命中粒子 | PR #66 已以 `7877d71` 合入非实体逻辑飞行；PR #67 按新版合同把终点从准星点修订为武器最大距离/世界边界，并加入最近障碍与 Ground 结果 |
| RL-COMBAT-005 | 位置式实际准星、手动压枪、随机散布与基础开镜未形成统一手感合同 | PR #67 已通过用户验收并以 `881c034` 合入 main |
| RL-COMBAT-006 | 准星响应、极端横向后坐力、OS 光标离窗与基础听觉反馈 | PR #68 已通过 CI 和用户验收，以 `ba3375e` 进入 main；远程桌面音频映射的附加延迟按用户要求延期到本机复验后再判断 |
| RL-COMBAT-007 | 常规瞄准仍有惯性、散布缺少移准/近距曲线、旧曳光像慢速实体弹 | PR #69 已完成 Direct/高倍模式分离、移准与距离包络、4200/7200 逻辑弹速和纯短线曳光，通过 exact-head CI 与用户验收后以 `f593719` 合入 main |
| RL-COMBAT-008 | 距离遮蔽动态扩散、组合输入闪动，以及 App 可读准星大于真实随机弹道范围 | PR #71 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `33da892` 进入 main |
| RL-COMBAT-009 | 成功击发缺少短烟、柔边局部闪光和不影响瞄准的轻微屏幕抖动 | PR #72 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `795b644` 进入 main |
| RL-COMBAT-010 | 完成武器切换时重建 WeaponAimState，导致实际准星跳回相对输入锚点 | PR #74 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `6138da8` 进入 main |
| RL-COMBAT-011 | 相对鼠标模式的实际准星与控制目标可持续移动到当前屏幕外，玩家可能无法找回准星 | PR #111 已允许权威准星和目标越出当前屏幕约 48px，并在外扩边界清除外向速度；经 CI 与用户正常游玩验收后以 `636a40b` 进入 main |
| RL-ANIM-001 | 角色上下移动动画和停止朝向不完整 | PR #78 已修复 Base 停止时丢失最后水平朝向；Base/Raid 左右移动继续复用六帧资源，上下移动仍用静态图，正式补全延期 |
| RL-POP-001 | Raid 救援与普通居民聚合池缺少可持久闭环 | PR #86 已以 `ee9ba48` 进入 main；三图一次性安全转移、schema v13 幂等账本与干净恢复检查点已完成，具名 NPC、护送 AI、伤病和职业仍延期 |
| RL-POP-002 | 受伤居民缺少使用真实库存物资与世界时间的医疗所治疗闭环 | PR #88 已建立聚合伤病、医疗分类授权、确定性精确消耗、单治疗槽和 schema v16；经 exact-head CI 与用户正常游玩验收后以 `987dc6b` 进入 main |
| RL-BASE-001 | Base 缺少以真实资产、劳动力和世界时间驱动的生产消费者 | PR #89 已完成单工坊槽和维护包配方，经 CI 与用户验收后以 `194f910` 进入 main |
| RL-BASE-002 | 旧运营支持资源不能表达居民士气，也没有愿望、短缺、床位与生产之间的可解释反馈 | PR #90 已完成独立三档士气、每日原因账本、五日稳定事件和制造耗时消费者，经 CI 与用户验收后以 `1af0e56` 进入 main |
| RL-BASE-003 | Home Region 扩大后，玩家必须逐个寻找固定设施才能发现待领取产物、进行中任务和缺员 | PR #130 已交付只读运营总览和点击定位，经 CI 与用户正常游玩验收后以 `ee44d52` 进入 main |
| RL-BASE-004 | 升级、制造和居民治疗完成后缺少主动提示，玩家仍需反复打开总览检查 | PR #131 已交付已提交结果驱动的短时通知和建设模式设施标记，经 CI 与用户正常游玩验收后以 `e7590d9` 进入 main |
| RL-BASE-005 | 仓库、医疗所、宿舍和工坊仍由 BaseWorld 代码固定坐标重建，基地建设无法形成可持续空间布局 | PR #132 已把四类核心设施迁入 Profile 权威布局并提供原子移动，经 CI 与用户验收后进入 main |
| RL-BASE-006 | 迁徙后进入 Reserve 的核心设施仍在 BaseWorld 中绘制、碰撞，且区域页可无选址直接安装 | PR #133 已让 Reserve 设施退出世界投影，并从建设面板以单一原子事务完成选址与恢复运行；经 CI 和用户验收后以 `411aaed` 进入 main |
| RL-BASE-007 | 厨房/净水首次建设只写入隐藏等级并直接标记 Installed，BaseWorld 没有对应设施实体 | PR #134 已让完成结果进入 Reserve，经建设面板部署为第五类空间核心设施，并为 schema v40 旧档补齐确定性布局；经 CI 与用户正常游玩验收后以 `2ec99c2` 进入 main |
| RL-BASE-008 | 建设工程散落在设施现场和区域迁徙页，`B` 面板只显示购买物且超过四项会静默截断 | PR #135 已建立内容驱动的工程目录投影和稳定分页；工程开始/取消与区域页跳转统一到 `B` 面板，经 CI 与用户正常游玩验收后以 `c37fe32` 进入 main |
| RL-BASE-009 | 基地设施从整个建筑矩形四周均可交互，建设放置也不能保护未来入口和 NPC 作业空间 | PR #136 已建立确定性入口、交互区和作业净空区；设施、储物箱和环境物不能堵住入口。经 CI 与用户正常游玩验收后以 `d1c9b35` 进入 main |
| RL-BASE-010 | 玩家必须打开建设管理卡或运营总览，才能知道世界设施正在工作、产物待取、缺员或不可用 | PR #137 已把现有管理事实投影为入口、近距提示和 Home Region 地图的统一状态；经 CI 与用户正常游玩验收后以 `95f830c` 进入 main |
| RL-BASE-011 | 空间化设施只有建筑和入口，升级、制造、治疗等任务缺少稳定的世界作业位置，后续 NPC 与设施内部表现没有可复用空间合同 | PR #138 已建立五类类型化、确定性作业点，并由既有作业净空区保护；经 CI 与用户验收后以 `5da9591` 进入 main |
| RL-BASE-012 | Workshop/Medical 虽有聚合岗位分配，但基地世界没有工作人员状态反馈，必须打开管理页才知道缺员或是否在工作 | PR #139 已把既有岗位与任务事实投影为作业点旁的缺员、闲置、工作和暂停占位；经 CI 与用户验收后以 `abe2886` 进入 main |
| RL-BASE-013 | 聚合岗位占位只能查看，点击作业点不能直接进行岗位分配、清除或自动补员 | 当前分支让 Workshop/Medical 作业点进入现有设施检查器并提供岗位专用右键菜单；动作继续消费领域查询，逐人 NPC 与排班延期 |
| RL-POP-003 | 聚合居民缺少专业构成与互斥岗位，设施等级也没有成为现有服务的实际消费者 | PR #91 已完成四类聚合专业、工坊/医疗所互斥岗位、专业效率和两个线性设施升级，经双平台 CI 与用户正常游玩验收后以 `12a2fa6` 进入 main；具名 NPC、培训、战斗小组和复杂居民模拟继续延期 |
| RL-MAP-001 | 三张固定图缺少出击风险判断、探索迷雾和可消费情报闭环 | PR #92 已通过 Windows/Ubuntu exact-head CI，并按用户连续交付授权以普通 merge commit `bf8baf3` 进入 main；用户正常游玩验收后续集中进行 |
| RL-MAP-002 | Raid 世界缺少程序化室外空间、冻结布局与坏种子回退合同 | PR #93 已通过本地 1033/1033、exact-head 双平台 CI 和用户集中验收，以普通 merge commit `1404b41` 进入 main |
| RL-MAP-003 | 可进入建筑仍与室外共享单一坐标、敌人、Loot 和碰撞空间 | PR #94 已通过 CI 和用户验收，以普通 merge commit `62ebd8a` 进入 main；独立空间 ID、入口/返回 Socket、schema v22/content v30 快照和当前空间运行时隔离已接受 |
| RL-MAP-004 | 特殊地点入口固定在单一坐标，无法随本局路线变化且缺少动态冲突过滤 | PR #95 已通过本地 1043/1043、exact-head 双平台 CI 和用户正常游玩验收，以普通 merge commit `d2ceb59` 进入 main |
| RL-MAP-005 | 特殊地点的精确入口和战术地图坐标从开局无条件泄露，探索没有发现价值 | PR #96 已通过本地 1048/1048、exact-head 双平台 CI 和用户正常游玩验收，以普通 merge commit `de3402c` 进入 main |
| RL-MAP-006 | 固定建筑内部布局没有可永久获取的情报，进入室内后战术地图只能显示占位提示 | PR #97 已通过本地 1058/1058、exact-head 双平台 CI 和用户正常游玩验收，以普通 merge commit `a7b3cc2` 进入 main |
| RL-MAP-007 | 敌人只按距离获取目标并直线推墙，视觉与近战没有读取当前空间遮挡 | PR #98 已通过 Windows Debug、1072/1072 CTest、exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `95fcd23` 进入 main |
| RL-PERF-001 | 多名敌人在程序化密集障碍中同时追击时，主线程每敌人每子步重建可见图并形成追帧雪崩 | PR #99 已用静态图缓存、10 Hz 刷新、查询预算和 100 ms 帧上限修复，并经用户复验后以 `1d2fea1` 进入 main；旧 8敌/18障碍/120帧约6357 ms，新 8敌/26障碍约422 ms |
| RL-PERF-002 | 未来百敌与大量建筑内容缺少可扩展查询、公平预算和可观测门槛 | 当前性能基础分支已建立近邻/障碍空间索引、双导航后端、每空间公平轮转、确定性工作计数和 F9 wall-time 面板；32/100 敌人 Debug 压力通过。最终最低配置 Release P95/P99、渲染分块、流场和安全并行仍需真实内容消费者后独立证明 |
| RL-PERF-003 | Frontier Exchange 中持续移动或移动准星时会规律性回退数毫秒，形成高帧率下仍可见的顿挫 | PR #111 将活动时钟改为原位推进已验证状态，连续回归最慢 Update 从约 70 ms 降至低于 1 ms；CI 与用户正常游玩验收通过，以 `636a40b` 进入 main |
| RL-PERF-004 | 在超大 Frontier 地图拾取物品会复制并重复校验冻结 Profile，产生可见单帧卡顿 | PR #113 把 Raid 拾取和局内库存事务收窄到 AssetRegistry 参与者，Windows Debug 回归由约 69.6 ms 降至约 2.8 ms；完整 CI 与用户正常游玩验收通过，以 `f095022` 进入 main |
| RL-MAP-008 | 只有办公室一个消费者，室外投影只显示首个已发现入口，无法证明多地点内容、快照、发现和情报没有特判 | PR #99 已增加独立货运装卸间，投影全部已发现入口，并把入口/返回点纳入合法放置与可达锚点；用户验收、双平台 CI 通过，以 `1d2fea1` 进入 main |
| RL-MAP-009 | `Frontier Exchange` 缺少可支撑完整 Raid 的超大主题分区、内容密度和可扩展投影/导航 | PR #111 已通过 CI 与用户验收，以 `636a40b` 进入 main；25600×14400 layout v3、八区、三地标、道路/环境/玩法 Socket、区块投影和分层导航成为接受基线 |
| RL-MAP-010 | layout v3 大型环境物件比例、重叠和同步加载反馈不满足正常游玩 | PR #111 已按角色尺度修正大型物件并保证非碎屑唯一占位，PR #112 又让碎屑避让大型物件并交付真实加载进度；两项均已验收进入 main |
| RL-AI-002 | 超大地图 36～48 名初始敌人仍是独立随机散点，路线、资源与地标没有形成可理解的遭遇压力，且初始出生点附近可能立即生成敌人 | PR #113 以 content v48/schema v37/rules v26 冻结巡逻、地标守点、路线伏击和同组声响响应，并强制 1200 世界单位出生保护区；128-seed、性能、CI 与用户正常游玩验收通过，以 `f095022` 进入 main |
| RL-MAP-011 | 超大随机图中的救援和自力寻回只有近屏世界提示，外围清剿也没有随机地图消费者 | PR #114 建立 Briefed/Explored 类型化战术目标；救援与失物缓存开局可定位，高危控制随探索显现，Ashworks 外围清剿改为 Frontier layout v4 并保持唯一 Settlement。自动化、exact-head CI 与用户正常游玩验收通过，以 `7be91e8` 进入 main |
| RL-CONTENT-001 | `Frontier Exchange` 六类资源点共用两张全局 Loot 表，地点身份和搜刮决策不足 | PR #115 已以 content v50/rules v27 建立六张专属 Loot 表和九种双语主题物资，通过用户验收并以 `6ab1724` 进入 main |
| RL-MAP-012 | `Frontier Exchange` 的持续高危仍使用全图统一增援节奏和固定高级资源区，缺少地图级危机差异 | PR #116 已冻结道路、工业、货运三类危机及合法目标、压力来源和主题高级 Loot，并修复压力敌人不汇聚的问题；用户验收后以 `e285a2b` 进入 main。实时尸潮、动态封路、停电/火灾和新敌人类型继续延期 |
| RL-CONTENT-002 | 现有 Pistol/Rifle 共用单一 9mm 弹药，弹匣按具体弹药 ID 兼容，无法形成口径、普通弹档位和职责配装梯度 | PR #117 已建立 CaliberDefinition、按口径装填、六职责武器与三口径两档普通弹，并保留所有旧 ID 和冻结 pending Raid；经 CI 与用户验收后以 `8164e66` 进入 main |
| RL-CONTENT-003 | 新增武器、弹药、防具和容器缺少清晰来源梯度，固定供应与制造也未覆盖三口径基础恢复 | Draft PR #119 已实现定义驱动固定供应、三张固定图与三类高危危机独立 Loot 表、三口径标准弹制造和制造回收防套利；Windows Debug、1324/1324 CTest 与 exact-head CI 通过，尚待用户正常游玩验收 |
| RL-AI-001 | 一名敌人进入攻击阶段后，其余警觉敌人被降为 Support 并在等待距离带内停住；旧单槽方案又会让围攻显得不自然 | PR #102 已拆分 Pressure 与攻击槽，允许最多 10 名并发攻击、超额成员稳定轮转，并加入 0.25 秒敌人伤害保护；双平台 CI 与用户正常游玩验收通过，以普通 merge commit `1c62064` 进入 main |

## 需要未来产品决策

- 持续高危、主动控制点、开局冻结的高级资源访问和轻装条件撤离已由 PR #75/#76/#77 接受；当前高危危机切片只数据化道路、工业和货运三种冻结危机。停电/火灾、动态路线封锁、燃油/凭证撤离及最终高级资源数量/品质仍需后续独立范围合同。
- 高倍率光学视野的正式镜片表现和具体倍镜内容；基础准星与开镜合同已由当前切片冻结。
- 完整产品早/中/后期目标、结束条件和长期基地路线。
- 灾难成因、主叙事责任链和正式世界观包装。
- 当前人口切片已把 `food` 明确迁移为按普通居民人数计算的口粮储备，并新增独立床位容量；其余三项 0～100 池仍是早期运营储备。正式居民士气现由独立三档状态承载，原 `M` 继续只表示运营支持。

以上均不阻塞 Core Extraction Alpha；Alpha 普通数值、接口和验收由开发主控收口。

## 延期工程债

- `src/app.cpp` 与 `GameplayWorld` 仍偏大，继续按 Base/Raid 消费者迁移，禁止一次性无行为重写。
- V0 `ItemId`/`ItemInstance`、3 HP、180 秒 Timeout、无限弹和旧 RaidSettlement 只服务历史测试路径；生产 Alpha 已绕过，但删除前仍需完整回归证明。
- 生产路径已移除 Projectile；历史 V0 测试适配器继续按消费者安全退场，不得重新建立 WeaponAmmo、伤害、存档或 UI 依赖。
- 当前没有合法瞄具定义、附件安装点或高倍率内容；圆形光学视野与镜片模糊不得在无消费者时做成通用相机框架。
- Extraction Loop 的正式美术仍使用代码 fallback；美术与 manifest 继续暂停。用户仅授权当前 P0 音效包，P1 音频和其他 runtime 资源仍需另行授权。
- 远程桌面音频映射会在 SDL 设备缓冲之外增加编码、网络和客户端播放延迟；当前 512 帧请求只能缩短游戏自身可控部分，最终本机与远程延迟差异需由用户正常游玩对比确认。
- RL-COMBAT-002 肢体破坏/血液/击退/碎块和 RL-COMBAT-003 尸体残留均不在 Alpha。
- Week29 分支无 PR、未进 main；代码可独立整理，正式攻击动画继续暂停。

## 阶段任务

| 任务 | 状态 |
| --- | --- |
| PR #55 / #54 / #56 / #57 / #58 / #59 | 已进入 `origin/main@ed45baa` |
| Extraction Loop | PR #59 已以 merge commit `ed45baa` 进入 main；本地 620/620、精确 head CI 与用户 7/7 集中验收均已通过 |
| Alpha Hardening | PR #60 已以 merge commit `50849d5` 进入 main；本地 645/645、精确 head CI 与用户最终正常游玩验收通过 |
| Survival Loadout：基础防具与命中部位 | PR #61 已通过 exact-head CI 与用户正常游玩验收，并以 merge commit `733b597` 进入 main |
| Survival Loadout：流血、疼痛与战地医疗 | PR #62 已通过 CI 与用户验收，以 merge commit `ea918ab` 进入 main |
| Survival Loadout：武器耐久、故障与维护 | PR #63 已通过 CI 与用户验收，以 merge commit `b8ddbe3` 进入 main |
| Survival Loadout：多武器配装与切换 | PR #64 已通过 CI 和用户验收，以 merge commit `4c16596` 进入 main |
| Survival Loadout：防具维护 | PR #65 已通过 CI 与用户验收，以 merge commit `755fa00` 进入 main |
| Combat：逻辑弹道与落点反馈 v1 | PR #66 已通过 CI 和用户验收，以 merge commit `7877d71` 进入 main |
| Combat：准星运动、逻辑弹道与开发调参 v1 | PR #67 已通过用户验收，以 merge commit `881c034` 进入 main |
| Combat：输入捕获、后坐力曲线与 P0 音频 v1 | PR #68 已通过 CI 和用户验收，以 merge commit `ba3375e` 进入 main |
| Combat：直接瞄准、距离散布与高速曳光 v2 | PR #69 已通过 exact-head Windows/Ubuntu CI 和用户验收，以 merge commit `f593719` 进入 main |
| Combat：动态散布模型与准星稳定性 v3 | PR #71 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `33da892` 进入 main |
| Combat：射击表现收尾 | PR #72 已通过 CI 与用户验收，以 merge commit `795b644` 进入 main |
| Raid Pressure & Variety：固定地图差异化 v1 | PR #73 已通过 exact-head CI 与用户验收，以 merge commit `a32c476` 进入 main；随机地图、情报、高危和正式地图美术仍延期 |
| Combat Reliability：武器切换准星连续性 | PR #74 已通过 exact-head CI 与用户验收，以 merge commit `6138da8` 进入 main |
| Raid Pressure & Variety：持续高危阶段 v1 | PR #75 已通过 Windows Debug 全目标、807/807 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以 merge commit `773443b` 进入 main |
| Raid Pressure & Variety：主动高危与高级资源区 v1 | PR #76 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `bc26337` 进入 main |
| Raid Pressure & Variety：高危条件撤离 v1 | PR #77 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `d106193` 进入 main |
| Base Growth：资源分配与基础需求 v1 | PR #78 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `ba8283f` 进入 main |
| Base Growth：世界时钟与每日需求 v1 | PR #79 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `5d2a11a` 进入 main |
| Raid 往返行动耗时 v1 | PR #80 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `defaac0` 进入 main；三图分钟值仍是开发期平衡值 |
| Base 枪匠全面维护服务 v1 | PR #81 已通过 exact-head CI 与用户正常游玩验收，以 merge commit `ace7c69` 进入 main |
| Base 周期愿望与物资提交 v1 | PR #82 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `eca7d62` 进入 main |
| Base 运营状态与即时枪械维护 v1 | PR #83 以四项最短储备日数投影运营状态，并把玩家枪械全面维护改为只扣货币、立即完成；已通过 exact-head CI 和用户验收，以 `20d9f48` 进入 main |
| Base 付费医疗服务 v1 | PR #84 已通过 exact-head CI 和用户正常游玩验收，以普通 merge commit `c01d431` 进入 main |
| Base 居民、床位与睡眠 v1 | PR #85 已通过 CI 和用户验收，以普通 merge commit `2377035` 进入 main |
| Raid 普通幸存者安全转移 v1 | PR #86 已通过 CI 和用户正常游玩验收，以普通 merge commit `ee9ba48` 进入 main |
| Base 宿舍扩建与供给修订 | PR #87 已通过 exact-head CI 和用户正常游玩验收，以普通 merge commit `1be94bf` 进入 main |
| Base 居民伤病与医疗所治疗 v1 | PR #88 已通过 exact-head CI 与用户正常游玩验收，以普通 merge commit `987dc6b` 进入 main |
| Base 基础制造队列 v1 | PR #89 已通过 Windows Debug、CTest 988/988、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `194f910` 进入 main |
| Base 正式士气与周期事件 v1 | PR #90 已通过 Windows Debug、CTest 1001/1001、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `1af0e56` 进入 main |
| Base 聚合岗位、专业人口与设施升级 v1 | PR #91 已通过双平台 CI 和用户正常游玩验收，以普通 merge commit `12a2fa6` 进入 main |
| 区域地图与对局情报 v1 | PR #92 已以普通 merge commit `bf8baf3` 进入 main；用户集中验收延期到后续批次 |
| Raid World：程序化室外空间基础 v1 | PR #93 已通过 CI 与用户验收，以普通 merge commit `1404b41` 进入 main |
| Raid World：独立室内空间 v1 | PR #94 已通过 CI 与用户验收，以普通 merge commit `62ebd8a` 进入 main |
| Raid World：特殊地点随机合法放置 v1 | PR #95 已通过 CI 与用户验收，以普通 merge commit `d2ceb59` 进入 main |
| Raid World：特殊地点发现与战术地图投影 v1 | PR #96 已通过 CI 与用户验收，以普通 merge commit `de3402c` 进入 main |
| Raid World：建筑内部图永久情报 v1 | PR #97 已通过 CI 与用户验收，以普通 merge commit `a7b3cc2` 进入 main |
| Raid World：空间战术可靠性 v1 | PR #98 已通过 CI 与用户验收，以普通 merge commit `95fcd23` 进入 main |
| Raid World：第二个代表性地点 v1 | PR #99 已通过双平台 CI 与用户正常游玩验收，以普通 merge commit `1d2fea1` 进入 main |
| Raid World：可扩展性能基础 v1 | PR #100 已通过 CI 与用户正常游玩验收，以普通 merge commit `d7c231b` 进入 main |
| Regional Operations：失物记录与行动老化 v1 | PR #101 已通过 exact-head CI 和用户正常游玩验收，以 `7185d55` 进入 main |
| Regional Operations：Loss & Recovery v1 | PR #103 已通过 exact-head CI 和用户统一正常游玩验收，以普通 merge commit `f31d91a` 进入 main；尸体表现、敌对幸存者携带和正式资源继续延期 |
| Regional Operations：区域路线与轻量哨所基础 | PR #104 已通过 exact-head CI 和用户统一正常游玩验收，以普通 merge commit `dc19745` 进入 main。主基地选址/迁徙、哨所库存/服务/升级和正式资源继续延期 |
| Regional Operations：哨所中断与恢复 v1 | PR #105 已通过 exact-head CI 与用户正常游玩验收，以普通 merge commit `cf555a1` 进入 main。随机遇袭、动态难度、路线事件、哨所伤亡/物资损失和正式表现延期 |
| Regional Operations：基地候选点清剿 v1 | PR #106 已通过 exact-head CI 和用户正常游玩验收，以普通 merge commit `e6721e4` 进入 main |
| Regional Operations：唯一主基地迁徙 v1 | PR #107 已通过 exact-head CI 和用户正常游玩验收，以普通 merge commit `d035181` 进入 main。正式基地布局、随机损失、设施耐久和尸潮预警门禁延期 |
| Regional Operations：区域基地独特设施 v1 | PR #108 已通过用户正常游玩验收，以普通 merge commit `bf0d383` 进入 main。Greyline 新事件/商人、持续维护、设施耐久、额外队列/独占配方和正式表现延期 |
| Regional Campaign：基地威胁预警与自动防守 v1 | PR #109 已通过用户正常游玩验收并以普通 merge commit `fab9f32` 进入 main。实时尸潮、防御设施、战斗小组、多档投资、个人装备损失及正式表现延期 |
| Regional Campaign：基地外围清剿 v1 | PR #110 已通过 CI 和用户正常游玩验收，以普通 merge commit `7bd3b02` 进入 main。动态外围事件、正式场景、防御设施和 AI 小队延期 |
| 程序化 Raid 内容扩展：完整室外布局生成 v3 | PR #111 已通过 CI 与用户验收，以 `636a40b` 进入 main；资源点生态由 PR #112 接受，遭遇生态由 PR #113 接受 |
| 程序化 Raid 内容扩展：既有消费者整合 v1 | PR #114 把救援、失物寻回、高危控制和 Ashworks 外围清剿接入冻结随机布局；自动化、exact-head CI 和用户正常游玩验收通过，以 `7be91e8` 进入 main |
| Content Beta：Frontier 物资身份与 Loot 内容包 v1 | PR #115 已通过用户正常游玩验收，并以 `6ab1724` 进入 main |
| Content Beta：完整目录开发覆盖 | PR #117 的 content v53 已随 `8164e66` 进入 main，PR #118 将同一幂等机制扩展到 content v54 的 50 种定义。按用户“仓库直接提供所有类型”的明确要求，该机制保留为当前开发检查入口且不会重复发放；它会削弱自然成长，发行准备阶段仍需改为开发选项或代表性起始包，但不阻塞当前来源梯度验证 |
| Content Beta：防具、胸挂与背包梯度 v1 | PR #118 已实现轻/均衡/重三档取舍、定义驱动容器 UI、24×16 Stash 和 v53→v54 兼容；经 CI 与用户正常游玩验收后以 `e24da4c` 进入 main |
| Content Beta：战利品来源、经济与补给可持续性 v1 | Draft PR #119 已完成固定地图/高危危机产出梯度、定义驱动固定供应、三口径标准弹制造与防套利校验；Windows Debug、1324/1324 CTest 与 exact-head CI 通过，尚待用户正常游玩验收 |

外部 GDD 的枪匠章节仍保留“全面维护需要等待”的旧描述，与 PR #83 已接受的即时维护决策冲突；其“公共医疗储备”描述也已被用户的新合同取代。GDD 保持只读，待策划线程同步修订。玩家付费医疗继续是货币即时服务；居民/NPC 设施治疗是独立命令，消耗世界时间和玩家明确授权的基地可访问自有医疗物资，不建立第二套库存。

具体依赖、自动化、人工验收和回滚见 `doc/exec-plans/active/regional-base-threat-auto-defense-v1.md`。
