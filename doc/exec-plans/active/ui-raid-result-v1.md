# 对局结果界面统一 v1

**RL-UI-RAID-RESULT-v1｜候选 fc00d531a705 已验收，正在交付。**

用户已验收候选 **fc00d531a705**，并授权提交、推送和合并。当前正在交付；本轮不启动下一项开发。

本轮不安排或启动下一任务。

- 统一结果页、分页带回记录、现有等比图片、可滚动详情和固定返营按钮已验收。
- 原结算、物品原位、救援检查点、失物记录、返营保存及V0行为保持。
- 继续使用原桌面 **Project Raidline Debug**；本轮只交付，不启动新开发。

<details>
<summary>授权、版本、验证与保护证据</summary>

- task_id：ui-raid-result；plan_version：RL-UI-RAID-RESULT-v1；state：accepted_pending_delivery；pending_request：无。
- candidate_id 与 accepted_candidate_id：RL-UI-RAID-RESULT-v1-local-fc00d531a705；acceptance_source：2026-10-23主控直接人类原文“对局结果界面 v1 验收通过，请完成提交、推送和合并。”。
- 基线main 07f6561fc1c6d2707ddbc3ae81017b5974193a5b；分支codex/ui-raid-result-v1。
- 只交付9项客户端／本地化／构建／测试与3项本任务记录。23份其他脏文档、其他四工作区分支／HEAD／索引与无关修改保持。
- 冻结473项源码／测试／CMake中472项原字节保持，测试末尾格式变化见下。152项二进制及程序保持原字节。验收源码指纹fc00d531a705b91174a7886034f878eab0a539a2a9e86b0bb3ff36e136666c16；程序SHA256 f8b6c203a664a32bf4de0869ac1f96d61469d4acf02261255f8f5a858b212e0c；固定入口、74项资源、7项DLL保持。
- 提交格式检查仅移除tests/test_ui_raid_result.cpp末尾一个多余空白行，无token／功能变化；原验收源码完整备份保留，交付源码指纹3e5e0fb04d47b2b9f6500959ef07556eab2c6e7deb0405139716e28632adbdab。本地检查对应验收源码，远端CI对应实际提交。
- Windows本地完整2594/2594、专项223/223；原2548项全保留，新增46项；原五项3ms与78张中英文三尺寸离屏QA通过。本地／PR／main分别记录，人工验收来源如上。
- 提交前独立只读审查确认候选一致、464项既有源码（含业务、存档、原测试与性能检查）原字节保留，5项已有与4项新源码为批准集合，无阻断项。
- 没有启动游戏、读取真实存档、修改信任／权限／模型、生产资产或开始下一任务。正常钩子若改变其他脏文档换行，仅恢复原字节；不整树reset、不重写历史。
- 证据目录：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-raid-result-v1-delivery-20261023；before.json、delivery.json、audit.json、提交／推送日志、双平台CI日志及策划原文保留。

</details>

<details>
<summary>实施、验收候选与已批准方案（历史）</summary>

以下待验收／未提交为历史阶段，当前以上文实际状态为准。

# 对局结果界面统一 v1

**RL-UI-RAID-RESULT-v1｜候选 fc00d531a705 已完成，待用户验收。**

结果标题、8条分页带回记录、可滚动详情和固定返营按钮已统一。保留原结算、损失、救援检查点和返营保存规则。

继续使用原桌面 **Project Raidline Debug**，按[简短试玩清单](../../acceptance/ui-raid-result-v1.md)正常试玩。本候选已冻结；未提交、推送或合并。

| 内容 | 实际结果 |
| --- | --- |
| 带回记录 | 完整分页、现有等比图片与短名，保留重复记录；不虚构堆数、耐久、估价或新增战利品。 |
| 结果摘要 | 成功、死亡、主动放弃分开；救援、资金、实际威胁减值、返程时间和愿望只读原结果。 |
| 详情与反馈 | 说明按需展开、长内容滚动，首局提示复用原语义；保存失败显示完整原因并可重试。 |
| 明确返营 | 同一按钮按下／松开及新鲜Enter生效，失焦、缩放、语言和结果变化取消旧输入，每帧最多一个意图。 |
| 原有规则 | 返营不再次结算；物品原位、失物记录、救援检查点、异常恢复和原V0／仓库用途保持。 |

