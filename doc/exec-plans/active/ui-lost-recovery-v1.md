# 行动失物记录与寻回界面统一 v1

**候选 1b0c6c638de5 已验收，正在交付。** 用户已明确授权提交、推送和普通合并；本轮不扩展或启动下一项开发。

- task_id：ui-lost-recovery；plan_version：RL-UI-LOST-RECOVERY-v1；state：accepted_pending_delivery；pending_request：无。
- candidate_id 与 accepted_candidate_id：RL-UI-LOST-RECOVERY-v1-local-1b0c6c638de5。
- acceptance_source：2026-10-22 主控直接人类原文“验收通过，请完成提交、推送和合并。”。这份批准对应上一轮提供的已冻结候选，源自真实人类消息。
- 冻结的465项源码／测试／CMake、150项二进制、程序、资源和固定入口已核对。实际源码交付12项，加当前状态、本记录和验收清单共15项；19份其他脏文档保持。
- 已有Windows本地2507/2507、专项154/154和原五项3ms通过；准确PR及普通合并后的main双平台CI尚待执行，不能用基线CI代替。
- 证据目录：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-lost-recovery-v1-delivery-20261022。正常运行提交钩子，不跳过检查、不重写历史、不改信任／权限／模型；不启动游戏或访问真实存档。

<details>
<summary>实施、验收候选与已批准方案（历史记录）</summary>

以下“待用户验收／未提交”等属于验收前历史；当前以上文和后续交付事实为准。

# 行动失物记录与寻回界面统一 v1

**RL-UI-LOST-RECOVERY-v1｜候选 1b0c6c638de5 已完成，待用户验收。**

原“行动失物记录与寻回”页面已整理为“失物记录、NPC 委托”两个标签；保留原委托和自行寻回规则。继续使用原桌面 **Project Raidline Debug**，按[简短试玩清单](../../acceptance/ui-lost-recovery-v1.md)验收。

| 内容 | 结果 |
| --- | --- |
| 记录与物品 | 全部记录按原顺序分页；完整只读清单滚动显示短名称、数量和等比图片，长名称可悬停查看。 |
| 费用与窗口 | 服务标价和实际可执行原因分开，余额不足不显示成免费；游戏内耗时与行动结算次数分开，0次正确表达最后机会。 |
| 委托状态 | 进行中隐藏实际结果；完成才可领取；容量不足或保存失败保留完整任务，取消需页内确认且不退款。 |
| 自行寻回 | 选择来源地图，清理失效情报和确认，返回出击页；只设置目标，不自动出击、扣款或发物品。 |
| 安全交互 | 按下与抬起绑定稳定记录／任务和当前状态，关闭、失焦、离窗、缩放或状态变化取消旧操作。原按下执行和旧显示分支已移除。 |
| 显示与验证 | 中英文三档窗口66张离屏图已复查，长标题和英文确认按钮问题已修正；专项154/154、完整2507/2507、原五项3ms通过。 |

原存档格式、领域规则、收费与时长、概率、领取和异常退出政策未改。没有生产新资源，未启动游戏或访问真实存档，未提交、推送或合并。自动化通过不代表人类验收。

<details>
<summary>候选、实际变更与验证证据</summary>

