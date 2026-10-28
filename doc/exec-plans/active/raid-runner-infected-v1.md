# 迅捷感染者 v3：已验收，正在交付

**用户已验收候选 `08178f45f1dd`，并授权提交、推送和合并。** 本次只交付该候选，不开始下一任务。

- state：`accepted_delivering`；task_id：`raid-runner-infected-v1`；plan_version：`RL-RAID-RUNNER-INFECTED-v3`。
- accepted_candidate_id：`RL-RAID-RUNNER-INFECTED-v3-local-08178f45f1dd`；acceptance_source：2026-10-28本主控用户“迅捷感染者 v3 验收通过，请完成提交、推送和合并。”。
- baseline：`77b117d1492d975a2861357a04c60a652d9e73dc`；branch：`codex/raid-runner-infected-v1`。599份源码、同一程序与固定入口核对一致；本地2874/2874，0失败/跳过，原六项及新增混合敌群3ms通过。
- 交付范围：56个产品／测试文件及本任务计划、试玩清单、CURRENT_STATE本片入口，共59文件。提交中的CURRENT_STATE以HEAD完整原文作历史后缀，其他本地历史记录保留但不夹入提交。
- 已重新保全五目录当前修改、验收后最新玩家文件、程序和固定快捷方式，见`delivery/before.json`。保留试玩产生的存档与升级源备份，不回退到开发前数据；提交钩子正常执行，钩子改写的无关文档在归档后按交付快照恢复。
- feature_commit／PR／merge_commit：尚无。准确PR及合并后main双平台CI待执行，不以本地检查代替；不追加仅复制CI状态的提交。

桌面 **Project Raidline Debug** 继续使用同一已验收程序。证据根：`C:\Users\Administrator\Documents\美术资源生成\tmp\raid-runner-infected-v3-20261028`。没有新增正式资源生产，不改变地图权限或已验收玩法。

<details>
<summary>已验收本地候选与开发记录（历史，当前交付状态以上方为准）</summary>

# 迅捷感染者 v3：待用户验收

**已完成获批开发，候选 `08178f45f1dd` 已冻结。** 仍用桌面 **Project Raidline Debug** 和原存档，按[四项试玩清单](../../acceptance/raid-runner-infected-v1.md)复验。

- 新生成的普通边境交换站，每个道路伏击组首名替换为迅捷者，共2～3名；8生命、无护甲，常态90、确认可见目标的合法追击270。
- 原攻击前摇移动、74.25扑击、攻击配额和其他玩法保持。只在现有视野内显示“迅捷”，没有新增掉落或正式美术。
- 旧档、旧对局不补敌或重抽；升级前保全主备原字节。固定入口、玩家文件和原有未提交工作保全检查通过。
- Windows Debug全目标构建、定向559/559、全量2874/2874通过，0失败/跳过；原六项3ms检查不变，新32／100混合敌群p95为2.0803／2.6739ms。
- 当前等待人工验收；未启动游戏、提交、推送、建PR或合并。UI专项保持结束，不启动下一任务。

<details>
<summary>候选身份、实际检查与回退证据</summary>