Windows Debug全目标构建，专项223/223、完整2594/2594、原五项3ms通过。78张中英文三尺寸离屏图已实看，自动检查不能代替用户游玩验收。

<details>
<summary>授权、版本、实际文件与验证证据</summary>

- task_id：ui-raid-result；plan_version：RL-UI-RAID-RESULT-v1；state：waiting_human_acceptance；pending_request：无。
- approval_source：2026-10-23主控直接人类原文“同意该方案，请进行开发”，对应已提出的RL-UI-RAID-RESULT-v1。本轮连续实施批准范围；candidate_id：RL-UI-RAID-RESULT-v1-local-fc00d531a705；accepted_candidate_id、acceptance_source：无。
- 实际开发目录E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1；分支codex/ui-raid-result-v1，baseline／HEAD 07f6561fc1c6d2707ddbc3ae81017b5974193a5b。已验收依赖PR190／main双平台2548/2548与五项3ms通过；它们不替代本片自动化或人工验收。
- 实际生产／测试集合9项：CMakeLists.txt、src/app.cpp、src/app.h、src/first_raid_ui.cpp、src/ui_localization.cpp、src/app_ui_raid_result.cpp、src/ui_raid_result_presentation.h/.cpp、tests/test_ui_raid_result.cpp。另有当前状态、本任务记录与验收清单，共12项；23份其他原脏文档不纳入本片。
- 源码指纹fc00d531a705b91174a7886034f878eab0a539a2a9e86b0bb3ff36e136666c16，473项源码／测试／CMake。程序SHA256 f8b6c203a664a32bf4de0869ac1f96d61469d4acf02261255f8f5a858b212e0c，152项程序／测试二进制冻结；原程序检查点accepted-program.bin与新candidate-program.bin均保留。74项运行资源、7项DLL和固定快捷方式保持原字节。
- 结果投影仅消费LastRaidResult和不可变内容定义；结果键绑定档案、结算ID、完整结果、语言与提示偏好。无每帧资产扫描，不读当前背包重构过去数量；测试场、缺结果与旧异常记录使用中性显示。
- 原GameFlow／GameSession／RaidLifecycle、存档与全部内容原字节保持；17个既有镜头／准星／地图／菜单／返营入口函数正文逐项相同，旧仓库分支正文保留。新增本页专属接缝，不重写其他已验收UI。
- 正常VS x64 Developer Shell、UTF-8、VCPKG_ROOT，windows-debug配置与全目标增量构建。App类布局变化后全部消费者重建，后续修正再次构建并重链；所有最终检查绑定tested-source.json，未用旧二进制。
- 专项focused-final：223/223、0失败0跳过；完整full-final：2594/2594、0失败0跳过。原2548个注册名全保留，新增46项。首次focused-1的220/222中，两项像素回归发现状态文本框26低于20字号的28行高，生产布局改为30后通过；未降低或移除断言，失败日志保留。
- 独立只读审查发现并修正了本页重复Enter及暂停旧按压边界；不修改全局InputSystem。入结果时仅清旧战斗UI瞬态，避免轮盘继续鼠标捕获；准星与捕获算法正文不变。
- 原五项3ms负载和计时不动：RaidLightConeVisibilityTest.PublishedFiveSpacesAndDenseLocalGeometryStayWithinObservationBudget；WeaponLightTest.LampAndNearBackgroundPreparationMeetUnchanged3msBudgetFor32And100Targets；WeaponLightTest.PoweredPublishedSpacesKeepTheOriginalObservationBudget；WeaponLightTest.PoweredFourConsumerQueriesReuseCurrentSnapshotWithin3ms；NoiseDecoyTest.RealPulseAndReturnNavigationKeepThreeMillisecondBudgetFor32And100。在完整CTest全部通过。
- 离屏QA为13状态×三尺寸×中英文共78张，全部联系表已实看并放大关键原图，包含批准图片和实际文字／数字。没有运行游戏EXE，不把离屏结果当作实际窗口或手感验收。
- 证据目录：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-raid-result-v1-20261023；before.json、原修改备份、build结果、focused-1/final、full-final、tested-source.json、previews与visual-review.json、audit.json、final-checks.json、freeze.json保留。
- 没有提交、推送、PR、合并或新远端CI，没有访问真实存档、改信任／权限／模型、外部GDD或生产资产。只在临时隔离档案验证失败和特殊情况。
- 回滚仅撤本片客户端、本地化、新测试／构建登记及3份任务记录，恢复原程序检查点并保留固定入口；不整树reset、不覆盖真实存档、不撤销合法结算或其他工作。

