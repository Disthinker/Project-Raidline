# Project Raidline 当前状态

最后核对：2026-08-20。

## Git 与交付基线

- `origin/main@ea918ab` 已包含完整 Core Extraction Alpha、基础防具/命中部位以及流血、疼痛与战地医疗；PR #62 的精确 head CI 与用户正常游玩验收均已通过。
- 当前开发分支：`codex/survival-loadout-durability-malfunction-repair`，从干净的 `origin/main@ea918ab` 创建；交付使用 Draft PR #63。
- 当前活动计划：`doc/exec-plans/active/survival-loadout-weapon-condition-maintenance.md`。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main；正式 Grab/Scratch/Bite 图像及所有新正式美术/音频生产继续暂停。

## 当前产品里程碑

Core Extraction Alpha 与前两个 Survival Loadout 切片已接受。当前里程碑进入 **Survival Loadout：武器耐久、故障与维护**；外部 GDD 继续只读，本仓库 ExecPlan 是该垂直切片的实施范围合同。

1. **Persistent Base**：PR #58 已合入，Profile/AssetRegistry、可行走 Base、Stash/三槽配装、固定经济/救济、schema v1 与跨进程恢复成为接受基线。
2. **Extraction Loop**：PR #59 已通过本地自动化、exact-head CI 与用户 7/7 集中真实窗口验收，并以 merge commit `ed45baa` 进入 main。
3. **Alpha Hardening**：PR #60 已以 merge commit `50849d5` 进入 main；本地 645/645、精确 head CI 与用户最终正常游玩验收通过。
4. **基础防具与命中部位**：PR #61 已由用户正常游玩验收，并以 merge commit `733b597` 进入 main。
5. **流血、疼痛与战地医疗**：PR #62 已通过 exact-head CI 和用户正常游玩验收，并以 merge commit `ea918ab` 进入 main。
6. **武器耐久、故障与维护**：whole-weapon 状态、Stovepipe、鼠标清障、Base/Raid 维护、schema v5 与库存拖放已经接通；正在冻结交付候选。

每个宏切片内部按领域、服务、客户端和证据形成可回滚提交，但不再为单个技术边界中断玩家功能交付。人工验证统一放在自动化和 CI 之后，由用户执行。

## 已接受能力

- `MainMenu → Base → Raid → RaidResult → Base` V0 流程和进程内多局会话。
- RL-INV-001/002/003：原子交换、Ctrl/Shift 锁定数量拖拽、同定义堆叠合并与 60 发上限。
- `ShotCommand → ShotResolution → HitResult` 窄边界；Projectile 只作为 GameplayWorld 内部 V0 表现适配器。
- 四个生产库、唯一业务源码编译所有权、强类型 DefinitionId、版本化 JSON ContentRegistry 和仓库本地 nlohmann-json overlay。
- Persistent Base 的长期 Profile、唯一 AssetRegistry、Stash/三槽配装、固定经济/救济、可行走三设施 Base、首次环境目标链和原子存档。

## 已接受的 Extraction Loop