- task_id：`raid-runner-infected-v1`；plan_version：`RL-RAID-RUNNER-INFECTED-v3`；state：`awaiting_user_acceptance`。
- approval_source：2026-10-28本主控用户明确批准上述版本实施，完成后先提供验收清单，暂不提交、推送或合并。没有模拟审批或策划代批。
- candidate_id：`RL-RAID-RUNNER-INFECTED-v3-local-08178f45f1dd`；accepted_candidate_id／acceptance_source：无。候选指纹绑定完整源码、测试、内容与现有资源，不把旧HEAD当作本候选提交。
- branch：`codex/raid-runner-infected-v1`；HEAD与基线均为`77b117d1492d975a2861357a04c60a652d9e73dc`。实际源码／构建目录`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1`。
- 完整源码SHA256：`08178f45f1dd1b633a2b454582aaca54d534ee0f01a3a7c46ef35658962370e1`；程序SHA256：`939562d7d42c422b1943f5cbe4dff2b0e9db23b511915d979c913731908e710e`。源与运行目录内容JSON一致：`ee821a1df5ca62905161347336fffefcdd9ebe5980935698ec61d18502980959`。
- schema58／content73／rules31及旧30、layout4已实现；新规则冻结所有初始敌人的类型、生命、移动与原攻击版本，未知／缺失／错组／错活动拒绝。旧版本未声明覆盖时保持旧实例行为。Base、防守、特殊行动、室内、其他地图和高危追加不生成迅捷者；已生成类型进入高危不重置。
- 攻击版本1显式解析原动作、伤害、冷却与前摇移动；跨空间、复制、稳定ID死亡清理与旧自定义AI回归通过。单独的无活动版本暂态检查点，若删除variant且追击旗标也为false，无法与合法旧自定义8HP实例区分；当前Raid持久DTO规则31已强制字段，Base防守不接受Runner，本片没有新增Raid运行态持久化。
- 初次检查发现历史测试用当前部署贴旧标签，以及新密集视野成本超标；均保留失败日志后修正。历史测试改为部署与指纹计算前用真实旧内容生成，生产拒降写校验与原断言保持。密集查询使用保守角度桶、闭边界／线投影筛选及连续只读数据，保持原射线、候选顺序和交点算术结果；4项与main旧慢实现逐位等价测试通过（密集220、薄墙擦边、方向环绕、远坐标、灯光接缝与三种格大小）。
- 原六项3ms测试源码、阈值、负载及串行隔离逐项核对不变；新测试保持32／100敌人、220近障碍、真实GameSession更新／观察／逐敌查询／死亡清理、150有效样本p95≤3ms，没有缩减负载、移出消费者或改分位。临时额外诊断已删除，正式CSV保留总时间与分段计数。
- 验证使用x64 VS Shell／UTF-8／windows-debug。最终全目标构建`build-Incremental-6.log`；定向`focused-3.xml`，全量`full-2.xml`；原失败与中间probe日志保留。新增46项测试，基线2828项全保留。
- 证据根：`C:\Users\Administrator\Documents\美术资源生成\tmp\raid-runner-infected-v3-20261028`。`candidate-source.json`含完整源码／程序／内容指纹、CTest注册与串行性、CSV和五目录保全；`before.json`与`original-work`保留开工字节；`preimplementation-program`保存旧程序及内容72。21份玩家文件、固定快捷方式和全部无关脏文件字节未改，其他分支／HEAD及暂存区保持。
- 未提交／推送／建PR，因此本候选没有远程CI结果；前片双平台2828项只证明基线，不冒充本候选CI。人工接受后再按用户交付授权核对准确提交的双平台CI。
- 回退：未升级保存时可回到保全的旧程序／内容72；升级后先保全最新玩家文件，再按不可覆盖的升级源备份处理。不要用旧程序直接读取schema58或覆盖之后的游玩成果。

</details>

<details>
<summary>已批准方案及策划来源（启动历史，当前状态以上方为准）</summary>

# 边境交换站迅捷感染者 v3

**方案：`RL-RAID-RUNNER-INFECTED-v3`。状态：用户已批准，实施中。**

增加一种“接近更快、生命更低”的敌人，让玩家在优先击杀、冲刺脱离、诱饵绕行和手雷预判之间作选择。沿用现有道路遭遇和战斗，不增加敌人总数。

## 本次会得到什么

| 内容 | 具体方案 |
| --- | --- |
| 类型 | 迅捷感染者，定义ID `enemy.infected.runner`；8生命、无护甲，普通型目前为12生命。 |
| 常态移动 | 巡逻、搜索、纯声音调查、记忆导航和支援移动的基准速度为90。 |
| 确认追击 | 当前看见玩家、已按原感知进入追击状态且实际有追击移动决策时，基准速度为270。玩家基础步行240、冲刺360；负重、举枪、转向、碰撞和既有减速照常作用。 |
| 攻击 | 沿用原抓击、抓抱和撕咬，原前摇、伤害、冷却、失衡及攻击配额保持；Grab扑击仍为74.25距离、0.55秒Active阶段。 |
| 出现位置 | 仅新生成的普通“边境交换站”室外初始敌群，每个道路伏击组首名普通成员替换为迅捷者，共2～3名。 |
| 识别 | 复用现有合法感染者Sprite和P0声音，只在当前可见时显示短名“迅捷”；不新增图片或声音资源。 |

270是该状态的移动基准，不代表贴墙、转向、受击或分离避让时每帧都移动同样距离。旧普通型和护甲型的行为保持。