</details>

<details>
<summary>已批准方案与选题历史</summary>

以下待批准／尚未实施是批准前历史，当前以上方状态为准。

# 对局结果界面统一 v1

**RL-UI-RAID-RESULT-v1｜待用户批准，尚未实施。**

让玩家看清本局结果、完整带回记录和已发生的变化，再返回基地整备。策划已指定本任务，主控已核查当前代码；本轮仅准备方案。

## 玩家会看到什么

| 部分 | 本次方案 |
| --- | --- |
| 结果标题 | 清楚区分撤离成功、玩家死亡与主动放弃，保留“对局结果”页面名称。 |
| 带回记录 | 成功时显示全部条目，使用现有物品图片、型号短名和分页。初稿每页8条；图片等比显示，不拉伸。 |
| 失败说明 | 说明随身物品已丢失及是否生成行动失物记录；不编造失败物品清单、估价或补偿。 |
| 结果摘要 | 按已有数据展示资金变化、已救援居民和实际降低的基地威胁。没有内容的项目省略或弱化。 |
| 展开详情 | 查看返程／归队耗时、普通与受伤居民、愿望带回摘要和完整说明，长内容可滚动。 |
| 固定底栏 | 只有一个“返回基地”主操作。显示悬停、按下和轻微渐变反馈；返营保存失败时留在本页并允许重试。 |

沿用已验收的深色、灰绿风格和短中文文本。优先展示结果与操作，详细说明按需展开。整理结算页自身提示的层级，避免原战斗HUD、结果框、返回按钮和首局说明同时抢占阅读区域；战斗中的原HUD不改。

## 信息含义必须准确

- 带回记录包含出发时自带的装备、容器内物品和安装零件，不等于本局新捡到的战利品。显示清楚的简短说明。
- 当前记录只保存物品定义条目，没有当时的堆叠数量、耐久、估值或新旧标记。初稿逐条展示并保留重复条目；总数明确写为记录条数，不冒充弹药发数或物品堆内数量。不扫描当前资产反推历史。
- 带回页是结果记录，不能拖动、装备、出售、使用或“领取全部”。成功撤离后的物品仍在原装备栏和格位，不自动搬进仓库。
- 资金变化只读原结果字段；当前正常结算填0，不计算估价或虚构净收益。返程／归队耗时不能写成整局游玩时间。
- 死亡／主动放弃沿用原损失规则；生成的行动失物记录仍按原保留与寻回规则处理，不能说成物品永久销毁。没有记录时如实说明。
- 失败时已确保救援仍可能保留，按结果字段显示普通与受伤居民。愿望贡献充足只提示回基地手动提交，不代为提交；过期与不足沿用原含义。

不增加击杀、评分、经验、排行、战斗时长、地图回放、历史耐久、奖励或补偿；没有结果数据就不猜测。

## 返回与输入

鼠标在同一按钮按下并松开才确认，Enter仍可返营；结果出现前已按住的鼠标或按键不能直接跳过结果。连续事件每次只处理一个明确意图。

翻页、收起详情、移出按钮、失焦、切换窗口／语言或结果变化时，取消旧按压。按压关联当前档案、结算ID、语言和页面状态，旧点击不能作用到另一份结果。

Esc保持原菜单含义，不变成立即返营或丢弃结果。首局提示的显示／隐藏与确认保存规则保持，仅调整本页内的摆放和重复信息；基地及战斗提示不一起重写。

继续调用原返营流程。结算在结果页出现前已经保存，“返回基地”不再结算、不再发放；首局提示确认保存失败时仍停在结果页，可重试。成功回出击口，失败回医疗点。

重启仍按原规则回基地，不强制重放尚未点击返营的旧结果。异常关闭沿用现有恢复检查点和回滚，不生成第四种正常失败结算；已确保救援的干净检查点保持。

## 实施与验证

获批后先从已验收main建立独立 `codex/...` 分支，并保护原修改和程序。按三个步骤完成：只读结果投影与分页 → 结果页绘制和原生输入 → 回归、离屏图与用户候选。