- task_id：ui-lost-recovery；plan_version：RL-UI-LOST-RECOVERY-v1；state：waiting_human_acceptance；pending_request：无。
- approval_source：2026-10-22主控直接人类原文“同意该方案，请实施开发”，明确指向本方案；candidate_id：RL-UI-LOST-RECOVERY-v1-local-1b0c6c638de5；accepted_candidate_id、acceptance_source：无。
- 实际目录：E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1；分支codex/ui-lost-recovery-v1；baseline／HEAD 58a2ac27c8d366ba3796d92d77c819b370518408。只从已验收main建立新任务分支，没有引入未验收功能；无本地提交或新远端CI。
- 源码指纹：1b0c6c638de5b86485c388962a12227d2437102373ad415de34219b15e7a4d36，465项源码／测试／CMake；程序SHA256：f3481633e0abe3dd9b074346f09078f6124c0add82d7f512cf5d42984375bbbd；当前150项程序／测试二进制有冻结清单。原已验收程序保留在证据目录accepted-program.bin，新程序保存为candidate-program.bin。
- 实际源码集合12项：CMakeLists.txt；src/app.cpp、app.h、app_base_defense.cpp、app_ui_base_deployment.cpp、app_ui_lost_recovery.cpp、ui_lost_recovery_presentation.h/.cpp、ui_localization.cpp；tests/test_ui_base_deployment.cpp、test_ui_localization.cpp、test_ui_lost_recovery.cpp。另有当前状态、本任务记录和试玩清单，共15项。
- app_base_defense.cpp只把警报关闭旧选择字段的一行清理改为resetLostRecoveryUi；这是删除旧UI字段的必要消费者适配，防守规则未变。tryDeployFromBase也只有对应UI清理适配，业务流程原文保持；另10个原输入／准星／镜头／区域函数逐项原文相同。
- 复用lost_raid_domain、recovery_task_domain、game_session、raid_lifecycle、profile_state、content_registry原业务实现，原字节保持。旧出击测试仅更新两项失物子页的操作为新原生事件，原测试名及断言保留。
- Windows构建命令：VS x64 Developer Shell、UTF-8、原VCPKG_ROOT；cmake --preset windows-debug；cmake --build --preset windows-debug --parallel 4。App头文件变化后重建全部受影响目标。首轮旧字段消费者编译错误已修正，不拿失败构建作候选证据。
- 最终专项命令与结果：run_tests.py focused 3，154/154；最终完整run_tests.py full final2，2507/2507，0失败、0跳过，原2467项全保留。新增数量为40。完整检查耗时396.68秒；均绑定tested-source.json中的同一源码和二进制，最终冻结再次核对未变。
- 原五项3ms：RaidLightConeVisibilityTest.PublishedFiveSpacesAndDenseLocalGeometryStayWithinObservationBudget；WeaponLightTest.LampAndNearBackgroundPreparationMeetUnchanged3msBudgetFor32And100Targets；WeaponLightTest.PoweredPublishedSpacesKeepTheOriginalObservationBudget；WeaponLightTest.PoweredFourConsumerQueriesReuseCurrentSnapshotWithin3ms；NoiseDecoyTest.RealPulseAndReturnNavigationKeepThreeMillisecondBudgetFor32And100。原测试文件、计时、负载和门槛保持，在最终完整CTest通过。
- 首轮专项151/151通过；首组66张离屏审查发现标题高度不足两行、1600英文取消确认文字截断，已在批准范围内修正并添加回归。新增悬停全文测试首次153/154，确认客户端绕回目录取短名后改为消费完整记录投影，最终154/154。修正前的完整full-final主动中断，日志保留，不作为通过证据；最终full-final2才是当前候选结果。
- 最终离屏图：qa-ui-lost-recovery-v1-2026102202，共66张，覆盖11种状态×三尺寸×中英文；contacts-2与视觉审查记录保留。关键数字、数量、标题和完整原因有像素或真实测量回归。离屏审查不代替实际窗口和手感验收。
- 19份无关原脏文档和五根其他改动逐字节保持；索引未暂存。固定快捷方式原字节、运行资源74项及DLL7项保持。无Manifest、信任／权限／模型或外部GDD修改。
- 完整证据：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-lost-recovery-v1-20261022；before.json、audit.json、freeze.json、tested-source.json、build-*、focused-3-*、full-final2-*、previews-2.json、visual-review.json。基线PR188与main CI为历史依赖事实，不写成本候选远端验证。
- 回退只撤回本片12项源码与3项记录，恢复程序检查点，保护其他修改和真实存档；不做整树reset、不恢复旧存档以撤销已合法事务。候选现已冻结，实质变更须重建并重新验收。

</details>

<details>
<summary>已批准方案与选题证据（历史阶段）</summary>

以下保留批准前计划，其中“待批准／尚未实施”均为历史阶段；当前以本页上方为准。

# 行动失物记录与寻回界面统一 v1

**RL-UI-LOST-RECOVERY-v1｜待用户批准，尚未实施。**

让玩家看清丢失物品、保留机会、委托费用与领取状态。接着已交付的出击页整理旧子页，沿用现有寻回规则。

入口保持 **对局出击 → 失物记录**，页面标题保持“行动失物记录与寻回”。批准后完成本地候选和简短试玩清单，等待验收；暂不提交、推送或合并。

## 页面怎么调整

沿用已验收的深色面板、灰绿强调、短中文和字号层级。按钮有悬停、按压与短渐变反馈，动效不改变点击范围；物品图片等比显示。