### 何时加速

迅捷者必须处于非攻击的Idle阶段，原感知已确认当前可见目标，移动决策确实在追击，而且职责为Pressure或Engage，才使用270。失去当前视觉确认、调查声音、按记忆行进、Searching或Support时使用90；不会因听声直接获得玩家位置。

Pressure允许继续接近，Engage另有近身攻击名额。加速只改变移动，不授予`canStartAttack`，不抢攻击名额。不能把270仅绑在Engage上，否则远处追击大多仍会使用慢速。进入攻击动作后沿用原攻击配置，不加快或延长扑击。

## 出现与收益边界

在现有道路伏击组完成组数、人数和位置分配后，替换每组指定的第一名成员。保持总人口、位置、尺寸、组身份、其他成员、生成随机流、地图几何和Loot。没有额外抽取，不在活动对局中删除普通敌人再生成新敌人。

高危追加、独立室内、医院及其他地图、基地外围、防守和隔离测试场继续使用原敌人；已生成的迅捷者进入高危后不重置。自力寻回、据点恢复、地块清剿、外围清剿不执行迅捷替换；资格须校验完整活动模式，不能只判断地图ID。

击杀只复用退休、计数／评分和清理，不新增可搜索尸体、容器、专属奖励或Loot。原三名资源护甲守卫的搜尸试点保持。地图访问权限、资产、撤离、失败损失和异常退出规则保持。

## 存档与版本

拟将内容72推进为`raid-runner-infected-content-73`，保存schema57推进为58；新增普通边境交换站规则`content-frontier-runner-infected-31`，保留旧`content-frontier-corpse-search-30`及更早规则。几何没有变更，布局版本仍为4。

新规则在出击时冻结每名敌人的稳定变体身份、生命、常态／追击移动能力及明确版本的原攻击配置。原攻击配置的版本含义必须固定，恢复不能用后来修改的默认值或最新平衡定义覆盖快照；不要另建热更新或通用事件框架。

旧档、旧pending及旧检查点没有新字段时走明确的旧版本解码和原验证合同，不补迅捷者、不重抽部署、不把原普通型变快。旧实例未声明追击覆盖时继续使用该实例原有的`normalMoveSpeed`，不全局硬填新速度。新规则缺字段、类型未知、速度非法或成员关系不符须拒绝，不能靠运行时默认值掩盖缺失。

保存升级前保留主档及备份的原始字节，验证失败不能覆盖源文件。旧内容72及更早兼容链、资产ID和关系、原pending回滚及重复结算保持；不允许用旧schema写出无法表达的新类型。当前失物记录不保存敌群，寻回会重新生成遭遇：本片明确不在该生成路径新增迅捷者，不改变寻回资产或窗口。

## 开发完成后怎么验收

仍使用桌面 **Project Raidline Debug** 和原存档，进入**新生成的普通边境交换站**。旧pending、医院或测试场中无需寻找该类型；有既有敌情资料时可用M辅助查看原敌群信息，不增加定位按钮。

| 普通操作 | 应看到的结果 |
| --- | --- |
| 沿道路接近伏击组 | 可见的新类型标有“迅捷”，普通敌人不误标；未见或被遮挡时不显示。 |
| 被发现后后退，再冲刺拉开距离 | 迅捷者的确认追击明显更快，冲刺可拉开；扑击仍有原前摇和距离。 |
| 绕到实体墙后或用原诱饵干扰 | 遮挡、搜索与调查按原规则处理，不隔墙追踪或显示标记。 |
| 优先射击或使用原手雷处理 | 生命更低，受击与死亡正常，不增加额外可搜尸体或奖励。 |
| 进出建筑后继续行动、撤离并重开 | 敌人不复活，原搜尸、准星、菜单、资产和结算保持；后续新出击仍按本片规则生成。 |

不要求玩家量世界距离、故意受伤或损失装备。速度、生命、数量、资格、攻击时序和旧档由自动化精确验证；交付时提供一份更短的试玩清单。

<details>
<summary>代码接线、检查与风险（开发记录）</summary>

### 已核查的实际入口

实际源码为`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1`。以下均为拟修改，尚未执行。