| 验证 | 应证明的行为 |
| --- | --- |
| 投影与布局 | 成功、死亡、主动放弃、无结果、空清单、超过8条和长名称均可读；重复定义不冒充堆内数量。 |
| 原生输入 | 翻页、展开、鼠标按下／松开、Enter、重复事件、失焦和窗口切换不会穿透或重复返营。 |
| 数据与保存 | 浏览不改档案；返营不再次结算；保存失败可重试；救援、愿望、清剿摘要与已存结果一致。 |
| 已有合同 | 原物品位置、失物记录、旧档、返程时间、救援检查点和异常回滚回归保持，旧测试名称与有效断言不削弱。 |
| 视觉与性能 | 中英文、1280×720／1600×900／1920×1080，关键文字数字实际绘制；完整注册CTest与原五项3ms负载及门槛保持。 |

需要修改App头部布局时，全量重建受影响目标，避免旧二进制。新功能完成后提供可识别候选和简短试玩清单，等待人工验收；本方案不授权验收前提交、推送或合并。未来交付仍需准确PR及合并后main的双平台CI。

正常试玩继续使用原桌面 **Project Raidline Debug**：

| 操作 | 预期 |
| --- | --- |
| 正常撤离后查看“对局结果”。 | 成功状态明确，全部带回条目可翻页查看；不会将自带装备说成新增战利品。 |
| 展开详情，再收起。 | 原结果数字清楚，浏览不移动、领取或提交任何物品。 |
| 点击“返回基地”或按一次Enter。 | 只返营一次，物品仍在原位置，之后可正常整备。 |
| 切换窗口尺寸，并试一次失焦后返回。 | 文字、图片和按钮正常，旧按压不继承。 |

无需为验收故意死亡、主动丢装备或积攒稀有事件。失败、保存失败、长清单和特殊数据由隔离档案自动验证；已自然遇到的失败场景可补看对应说明。

<details>
<summary>已核查代码、技术边界与回滚</summary>

以下路径均在实际开发目录 `E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1`。

| 文件 | 拟调整内容 |
| --- | --- |
| `src/ui_raid_result_presentation.h/.cpp`（拟新增） | 只读消费LastRaidResult和已有定义；结果摘要、完整条目、分页、文本测量、布局及命中区域。 |
| `src/app_ui_raid_result.cpp`（拟新增） | 结果页专属绘制、事件、按压与缓存；调用原handleScreenConfirm／returnToBase。 |
| `src/app.cpp`、`src/app.h` | 只接入RaidResult的队列、更新、绘制和清理；移走旧Alpha结果分支，保留仓库／旧V0用途及其他共享按钮调用方。 |
| `src/first_raid_ui.cpp` | 仅按需要调整结果页首局提示的摆放／复用，保留原显示、隐藏和保存规则。 |
| `src/ui_localization.cpp` | 追加本页短文本，复用已验收物品短名；不全局改旧词条含义。 |
| `tests/test_ui_raid_result.cpp`（拟新增） | 投影、布局、真实SDL事件、隔离保存及离屏文字／数字回归。 |
| `tests/test_first_raid_guidance.cpp`、`tests/test_ui_localization.cpp` | 仅在需要时补本页接入回归，不削弱已有断言。 |
| `CMakeLists.txt` | 只登记上述客户端文件和专项测试。 |

只读复用、不计划修改：`src/profile_state.h::LastRaidResult`、`src/game_flow.cpp::returnToBase/continueGame`、`src/game_session.cpp::settleAlphaRaid`、`src/raid_lifecycle.cpp::settlePendingRaid`、`src/raid_settlement.h/.cpp`、保存DTO、内容定义、资源和Manifest。

现状证据：`app.cpp::renderStashOverlay`的正式结果分支沿用460×380旧面板，携回列表只绘前9条，有wishReturn时只绘前5条；`render()`额外绘共享返回按钮。`pollEvents`在结果页的鼠标按下即排确认，`update`经handleScreenConfirm返回；新输入仅限定本页。`renderScreenPrimaryButton`按原英文长度估算宽度，本页将复用实际文本测量，其他调用方不一起重写。

LastRaidResult不保存地图快照、击杀、用时、数量或估值；正式Alpha不能用旧V0 RaidSettlementSummary的stackCount／unitCount来填补。developer range没有正常结果数据时只能显示当前测试结束／暂无结果的中性状态，不能借上次结算冒充本次结果。缺失或旧异常枚举仅作防御性兼容显示，不触发任何新结算。

