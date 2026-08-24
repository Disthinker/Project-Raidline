# Project Raidline 当前状态

最后核对：2026-08-24。

## Git 与交付基线

- `origin/main@d106193` 已包含完整 Core Extraction Alpha、Survival Loadout、Combat 射击手感/表现收尾，以及 Raid 固定地图、持续高危、主动高危、高级资源区和轻装条件撤离 v1；PR #77 已通过 CI 和用户正常游玩验收后普通合入。
- 当前开发分支：`codex/base-resource-pressure-v1`，从干净的 `origin/main@d106193` 创建。
- 当前活动计划：`doc/exec-plans/active/base-resource-pressure-v1.md`。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main；正式 Grab/Scratch/Bite 图像及所有新正式美术生产继续暂停。用户于 2026-08-21 仅授权当前 ArtWorkbench P0 音效包接入。

## 当前产品里程碑

Core Extraction Alpha、五个 Survival Loadout 切片、Combat PR #66～#74，以及 Raid Pressure PR #73/#75/#76/#77 已接受。当前进入 **Base Growth：资源分配与基础需求 v1**；外部 GDD 继续只读，本仓库 ExecPlan 冻结首个“带回物在个人保留与基地需求之间取舍”的长期循环。

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
20. **Base 资源分配与基础需求 v1**：当前分支把成功带回的新 Loot 放入独立待分配区，允许保留到 Stash 或不可逆转化为食物、卫生、士气、安全；每次正式 Raid 结算消耗小额需求，短缺不造成死档。当前表现只使用文字、色条和几何图形，不接入新美术资源。

每个宏切片内部按领域、服务、客户端和证据形成可回滚提交，但不再为单个技术边界中断玩家功能交付。人工验证统一放在自动化和 CI 之后，由用户执行。

## 已接受能力

- `MainMenu → Base → Raid → RaidResult → Base` V0 流程和进程内多局会话。
- RL-INV-001/002/003：原子交换、Ctrl/Shift 锁定数量拖拽、同定义堆叠合并与 60 发上限。
- `ShotCommand → ShotResolution → HitResult` 窄边界；生产路径已使用非场景实体的 `LogicalBallisticFlight`，冻结本发并连续扫掠目标。
- 四个生产库、唯一业务源码编译所有权、强类型 DefinitionId、版本化 JSON ContentRegistry 和仓库本地 nlohmann-json overlay。
- Persistent Base 的长期 Profile、唯一 AssetRegistry、Stash/三槽配装、固定经济/救济、可行走三设施 Base、首次环境目标链和原子存档。

## 已接受的 Extraction Loop

- `ProfileState::AssetRegistry` 在 Base、Deploy、Raid Loot 与 Settlement 全程唯一拥有资产；装备根、容器子资产、已安装弹匣和 Raid 地面位置均使用稳定实例 ID。
- content v9 提供三张可选择的固定 Raid 地图；每图各有 3 组出生/撤离配对、3 组 4～6 敌人部署、10 个三路线 Loot 插槽与独立障碍。每局冻结 6～9 个有效 Loot，PCG32 命名随机流和所选 MapDefinitionId 写入 pending Raid 快照。
- schema v2 保存当前 HP、弹匣有序弹药、枪膛、Settlement 幂等记录和最近 RaidResult，并能读取旧 pending Raid；新生产 Deploy 不再把运行中 pending Raid 覆盖到磁盘。schema v1 可显式迁移。
- Base 与 Raid 共用按住拖拽库存交互；格子移动/交换/堆叠/配装均由领域预览和命令提交。Base 可将弹药拖到弹匣即时压弹、将弹匣拖到武器安装并按条件自动上膛；Raid 可拖动弹药到弹匣执行 0.2 秒/发的可中断压弹，也可拖动指定弹匣到武器并执行 2 秒换弹。
- 卸弹、显式上膛和 Medkit 使用物品右键情境菜单，不再依赖 `FILL MAG / INSTALL / CHAMBER / USE MED` 等验收按钮。Base 卸弹即时回到 Stash；Raid 弹匣卸弹为 3 秒可中断动作，完成时原子写入背包或胸挂通用格。`F`/`Ctrl+右键` 保留为 Base 快速转移捷径；可穿戴物在对应栏位为空且领域查询合法时优先快速装备，否则沿用容器转移。
- 玩家为 100 HP；Medkit 每件 3 次、每次恢复最多 30 HP，Raid 内治疗 5 秒且中断不消耗。
- 生产 Raid 的常规阶段为 180 秒；归零只进入无终局倒计时的持续高危，不产生时间失败。E 拾取真实 Loot，随身库存可移动和整理，打开时禁止射击/换弹/开始治疗但允许普通移动。
- 3 秒撤离成功保留合法随身资产与 HP；死亡和主动放弃全损并恢复 100 HP。关闭程序或异常退出不会结算，重开后加载出击前的完整 Profile；正式成功/失败结果仍使用唯一 Settlement ID 幂等提交。
- Raid 世界支持 Shift 奔跑，速度为普通移动的 1.5 倍；当前不引入耐力条、负重或复杂移动消耗。
- RaidResult 显示结果、成功带回物和货币变化；失败不生成丢失物清单。生产 Alpha 路径不再使用 V0 柜体、无限弹或 Timeout 结算。