| 标签 | 内容与操作 |
| --- | --- |
| 失物记录 | 左侧按原顺序分页显示全部记录；选中后，右侧显示来源、失败类型、保留机会及完整物品清单。清单可滚动，短名称和堆叠数量清楚对应。 |
| 寻回方案 | 位于选中记录详情内，分别显示 NPC 费用、游戏内耗时、寻回倾向，以及自行寻回目标。使用“委托 NPC 寻回”“设为寻回目标”等短按钮，费用和长说明移到按钮旁。 |
| NPC 委托 | 独立标签显示当前任务、已付费用、剩余游戏内时间及“进行中／待领取”。领取和取消使用不同操作；没有普通记录时，委托仍可访问。 |
| 返回与反馈 | 固定返回入口；完整失败原因在所属区域显示并可滚动。取消采用页内再次确认，明确“不退款”。 |

记录列表按稳定记录 ID 选择，不绑定行号。物品清单只读，不提供拖拽、装备、使用或出售，也不复制物品作为展示库存。资产条目数与堆叠数量分开表达。

“保留机会”与“委托耗时”分开显示：前者按后续行动结算计数，最后机会明确写“下一次行动结算后到期”；后者标为游戏内时间。剩余保留计数为 0 表示最后机会，不能显示成已经到期。

## 保留哪些操作规则

- **委托**仍同时限一个，按原费用扣款。只查看、切标签和选记录不扣款、不转移资产，也不加速时间。寻回倾向不写成保证收益或虚构百分比。
- **进行中**不显示已经冻结的具体回收结果；完成后才显示现有结果汇总。任务完成不等于仓库一定有空间。
- **领取**仍由原事务检查容量。放不下时保留完整任务和资产，不部分领取、不丢弃；成功后进入原独立仓库页，保留装备与安装件的关系。
- **取消**不退款，恢复原记录及全部任务资产，保留原老化进度，不追补委托期间的行动次数。成功后定位恢复的记录。
- **自行寻回**只设置目标并返回出击页，选中来源地图，清除原本应失效的情报选择和出击确认。仍需明确出击；清除目标不删记录。已有 NPC 委托不阻止查看或自行寻回其他合法记录。

余额不足等查询失败时，报价结构中的默认 0 不能显示成免费或无需等待。已经核查原地图的 `recovery.serviceFee / durationMinutes`：费用和耗时取同一来源地图的原服务定义，作为服务标价展示；能否执行、失败原因仍以当前领域报价为准。无有效报价时不显示高低倾向数量，不绕过任何合法性检查。

## 本次边界

只整理本页及往返出击页的客户端接缝。不改失物生成、老化次数、费用、时长、寻回概率、取消退款、领取规则、异常退出政策或存档格式。

不扩展多委托、保险、部分领取、自动寻回、Raid 内缓存或区域路线。复用已批准物品图片和控件，不需要新美术、音效或 Manifest 变更。

保留已验收的窗口准星、菜单、背包与仓库、搜尸、视野、手电电池、快速近战、声响诱饵、地图权限及固定桌面入口。原五项 **3ms** 性能检查的负载、计时和门槛保持。

## 批准后的实施顺序

1. 建立本页只读展示与选择状态，完成两个标签、全部记录和完整清单；不新增通用任务框架。
2. 接入原开始／取消／领取命令和自行寻回目标，替换旧按下即执行输入，清理旧显示分支，避免双路提交。
3. 补齐中文短文案、长原因显示、三档窗口与中英文检查，再完成专项及完整注册回归。
4. 用文件清单、内容指纹和程序标识冻结候选，继续使用桌面 **Project Raidline Debug**，提供一次简短人工验收清单。

按钮按下时绑定动作、记录／任务 ID、费用及当前状态，抬起时重新核查；状态变更不能把“开始委托”误触为“领取”。移出、失焦、离窗、关闭、翻页、语言／窗口变化和警报等模态切换取消旧操作。成功事务后重新取投影，不继续使用被替换 Profile 的旧引用。

## 怎么验收

自动化覆盖空态、全部记录和物品可访问、余额不足、已有委托、失效选择、保存失败、取消恢复、未完成结果隐藏、满仓领取拒绝、重复操作、资产树与唯一所有权，以及目标设置／清除和出击页往返。

检查 1280×720、1600×900、1920×1080 的中英文、长名称、关键数字和完整原因，检查悬停／按压与滚动。按项目 Windows Debug 预设重建受影响目标，运行专项、完整 CTest 及原五项 3ms 检查。正式推送后才进行新提交的准确双平台 CI；本轮没有运行构建或测试。

普通试玩预期：