| 文件／模块 | 计划接线与原因 |
| --- | --- |
| [src/enemy_combat_definition.h](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/enemy_combat_definition.h>)、[src/content_registry.h](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/content_registry.h>) / [src/content_registry.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/content_registry.cpp>)、[assets/content/v1/core.json](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/assets/content/v1/core.json>) | 新增独立敌人移动能力和受限道路成员覆盖，保持物品、供应与Loot条目原值。现有`armored_member_definition`限带甲Guard，不能借用它配置迅捷者。 |
| [src/raid_lifecycle.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/raid_lifecycle.cpp>)、[src/profile_state.h](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/profile_state.h>) / [src/profile_state.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/profile_state.cpp>) | 普通Frontier且`regionalMissionCount==0`才选择新31规则；部署后按原稳定成员索引冻结替换，严格验证每组唯一首名、身份、8生命、90／270、原攻击配置、无甲、尺寸与空间。其余成员保留原部署校验，不把`districtEnemyMatches`改成忽略生命。 |
| [src/enemy_ai.h](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/enemy_ai.h>) / [src/enemy_ai.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/enemy_ai.cpp>)、[src/enemy.h](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/enemy.h>) / [src/enemy.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/enemy.cpp>)、[src/gameplay_world.h](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/gameplay_world.h>) / [src/gameplay_world.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/gameplay_world.cpp>)、[src/game_session.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/game_session.cpp>) | 从冻结配置构造Enemy；新增明确的追击移动原因／能力消费，覆盖Pressure与Engage但保留攻击授权。旧未声明覆盖的配置仍走原移动；90常态仍满足原`normalMoveSpeed < attackMoveSpeed`，270不塞进旧`attackMoveSpeed`字段。 |
| [src/combat_runtime_checkpoint.h](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/combat_runtime_checkpoint.h>)及现有检查点JSON适配、[src/save_repository.h](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/save_repository.h>) / [src/save_repository.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/save_repository.cpp>) | 显式记录并恢复变体和移动能力，保留原攻击配置版本、稳定CombatTargetId、HP、位置与动态攻击状态。旧检查点按原版本恢复；新schema58／内容73显式迁移、源保全、指纹及篡改拒绝。 |
| [src/raid_corpse_search.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/raid_corpse_search.cpp>)与[src/profile_state.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/profile_state.cpp>)中的规则判定 | 共享尸体、凭证、生态检查明确支持旧30和新31，不直接把原30常量改成31。原三守卫来源、容器、冻结Loot与凭证撤离保持。 |
| [src/app.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/app.cpp>)、[src/ui_localization.cpp](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/src/ui_localization.cpp>) | 在已有敌人可见性裁剪之后，按真实变体画“迅捷”短名，复用文字缓存；不从HP、颜色或名称推断战斗规则，不泄露暗区或新增大面板。 |
| `tests/`、[CMakeLists.txt](<E:/WorkPlace/Projects/C/Project Raidline-ui-inventory-presentation-v1/CMakeLists.txt>) | 拟新增`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1\tests\test_runner_infected.cpp`、`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1\tests\test_runner_infected_save.cpp`及`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1\tests\test_runner_infected_performance.cpp`，或按既有分层拆分；登记有真实消费者的测试，不新增游戏内验收按钮。 |

### 具体实施顺序

1. 定义与冻结：静态能力、普通出击资格、稳定成员替换及严格快照校验先通过。
2. 运行时：真实Pressure／Engage追击、非视觉90、攻击配额／原扑击、空间往返与死亡一次消费通过。
3. 保存：schema58、内容73、规则31兼容旧30、旧pending／检查点往返和失败保全通过。
4. 最小中文识别与联合回归：显示遵守现有遮盖，完整检查通过后冻结本地候选，等用户正常游玩验收。

### 自动化与退出条件