## 当前自动化证据

- Windows Debug 当前树全目标构建成功，`Project_Raidline.exe` 已生成但未由开发代理启动。
- PR #77 的单位克重/嵌套资产/弹匣与枪膛计重、路线资格取消、三图几何互斥、旧 content v11 加载和 GameSession 投影均已完成并接受；当前 Base 资源切片及其验收加固 Windows Debug 全目标构建成功，完整 CTest 847/847 通过，开发代理未启动游戏。
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

- BaseWorld 保存最后一次水平朝向；静止状态不再由渲染器强制回到左向。共享 `collision` 模块对 Base 设施和 Raid `BallisticBlocker` 执行 X/Y 两轴连续首次接触解算；四向、斜向和长帧移动均停在障碍边缘，未被阻挡轴仍可贴墙移动。
- SDL client 的全部玩家可见文本绘制统一经过 `UiTextRenderer` 和 `localizeUiText`；内容名称、动态计数、领域拒绝原因、菜单、HUD、库存、Base、RaidResult 与 F10 面板均支持 English/简体中文。
- 首次运行默认简体中文；主菜单与 Base/Raid 暂停菜单的设置页可点击语言项切换，`L` 是同一设置页快捷键。选择写入独立 `settings.json`，损坏或未知设置安全回退到简体中文，不改变游戏存档。
- Windows 客户端使用系统微软雅黑生成并缓存 Unicode SDL 纹理，不提交字体文件、不生成美术资源、不修改 manifest；纯数字物品数量继续使用 SDL 内建数字字形。

## 尚未完成

- 射击表现收尾：代码与本地自动化已完成；exact-head Windows/Ubuntu CI 与用户对火光、短烟、柔边局部闪光和轻微抖动的正常游玩验收尚待完成。
- 高倍率圆形光学视野等待首个合法高倍瞄具定义、附件安装点和实际内容消费者后独立交付；当前基础开镜不伪造高倍镜。
- Rifle 当前只启用 Stovepipe；Misfire/Double Feed 需要通用的 Raid 动态地面弹药所有权，不能静默销毁或凭空生成退膛/抛出弹药。
- NPC 全面维护、组件级耐久和改枪台后续独立切片，不与当前防具自助维护混写。
- 疼痛叫声的墙/门遮挡等待正式空间遮挡查询；当前只提供有消费者的距离刺激，不能扩张为通用音频事件总线。
- 旧 V0 `ItemId`/`ItemInstance` 与旧 GameplayWorld 路径仍保留给历史回归；生产 Alpha 已绕过，后续按消费者安全退场。
- Week29 枪口与受伤代码反馈仍待按新投影边界独立整理；本切片不整体合并 Week29。
- 正式攻击动画及所有新正式美术/音频生产。

## 明确停止扩展的 V0 合同

- 3 HP、180 秒直接失败、V0 只读 Stash、无限弹和旧 RaidSettlement 不是产品终态，不得增加新消费者。
- 生产 Alpha 只通过 Profile Deploy/Settlement 事务进入 Raid；不得重新引入 Profile 与 V0 库存的资产复制桥。
- 生产射击不得重新引入可渲染/可碰撞 Projectile 场景实体；武器、伤害、存档和 App 只能消费射击领域值、逻辑飞行投影与 HitResult。
- 普通命中最终不显示准星 X；爆头/弱点必须由击发准星意图与真实命中部位双重验证的领域结果驱动，App 不得猜测。当前敌人尚无数据化弱点区域，因此只有领域接口预留，不伪造弱点内容。