原测试依据：`test_first_raid_lifecycle.cpp`的各结果返营、FailedReturnAcknowledgmentStaysOnResultAndCanRetry、重启和二次出击；`test_raid_lifecycle.cpp`的成功保持精确位置、死亡失物记录、返程只应用一次和异常回滚；`test_alpha_extraction_session.cpp::SecuredRescueIsCleanAbnormalExitCheckpoint`；`test_base_wish_expedition.cpp`及`test_first_raid_guidance.cpp`。

投影按profileId、settlementId、结果内容和语言失效；绘制和命中消费同一布局，页面状态只在内存。仅在结果变化时整理条目，不在每帧扫描完整资产；原观察、邻居计算与五项3ms计时测试不改。

不修改准星、镜头、菜单、地图权限、库存或战斗行为，不改世界时间／救援／愿望／损失／恢复规则。无需新美术、音效、UI包或外部依赖；不接触外部GDD、真实存档、信任／权限／模型。

回滚只撤本页客户端、本地化和新增测试／构建登记，恢复原程序检查点及三份任务记录；保留固定入口，不整树reset，不覆盖真实存档，不撤销已合法发生的结算或其他修改。

</details>

<details>
<summary>任务身份、基线与策划协调证据</summary>

- task_id：ui-raid-result；plan_version：RL-UI-RAID-RESULT-v1；state：waiting_plan_approval；pending_request：无。
- approval_source：无实施批准。2026-10-23主控直接人类原文“请协调策划线程安排下一步UI优化任务，并给出具体开发方案。”，仅授权协调、只读核查和计划记录。
- candidate_id、accepted_candidate_id、acceptance_source：无本任务候选或验收。上一已验收运行候选RL-UI-REGIONAL-OPERATIONS-v1-local-112b77113765保持；程序SHA256 5cbace9b30da77141a0ee5938ee2bc1a67a75e0bd8f167895e4b9a946c87c92a，源码指纹112b771137650d1b9cf2ffd853f6298a363164b3a06674afbf703aac6522d188。
- 实际开发目录E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1；当前分支codex/ui-regional-operations-v1、HEAD 5744acd8eaf97eb150fe34ad069d05d015de0451。已fetch的main 07f6561fc1c6d2707ddbc3ae81017b5974193a5b，树5b9b18f265cfe986d7da09fdce1c582b113c194f，与当前提交同树；本轮未切分支。
- 依赖PR190已普通合并；PR CI 39883998644、main CI 39885983478均attempt1、Windows／Ubuntu各2548/2548、原五项3ms通过。本轮已重新读取准确远端状态；它们是依赖证据，不是未实现新方案的验证。
- 本轮没有构建、测试、启动游戏、访问真实存档、生产资产、提交、推送或合并；469项源码、151项程序／测试二进制、74资源、7DLL、固定快捷方式和五工作区其他改动按原字节保护。
- 原生策划请求request_id MAIN-UI-NEXT-20261023-AFTER-REGIONAL-OPERATIONS-01；request task_id ui-optimization-next；version RL-UI-NEXT-20261023-AFTER-REGIONAL-OPERATIONS-v1；baseline main 07f6561fc1c6d2707ddbc3ae81017b5974193a5b; tree 5b9b18f265cfe986d7da09fdce1c582b113c194f; accepted runtime RL-UI-REGIONAL-OPERATIONS-v1-local-112b77113765。
- 实际目标019feff4-477c-75c2-bf31-3f09ff1e3891，host local；turn 01a16692-24fe-75c3-8d80-8f64b41ea5bf；message msg_09394c523a164a07016adac67ec1ac8191bc3ab31724b49b7b；role planner；status proposed_readonly。已读完整原文并校验四元组和目标；发送回执、idle或工具许可不作为批准。
- 策划指定本任务；主控根据当前代码进一步明确：重复定义逐条保留、带回数量边界、开发测试场缺失结果的中性展示、已确保救援检查点、首局提示的结果页接缝及原V0用途不改，均未扩展业务。
- 证据目录：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-next-after-regional-operations-20261023；before.json、planner-request.txt、planner-send.json、planner-result.json、state.json及audit.json。外部GDD只读，没有美术生产需求。


</details>

</details>

</details>