| 前置 | 操作 | 看见什么 |
| --- | --- | --- |
| 原存档有失物记录 | 从对局出击进入失物记录并选中 | 来源、保留机会和完整清单清楚可读。 |
| 有合法记录和足够货币 | 查看方案后明确委托 | 费用与游戏内耗时分开显示，只扣一次，任务出现在 NPC 委托。 |
| 委托正在进行 | 通过原休息或行动推进世界时间后返回 | 状态更新；完成前不泄露结果，完成后才可领取。 |
| 已有待领取任务 | 领取；容量不足时整理仓库后重试 | 失败原因清楚，原任务完整保留；成功后物品进入仓库。 |
| 有可自行寻回记录 | 设置目标并返回 | 正确来源地图和目标可见，仍需自行确认出击。 |
| 有可取消委托 | 确认取消 | 不退款提示明确，恢复原记录；返回、切页与菜单行为正常。 |

不要求玩家为了验收故意死亡或放弃装备。特殊边界使用隔离测试；本轮不启动游戏，不访问真实存档。

<details>
<summary>代码范围、基线、协调与接管证据</summary>

### 状态与授权

- task_id：ui-lost-recovery；plan_version：RL-UI-LOST-RECOVERY-v1；state：waiting_plan_approval。
- approval_source：无实施批准。本轮人类原文为“请协调策划线程安排下一步具体开发方案。”，仅授权协调和准备方案。
- candidate_id、accepted_candidate_id、acceptance_source：无；pending_request：无，策划请求已闭环。
- 实际开发目录：`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1`。治理根目录仍为 `E:\WorkPlace\Projects\C\Project Raidline`；其旧源码不是本片开发基线。
- 当前分支 `codex/ui-base-deployment-v1`，HEAD `47f8343b909c9608d79a5784c3b6b950dd3716f6`。已 fetch 并核对 `origin/main 58a2ac27c8d366ba3796d92d77c819b370518408`，两者树均 `f602b8b369db255620713d75dafeef4998fd93e9`。
- 实施获批后拟从上述已验收 main 建立单任务分支 `codex/ui-lost-recovery-v1`；本轮未切分支，未创建新开发工作区或测试入口。若届时 main 有变化，先核查依赖，不复制未验收功能。
- PR188 已普通合并；准确 main CI39622217703、attempt1、head58a2ac2，结论 success。已交付的双平台各2467/2467及五项3ms只是基线证据，不是本方案验证结果。
- 前任务仍为已交付；其原运行候选 `RL-UI-BASE-DEPLOYMENT-v1-local-992a71d67b10` 保持。交付时只有测试字体定位适配，当前源码验证指纹 `30fe2b31fdedf5cac660471cf81e6599bfadad78b1e9b7c6002993f84c3e16ce`。

### 已核查及拟处理文件

以下路径相对实际开发目录；“拟新增”文件尚未创建。

| 文件 | 操作 | 原因 |
| --- | --- | --- |
| `src/app.cpp`、`src/app.h` | 修改 | 替换 renderLostRaidRecords 及原 RaidGate 失物点击分支；接入本页事件、退出和清理，移除该页旧重复路径。 |
| `src/app_ui_lost_recovery.cpp` | 拟新增 | 本页原生按下／抬起事件、渲染、选择刷新与原服务调用。 |
| `src/ui_lost_recovery_presentation.h/.cpp` | 拟新增 | 只读记录、委托状态、短文案、布局和命中投影，作为本页直接消费者。 |
| `src/app_ui_base_deployment.cpp` | 必要时最小修改 | 仅本页入口／返回时重置按压、选择与展示缓存；保留其他出击操作和确认语义。 |
| `src/ui_localization.cpp`、`tests/test_ui_localization.cpp` | 修改 | 新短标签与领域失败原因中文映射，保留原词条语义。 |
| `tests/test_ui_lost_recovery.cpp` | 拟新增 | 本页真实事件、状态、显示和事务衔接回归。 |
| `tests/test_ui_base_deployment.cpp` | 必要时补充 | 验证出击往返、地图／目标／情报／确认状态，不放松原断言。 |
| `CMakeLists.txt` | 修改 | 在 SDL 客户端注册本页源码与专项测试。 |
| 当前状态、本计划、`doc/acceptance/ui-lost-recovery-v1.md` | 更新／后续拟新增 | 实施后记录实际候选、检查及简短试玩步骤。 |