- `ProfileState::AssetRegistry` 在 Base、Deploy、Raid Loot 与 Settlement 全程唯一拥有资产；装备根、容器子资产、已安装弹匣和 Raid 地面位置均使用稳定实例 ID。
- content v2 提供一张固定 Alpha 地图的 3 组出生/撤离配对、3 组 4～6 敌人部署、10 个三路线 Loot 插槽；每局冻结 6～9 个有效 Loot，PCG32 命名随机流结果写入 pending Raid 快照。
- schema v2 保存当前 HP、弹匣有序弹药、枪膛、Settlement 幂等记录和最近 RaidResult，并能读取旧 pending Raid；新生产 Deploy 不再把运行中 pending Raid 覆盖到磁盘。schema v1 可显式迁移。
- Base 与 Raid 共用按住拖拽库存交互；格子移动/交换/堆叠/配装均由领域预览和命令提交。Base 可将弹药拖到弹匣即时压弹、将弹匣拖到武器安装并按条件自动上膛；Raid 可拖动弹药到弹匣执行 0.2 秒/发的可中断压弹，也可拖动指定弹匣到武器并执行 2 秒换弹。
- 卸弹、显式上膛和 Medkit 使用物品右键情境菜单，不再依赖 `FILL MAG / INSTALL / CHAMBER / USE MED` 等验收按钮。Base 卸弹即时回到 Stash；Raid 弹匣卸弹为 3 秒可中断动作，完成时原子写入背包或胸挂通用格。`F`/`Ctrl+右键` 保留为 Base 快速转移捷径；可穿戴物在对应栏位为空且领域查询合法时优先快速装备，否则沿用容器转移。
- 玩家为 100 HP；Medkit 每件 3 次、每次恢复最多 30 HP，Raid 内治疗 5 秒且中断不消耗。
- Alpha Raid 无硬时限；E 拾取真实 Loot，随身库存可移动和整理，打开时禁止射击/换弹/开始治疗但允许普通移动。
- 3 秒撤离成功保留合法随身资产与 HP；死亡和主动放弃全损并恢复 100 HP。关闭程序或异常退出不会结算，重开后加载出击前的完整 Profile；正式成功/失败结果仍使用唯一 Settlement ID 幂等提交。
- Raid 世界支持 Shift 奔跑，速度为普通移动的 1.5 倍；当前不引入耐力条、负重或复杂移动消耗。
- RaidResult 显示结果、成功带回物和货币变化；失败不生成丢失物清单。生产 Alpha 路径不再使用 V0 柜体、无限弹或 Timeout 结算。

## 当前自动化证据

- Windows Debug 当前树全目标构建成功，`Project_Raidline.exe` 已生成但未由开发代理启动。
- ProfileCombatDomain、ContentRegistry、SaveRepository、HitResolution、GameplayWorld、InventoryDomain、RaidLifecycle 与 AlphaExtractionSession focused 通过。
- PR #61 的 Windows Debug 全目标、663/663 CTest、exact-head Windows/Ubuntu CI 和用户正常游玩验收均通过。
- PR #62 的医疗切片 Windows Debug、680/680 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收均已通过。
- 新长序列自动化覆盖 10 次混合成功/失败 Raid、至少 3 次跨进程重载、三组出生/撤离、三组敌人部署、三路线 Loot、重复 Settlement 和保存失败阻断。
- 当前武器状态切片 Windows Debug 全目标构建与全量 CTest 697/697 已通过；新增 focused 覆盖射击磨损、可靠性分级、Stovepipe、清障、维护原子性、schema v5 与旧档迁移。Draft PR #63 初始候选的 exact-head Windows/Ubuntu CI 已通过；耐久显示与动作慢走反馈修订正在重新冻结候选。开发代理未启动游戏。

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
- 玩家射击按碰撞落点稳定解析 Head/Torso/Legs 并写入 HitResult；普通命中不显示 X，爆头/弱点才显示短促专用标记。受击边缘反馈区分普通伤害与护甲实际减伤。

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

## 尚未完成

- 武器耐久、故障与维护：反馈修订的 exact-head Windows/Ubuntu CI 与用户正常游玩验收。
- Rifle 当前只启用 Stovepipe；Misfire/Double Feed 需要通用的 Raid 动态地面弹药所有权，不能静默销毁或凭空生成退膛/抛出弹药。
- 护甲维修、维修 NPC、组件级耐久和改枪台后续独立切片，不在当前武器闭环扩张。
- 疼痛叫声的墙/门遮挡等待正式空间遮挡查询；当前只提供有消费者的距离刺激，不能扩张为通用音频事件总线。
- 旧 V0 `ItemId`/`ItemInstance` 与旧 GameplayWorld 路径仍保留给历史回归；生产 Alpha 已绕过，后续按消费者安全退场。
- Week29 代码反馈独立整理。
- 正式攻击动画及所有新正式美术/音频生产。

## 明确停止扩展的 V0 合同

- 3 HP、180 秒直接失败、V0 只读 Stash、无限弹和旧 RaidSettlement 不是产品终态，不得增加新消费者。
- 生产 Alpha 只通过 Profile Deploy/Settlement 事务进入 Raid；不得重新引入 Profile 与 V0 库存的资产复制桥。
- 普通命中最终不显示准星 X；爆头/弱点反馈等待命中部位领域合同，App 不得猜测。
