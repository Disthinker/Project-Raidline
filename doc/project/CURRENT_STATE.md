# Project Raidline 当前状态

最后核对：2026-08-20。

## Git 与交付基线

- `origin/main@7877d71` 已包含完整 Core Extraction Alpha、Survival Loadout 已接受切片，以及 Combat 逻辑弹道与落点反馈 v1；PR #66 已通过精确 head CI 和用户正常游玩验收后合入。
- 当前开发分支：`codex/combat-aim-handling-ads-v1`，从干净的 `origin/main@7877d71` 创建。
- 当前活动计划：`doc/exec-plans/active/combat-aim-handling-ads-v1.md`。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main；正式 Grab/Scratch/Bite 图像及所有新正式美术/音频生产继续暂停。

## 当前产品里程碑

Core Extraction Alpha、五个 Survival Loadout 切片与 Combat 逻辑弹道 v1 已接受。当前里程碑进入 **Combat：准星操控与基础开镜 v1**；外部 GDD 继续只读，本仓库 ExecPlan 是该垂直切片的实施范围合同。

1. **Persistent Base**：PR #58 已合入，Profile/AssetRegistry、可行走 Base、Stash/三槽配装、固定经济/救济、schema v1 与跨进程恢复成为接受基线。
2. **Extraction Loop**：PR #59 已通过本地自动化、exact-head CI 与用户 7/7 集中真实窗口验收，并以 merge commit `ed45baa` 进入 main。
3. **Alpha Hardening**：PR #60 已以 merge commit `50849d5` 进入 main；本地 645/645、精确 head CI 与用户最终正常游玩验收通过。
4. **基础防具与命中部位**：PR #61 已由用户正常游玩验收，并以 merge commit `733b597` 进入 main。
5. **流血、疼痛与战地医疗**：PR #62 已通过 exact-head CI 和用户正常游玩验收，并以 merge commit `ea918ab` 进入 main。
6. **武器耐久、故障与维护**：PR #63 已通过 exact-head CI 和用户正常游玩验收，以 merge commit `b8ddbe3` 进入 main。
7. **多武器配装与切换**：PR #64 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以 merge commit `4c16596` 进入 main。
8. **防具维护**：PR #65 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以 merge commit `755fa00` 进入 main。
9. **逻辑弹道与落点反馈 v1**：PR #66 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以 merge commit `7877d71` 进入 main。
10. **准星操控与基础开镜 v1**：实际准星、稳定性随机散布、五项武器属性、基础开镜、奔跑举枪与射程反馈正在当前分支形成完整玩家闭环。

每个宏切片内部按领域、服务、客户端和证据形成可回滚提交，但不再为单个技术边界中断玩家功能交付。人工验证统一放在自动化和 CI 之后，由用户执行。

## 已接受能力

- `MainMenu → Base → Raid → RaidResult → Base` V0 流程和进程内多局会话。
- RL-INV-001/002/003：原子交换、Ctrl/Shift 锁定数量拖拽、同定义堆叠合并与 60 发上限。
- `ShotCommand → ShotResolution → HitResult` 窄边界；生产路径已使用非场景实体的 `LogicalBallisticFlight`，冻结本发并连续扫掠目标。
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
- PR #63 的最终 exact-head Windows/Ubuntu CI 与用户正常游玩验收已通过并合入 main。
- PR #64 的最终 exact-head Windows/Ubuntu CI 与用户正常游玩验收已通过并以 `4c16596` 合入 main。
- PR #65 的防具维护 Windows Debug 全目标、718/718 CTest、exact-head Windows/Ubuntu CI 和用户正常游玩验收均已通过并合入。
- PR #66 的逻辑弹道切片已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，并以 `7877d71` 合入 main。
- 当前准星操控切片已在 Visual Studio Developer Shell、x64 host/x64 target 下完成 Windows Debug 全目标构建；全量 CTest 734/734 与 PR #67 代码提交 `92f6d94` 的 exact-head Windows/Ubuntu CI 通过。开发代理未启动游戏。

## Combat 逻辑弹道与落点反馈 v1 当前实现

- `ShotCommand` 新增最大飞行距离；`ShotResolution` 在成功击发时冻结规范化方向、速度、最大距离和最终落点，后续鼠标或角色移动不能修改本发。
- `GameplayWorld` 不再创建可渲染/可碰撞的 Projectile 场景对象；`LogicalBallisticFlight` 只保存本发冻结值和已飞距离，不具有资产、场景或存档身份。
- 弹道以现有 1200 世界单位/秒直线推进并返回本帧实际飞过的线段；命中解析连续扫掠、选择最近活目标，高速大帧不会因离散采样穿过薄目标。
- 未命中敌人时，弹道到达冻结准星落点才产生一个 `World HitResult` 和代码 impact 粒子。App 的短轨迹钳制在已经飞过的区段，不能提前显示未来路径。
- 普通命中与 World 命中不显示准星 X；爆头/弱点继续只由领域 `HitSemantic` 触发专用标记。没有生成、发布或接入新美术/音频，也未修改 manifest。

## Combat 准星操控与基础开镜 v1 当前实现

- 武器实际准星以受 `handlingSpeed` 限制的最大角速度追随鼠标；玩家朝向、准星表现和成功击发都消费同一个实际准星，而不是原始鼠标点。
- 实际准星只确定总体射击方向；每发在当前散布圆锥内使用确定性随机偏移冻结最终方向。`stability` 决定连续击发的散布增长，`accuracy` 决定最小/最大散布，普通移动与距离会进一步放大散布。
- `recoilControl` 决定击发时准星中心跳动；跳动不会随时间自动回中，必须用鼠标反向修正。停火后只恢复散布，不自动抹除准星中心偏移。
- 按住鼠标右键基础开镜；开镜降低移动速度并改善散布/稳定性。开镜击发不显示普通短轨迹；换弹保持开镜输入，但在换弹期间把散布锁定到最大。
- 奔跑不能直接击发；奔跑中按射击会先结束奔跑，经过由操控速度决定的短促举枪准备后只提交一次原射击意图，准备完成前不消耗弹药。
- 超过有效射程后散布逐渐恶化；超过最大射程时准星变红，命中只保留 25% 基础伤害。五项属性、基础伤害和射程来自版本化 WeaponUse 内容定义，App 不按名称猜测。

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

- 准星操控与基础开镜 v1：PR #67 已通过代码提交 exact-head Windows/Ubuntu CI，待用户正常游玩验收。
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
- 普通命中最终不显示准星 X；爆头/弱点反馈等待命中部位领域合同，App 不得猜测。