复用且保持业务实现：`lost_raid_domain.*`、`recovery_task_domain.*`、`game_session.cpp`、`raid_lifecycle.cpp`、`profile_state.cpp`、`content_registry.*`；不新增存档或内容版本。

### 关键代码事实

- `app.cpp:15227` 旧渲染只显示前四条记录、每条前四项物品摘要；`:2572` 的270像素主按钮承载报价长文。`:3334` 旧输入在按下时执行服务。
- `lost_raid_domain.h:18`、`lost_raid_domain.cpp:57` 提供完整记录／资产与原排序。`recovery_task_domain.h:15` 提供报价和任务投影，倾向数量不是保证概率。
- `content_registry.h:357` 的 RaidRecoveryDefinition 是费用／时长来源；`recovery_task_domain.cpp:234` 使用同一地图服务定义，失败会早退且报价数字默认0。因此只读展示原服务标价，执行仍服从当前 queryStartRecoveryTask。
- `recovery_task_domain.cpp:304/369/421` 分别开始、取消和领取。取消恢复原记录且不退款；领取容量不足整笔拒绝。`profile_state.cpp:1151` 按独立仓库页找合法位置，不跨页摆物品。
- `game_session.cpp:3473/3495/3516` 保留候选保存与失败零修改；`game_flow.cpp:180` 普通设施打开时暂停世界推进，防守活动例外仍沿原规则。
- `app.cpp:3407` 设置自行寻回地图、清情报和风险确认；`app_ui_base_deployment.cpp:218/270` 处理换地图／清目标。共享 tryDeployFromBase 与实际地图、室内图权限不改。
- 本页投影以 Profile revision、世界分钟、语言、选择／分页及目标为刷新依据，不在动画帧复制完整 Profile、重复遍历全资产或创建展示用资产。无独立领取容量计划时不预判保证可领取。

### 原策划线程有效回复

- target：策划线程 `019feff4-477c-75c2-bf31-3f09ff1e3891`，host local。
- request_id：MAIN-UI-NEXT-20261022-AFTER-DEPLOYMENT-01。
- request task_id：ui-optimization-next；version：RL-UI-NEXT-20261022-v1。
- baseline：main 58a2ac27c8d366ba3796d92d77c819b370518408; tree f602b8b369db255620713d75dafeef4998fd93e9; accepted runtime RL-UI-BASE-DEPLOYMENT-v1-local-992a71d67b10。
- 实际完成 turn：`01a1629e-9f5c-7653-9a11-be1e4aef920b`；最终 message：`msg_09394c523a164a07016ad9c4f63adc8191b3797909eaa5b775`。
- role：planner；status：proposed_readonly；chosen_task_id：ui-lost-recovery；plan_version：RL-UI-LOST-RECOVERY-v1。
- 原生 wait_threads 返回最终消息被截断，已用 read_thread 精确读取完整最终原文并逐项核对四元组；没有把发送回执或进度消息当正式选题结果。
- 策划推荐本片是因为出击流程的旧子页仍承载扣款、期限和所有权信息，整理收益明确，无新增玩法或资源依赖。方案建议不是人类批准。
- 临时只读核查工协助检查合同，不替代已有策划线程。未联系美术，因为本片没有新资产需求。

### 本轮检查、保护与回滚

证据目录：`C:\Users\Administrator\Documents\美术资源生成\tmp\ui-next-after-deployment-20261022`。保存原生请求、完整有效回复、原工作区字节检查点、基线PR／CI核对及最终保护核查。

本轮只改两处目录中的 CURRENT_STATE 和本计划。保留原五工作区所有其他未提交修改；实际开发目录原20份脏文档中的另外19份原字节保持，其他根原修改保持。461项源码／测试／CMake、149个程序／测试二进制、74项运行资源、7项DLL及固定快捷方式均与检查点相同。

验收程序 SHA256：`595f7d057e4cf338563f726f72973ab1f22a2c25b87a65301812fe3596a09f5f`；快捷方式 SHA256：`6c2d05a98e66a1a724dd119dbc676cf130f075b0a688722324fda93b6ec61c28`。

尚未进行本片实施、构建、测试、游戏运行、真实存档访问或新远端CI；没有提交、推送、合并、资产生产、信任／权限／模型改动。

实施后的回滚仅撤回本片客户端、本地化、测试与相应记录，恢复程序检查点，沿用固定入口。保护其他修改和真实存档，不用整树 reset 或旧存档覆盖已经合法完成的事务。本轮方案如被修订，只更新方案，不实施代码。

</details>


</details>

</details>