- 新定义非法值、未知变体、错误活动、非首名／重复覆盖、篡改HP／速度、错空间与组关系均拒绝，失败保持原参与者。
- 多seed对照原人数、位置、尺寸、组成员索引、几何、资源、Loot及随机流；每局仅2～3名迅捷者，三资源守卫和凭证行为不变。
- 用真实Enemy更新验证常态90、可见Pressure／Engage追击270、无攻击名额仍可正常追击、失去视觉／纯听声／搜索／Support回到90。保留转向、受击减速、碰撞、Scratch／Grab→Bite时序和最大同时攻击数。
- 旧普通型／护甲型、医院、室内、特殊行动、基地／防守、旧自定义AI配置及旧检查点不变；新类型复制、空间往返、首／中／末死亡清理不丢配置、不复活、不重复评分。
- 旧schema57内容72及更早档案、旧pending和损坏主档／备份恢复，升级源不可覆盖；新schema往返、缺字段、未知版本、错误来源、保存失败、异常退出回滚及重复结算。
- 使用windows-debug重建全部受影响目标、相关回归和完整注册CTest。保留基线2828项，**原五项及手雷共六项3ms负载、分位、阈值和串行隔离保持**；另加32／100混合类型的真实追击、绕障、观察与死亡清理p95≤3ms检查，不以静态参数测试替代消费者。
- 验收前不提交、推送或建PR。用户验收后再按其交付授权核对准确HEAD的Windows／Ubuntu CI；自动检查不代替人工验收。

### 风险与回退

风险集中在速度条件接错、追击特权误扩散、高速穿障、旧快照被新平衡覆盖和标记泄露。以真实位移／时序／拒绝对照覆盖，保持模块单体与既有EnemyRoster生命周期，不扩AI架构。

批准后拟从已验收main `77b117d`创建`codex/raid-runner-infected-v1`；目前保持原分支、原程序和固定入口。未保存升级前可回退本片代码与程序；保存升级后必须先保全最新玩家文件，再按升级点源备份恢复，不用旧程序直接读取schema58，不覆盖之后的游玩成果。

</details>

<details>
<summary>状态、策划来源与只读证据</summary>

- task_id：`raid-runner-infected-v1`；plan_version：`RL-RAID-RUNNER-INFECTED-v3`；state：`implementing`。
- approval_source：2026-10-28主控用户明确批准RL-RAID-RUNNER-INFECTED-v3实施，并要求保留已验收UI／玩法、原存档、固定入口和3ms门槛；完成后先提供验收清单，暂不提交、推送或合并。candidate_id／accepted_candidate_id／acceptance_source：无。
- baseline：已验收main `77b117d1492d975a2861357a04c60a652d9e73dc`（PR196）；已fetch核对仍为该版本。实际HEAD `1a446c5b9ba0c357d09297b0a77e0ca1239e8c8f`、分支`codex/raid-fragmentation-grenade-v1`，产品源树与main相同，历史未提交文档另行保留。
- verification：本轮仅只读代码／GDD核查及方案文档检查，未实现、构建、运行测试或启动游戏。前片PR及main双平台2828/2828仅证明基线，不是迅捷者的验证结果。
- 策划原线程：`019feff4-477c-75c2-bf31-3f09ff1e3891`，host `local`，“策划线程”。request_id：`main-next-function-after-grenade-20261028-77b117d`；task_id：`next-function-after-grenade`；version：`RL-NEXT-FUNCTION-AFTER-GRENADE-20261028-v1`；baseline：同上main。
- 有效最终回复：role=`planner`、status=`proposed`；turn `01a18045-f34d-7a53-9fe9-9ed9eddfa773`；message `msg_03470a7a4dc51bec016ae159c3521c8191a09fbc66607e95c1`。真实目标及四元组已匹配；最终v3替代未批准的v1／v2候选，历史回复不授权实施。
- 主控补齐schema58、内容73、新规则31／旧30及布局4合同；已核查失物记录不保存敌群、寻回重新生成，明确排除对应生成替换。未修改GDD，不唤醒美术生产，不更改信任、权限或模型。
- 证据根：`C:\Users\Administrator\Documents\美术资源生成\tmp\next-function-after-grenade-20261028`，含原生请求／补充／发送回执、v1／v2历史、v3完整实际回复及五目录／源文件／玩家数据／程序／入口的前后保全。pending_request：无，等待用户批准本版本。

</details>

## 实施启动（2026-10-28）

已按本主控用户的明确批准，从已验收main77b117d建立`codex/raid-runner-infected-v1`。开工前已重新保全五目录脏文件、21份玩家数据、旧程序／内容72和固定入口，证据目录`C:\Users\Administrator\Documents\美术资源生成\tmp\raid-runner-infected-v3-20261028`。前文拟实施与待审批文字保留为方案历史，以上方状态和本段为准。本轮连续完成获批范围及检查，不启动游戏、不提交／推送／合并。

</details>

</details>
