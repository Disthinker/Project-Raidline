# 区域路线与基地行动界面统一 v1

**候选 112b77113765 已验收，正在交付。**

用户已验收候选 **112b77113765**，并授权提交、推送和合并。当前正在交付；本轮不启动下一项开发。

本轮不安排或启动下一任务。

- task_id：ui-regional-operations；plan_version：RL-UI-REGIONAL-OPERATIONS-v1；state：accepted_pending_delivery；pending_request：无。
- candidate_id 与 accepted_candidate_id：RL-UI-REGIONAL-OPERATIONS-v1-local-112b77113765；acceptance_source：2026-10-23 主控直接人类原文“区域路线与基地行动界面 v1 验收通过，请完成提交、推送和合并。”。
- 基线main 8a086f435345deff312ac4a00b955eaab2d28c9f；分支codex/ui-regional-operations-v1。
- 只交付10项客户端／本地化／构建登记／测试与3项本任务记录。21份其他脏文档、其他四工作区分支／HEAD／索引与无关修改保持。
- 冻结469项源码／测试／CMake、151项二进制及程序保持原字节。源码指纹112b771137650d1b9cf2ffd853f6298a363164b3a06674afbf703aac6522d188；程序SHA256 5cbace9b30da77141a0ee5938ee2bc1a67a75e0bd8f167895e4b9a946c87c92a；固定快捷方式、74项资源、7项DLL保持。
- Windows本地完整2548/2548、专项214/214、原2507项全保留、新增41项；五项原3ms与78张中英文三尺寸离屏QA通过。本地／PR／main各自记录，不把历史CI当新版本证据。
- 未启动游戏、读取真实存档、修改信任／权限／模型、生产资产或开始下一任务。正常提交钩子如更改其他脏文档的换行，仅恢复该文档原字节，不整树reset或重写历史。
- 证据目录：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-regional-operations-v1-delivery-20261023；before.json、delivery.json、audit.json、提交／推送与CI日志及策划原始回复保留。

<details>
<summary>实施、验收候选与已批准方案（历史记录）</summary>

以下“待验收／未提交”等属于历史阶段，当前以上文实际交付状态为准。

# 区域路线与基地行动界面统一 v1

**RL-UI-REGIONAL-OPERATIONS-v1｜候选 112b77113765 已完成，待用户验收。**

原区域页已整理为“路线与哨所、基地与迁移、外围清剿”三个标签。列表与详情分开，建立、驻军、迁移、修复及出击使用明确的独立按钮。继续使用原桌面 **Project Raidline Debug**，按[简短试玩清单](../../acceptance/ui-regional-operations-v1.md)验收。

| 内容 | 实际结果 |
| --- | --- |
| 路线与哨所 | 单程路线、驻军、在线／中断、安全次数分开；完整列表分页，长说明和原因滚动显示。 |
| 基地与迁移 | 迁移条件、耗时、抵达时刻、缺失设施及储备预览清楚；特色修复状态与当前生效位置分开。 |
| 外围清剿 | 分开显示行动增加威胁与成功清剿减值，只从原查询读取估算，不提前结算。 |
| 明确操作 | 三类清剿保留各自冻结目标；同帧重复点击不能消耗自身风险确认，Enter不穿透。失效目标不会自动换成另一个地点。 |
| 已有规则 | 逐命令门禁、成本、健康满员驻军、唯一核心、设施储备、队列、世界时间和保存失败零提交保持。 |

Windows Debug全目标构建、专项214/214、完整2548/2548及原五项3ms通过。78张中英文三尺寸离屏图已审查；这些结果不能代替实际游玩验收。

本轮没有改领域规则、存档格式或资源，没有启动游戏、访问真实存档、提交、推送或合并。候选已冻结，等待用户验收；实质修改须重新识别候选和复验。

<details>
<summary>候选、实际变更、验证和保护证据</summary>

- task_id：ui-regional-operations；plan_version：RL-UI-REGIONAL-OPERATIONS-v1；state：waiting_human_acceptance；pending_request：无。
- approval_source：2026-10-22主控直接人类原文“同意 RL-UI-REGIONAL-OPERATIONS-v1，请按方案实施，保留已验收的 UI 和玩法行为、原存档、固定测试入口及3ms性能门槛，完成后提供简洁的验收清单，暂不提交、推送或合并。”。candidate_id：RL-UI-REGIONAL-OPERATIONS-v1-local-112b77113765；accepted_candidate_id、acceptance_source：无。
- 实际目录：E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1；分支codex/ui-regional-operations-v1；baseline／HEAD 8a086f435345deff312ac4a00b955eaab2d28c9f。从已验收main建立独立分支，原未提交修改逐字节保护；没有本地提交或新远端CI。
- 源码指纹：112b771137650d1b9cf2ffd853f6298a363164b3a06674afbf703aac6522d188，469项源码／测试／CMake；程序SHA256：5cbace9b30da77141a0ee5938ee2bc1a67a75e0bd8f167895e4b9a946c87c92a；151项程序／测试二进制冻结。原程序检查点accepted-program.bin与新candidate-program.bin保留，固定快捷方式原字节、资源74项和DLL7项保持。
- 实际代码集合10项：CMakeLists.txt；src/app.cpp、app.h、app_ui_base_deployment.cpp、ui_localization.cpp、app_ui_regional_operations.cpp、ui_regional_operations_presentation.h/.cpp；tests/test_ui_base_deployment.cpp、test_ui_regional_operations.cpp。另有当前状态、本任务记录和试玩清单，共13项；其他原脏文档不纳入本片。
- 原区域Down-only执行、旧绘制和6个废弃几何已移除。窄openRegionalConstruction helper只复用原厨房饮水项目分页与Owned储备跳转，不购买、安装或重设队列；旧出击回归保留名称及断言，并改用原生区域Close，同时断言旧热点不能执行。
- 12个原准星／镜头／地图／选择函数逐项正文相同；tryDeployFromBase业务正文相同，仅增加本页状态清理。GameSession、区域、迁移、特色修复、围攻、生命周期、保存与内容文件原字节保持。所有成本、资格、时间和结果继续消费原查询／回执。
- 新增中文词条只追加到原词表末尾，保留原顺序；精确BASE THREAT标签避免完整数字行二次翻译与测量前缀不一致。修复前置受阻时也展示领域已提供的设施储备清单。
- 构建：原VS x64 Developer Shell、UTF-8、VCPKG_ROOT；cmake --preset windows-debug；cmake --build --preset windows-debug --parallel 4。App头布局变更后全目标重建，所有消费者重链。终稿与测试前置修正后再次增量构建成功，未使用陈旧二进制结果。
- 最终专项run_tests.py focused 2：214/214、0失败0跳过。完整run_tests.py full final：2548/2548、0失败0跳过，耗时406.48秒。原2507项全部保留，新增41项，均绑定tested-source.json的同一源码及151二进制。
- 首轮专项208/214，六项失败均由新测试fixture未把起始仓库步枪装备造成；只修正隔离测试档的合法配装前置，保留所有断言与名称，并明确检查合法档案和可击发／不可击发状态；最终214/214。所有失败日志保留，没有跳过或放宽业务断言。
- 原五项3ms：RaidLightConeVisibilityTest.PublishedFiveSpacesAndDenseLocalGeometryStayWithinObservationBudget；WeaponLightTest.LampAndNearBackgroundPreparationMeetUnchanged3msBudgetFor32And100Targets；WeaponLightTest.PoweredPublishedSpacesKeepTheOriginalObservationBudget；WeaponLightTest.PoweredFourConsumerQueriesReuseCurrentSnapshotWithin3ms；NoiseDecoyTest.RealPulseAndReturnNavigationKeepThreeMillisecondBudgetFor32And100。原测试、计时、负载、门槛均未修改，在完整CTest通过。
- 离屏QA：qa-ui-regional-operations-v1-2026102201，13状态×三尺寸×中英文共78张；中英文联系表全部实看，另放大关键原图。长标题第二行、驻军、时间、材料、威胁数字有真实绘制回归；离屏图不代替真实窗口和手感验收。
- 原存档与固定入口保持，不读取玩家真实存档；特殊情况只用临时隔离档案。原程序、全部无关脏改动、其他四工作区HEAD／分支／索引，以及原资源受保护。没有资产生产、外部GDD、信任／权限／模型修改或Git交付。
- 证据目录：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-regional-operations-v1-20261022；before.json、build-1至5、focused-1/2、full-final、tested-source.json、previews-1.json、visual-review.json、final-checks.json、audit.json和freeze.json。
- 回滚只撤本片客户端、本地化、构建登记、测试及三份任务记录，并恢复已验收程序检查点；不整树reset、不覆盖真实存档或撤销合法事务。没有新增正式资源或存档迁移。

</details>

<details>
<summary>已批准方案与选题历史</summary>

以下“待批准／尚未实施”是批准前历史；当前以本页上方为准。

# 区域路线与基地行动界面统一 v1

**RL-UI-REGIONAL-OPERATIONS-v1｜待用户批准，尚未实施。**

把目前挤在一页的路线、哨所和基地行动分开，让玩家在操作前看清条件、成本和影响。沿用已验收的灰绿深色界面、短中文、悬停反馈和轻动效。

入口保持：原桌面 **Project Raidline Debug → 基地 → 对局出击 → 区域路线**。本轮只核查代码、协调策划并记录方案；程序与玩法没有改动。

## 页面安排

| 标签 | 玩家可以查看和操作什么 |
| --- | --- |
| 路线与哨所 | 路线耗时、哨所是否建立、驻军人数、捷径在线或中断，以及剩余安全行动次数。建立、派驻、撤回、恢复清剿分别使用明确按钮。 |
| 基地与迁移 | 当前主基地、原有候选地点、清剿与迁移条件、特色设施状态。保留厨房饮水建设和工坊储备的原跳转。 |
| 外围清剿 | 当前基地威胁、出击条件、正常行动新增威胁与成功清剿减值，以及明确的出击操作。 |

每页采用列表与详情分区，操作按钮位于独立区域。选中只查看；不自动派人、修复、迁移或出击。列表过长时分页，长原因和设施清单可滚动；短标签保持简洁，悬停能查看完整名称与说明。

页面复用现有字体、文本测量、配色、悬停与选中反馈。1280×720、1600×900、1920×1080及中英文都要可读；不靠继续堆长句或缩小关键数字来容纳内容。

## 必须说清的操作影响

- **建立不等于在线。** 哨所建立、满员健康驻军、捷径在线、中断是不同状态。当前主基地对应地点不能被当作普通哨所派撤驻军。捷径安全次数与主基地威胁分开显示。
- **成本来自原查询。** 建立和驻军不新增材料、货币或时间收费；驻军仍按原规则满员分配，尊重健康、职业顺序及建设预留。查询失败不能把默认0显示成免费、即时到达或可执行。
- **迁移、修复直接推进游戏时间。** 执行区提前显示耗时与到达时刻；迁移还列出缺失核心设施及进入储备的设施。特色修复显示建材、劳动力条件和效果；不改成后台等待任务。
- **已修复不等于当前生效。** 特色效果只在满足原地点与设施条件时生效。“制造耗时75%”不能写成“效率提高75%”，也不追溯改写已有订单。
- **清剿仍须完成行动。** 候选点清剿、哨所恢复、基地外围清剿各自绑定原任务目标和部署参数。点击出击不直接解锁地点、恢复捷径或降低威胁；结果由原行动结算决定。
- **每一步都由玩家明确操作。** 不自动串联清剿、建立、建设与迁移。工坊储备仍只跳转建设“已拥有”页，不在区域页隐式安装或重设队列。

## 交互与保留边界

动作保持固定语义，不再由同一按钮临时变成另一项命令。按下与抬起必须属于同一动作、同一稳定目标和仍然有效的状态；切换目标、翻页、切标签、关闭、失焦、离窗或缩放会取消未完成的按压。

不安全配装、失物即将到期继续沿用原出击二次确认，不增加额外确认层，也不绕过。旧风险确认只能用于原目标和动作；切换后失效。Enter不得穿透区域页触发普通出击。

逐个消费原查询的可执行状态及原因，不新增“整页统一禁用”规则。迁移、特色修复、外围清剿原有预警／防守门禁保持；建立和派驻不能因为UI改造而被增加领域中不存在的限制。上层模态窗口仍优先接收输入，取消旧按压不代表修改业务资格。

仅调整客户端展示和输入适配。保留窗口准星、菜单、配装、独立仓库页、搜尸、视野、手电电池、快速近战、声响诱饵、地图权限、出击和失物寻回行为。保持唯一科技核心、资产与驻军所有权、保存失败零提交，以及原存档格式和固定入口。

不新增手工选路、多基地经营、哨所仓储或服务、逐人驻军选择、新地图、新任务、新资源或新美术音效。本片不改标题菜单、对局结果、防守或建造页面的布局。

## 实施顺序与验证

1. 建立区域页的只读展示数据和稳定目标选择。复用现有查询，拆出三个标签、布局和独立动作；禁止展示查询修改长期档案。
2. 接入专属按下／抬起事件和状态刷新，删除对应旧按下即执行入口及废弃几何。保留原GameSession命令、任务参数、风险确认和建设跳转；迁移等成功后重新读取当前主基地与特色状态。
3. 补客户端专项回归，检查完整流程和相邻页面。完成Windows Debug全目标构建、专项与完整注册CTest；原2507项不得删减，新增测试另计。原五项3ms的计时、负载和门槛不变。
4. 检查中英文、三档窗口、长列表及长原因的离屏渲染，再提供一个冻结候选和简短试玩清单。离屏图和自动化不能代替用户游玩验收。

专项覆盖浏览零副作用；目标／动作在按压中变化；重复输入、旧版本和过期确认；建立与健康满员驻军；中断哨所恢复；迁移缺设施、设施储备与队列保留；特色修复资格及生效地点；保存失败零修改；三类区域出击参数；与出击／失物／菜单往返及Enter不穿透。特殊状态使用隔离测试档案，不读取或改造玩家真实存档。

本轮没有运行构建、测试或游戏。主线8a086f4已有双平台2507/2507和五项3ms通过，这是前任务基线证据，不是本方案的实现或验收结果。

## 完成后的简短试玩办法

继续使用原桌面 **Project Raidline Debug** 和原存档。没有高级基地状态时，只检查当前可见状态和禁用原因；无需为验收故意死亡、刷资源或迁移基地。

| 操作 | 应看到的结果 |
| --- | --- |
| 从“对局出击 → 区域路线”进入，切换三个标签并返回。 | 分区、选中反馈和返回关系清楚；浏览不派人、不消耗、不出击。 |
| 查看一处哨所和路线详情。 | 能分清已建立、驻军与在线状态；路线时间、安全次数和不可执行原因清楚。 |
| 查看“基地与迁移”的迁移、特色修复信息，不执行。 | 条件、耗时、到达时间和设施储备影响可读；已修复与当前生效不混淆。 |
| 有条件且愿意时，执行一项原本合法的区域操作。 | 只执行对应动作一次，结果或失败原因正确；区域出击保留原风险确认。缺少状态由隔离自动化补足。 |
| 切换窗口尺寸和语言，再打开、关闭菜单。 | 关键文字和数字不截断，长内容可访问；鼠标、准星与相邻页面保持原行为。 |

## 批准与交付方式

当前等待 **RL-UI-REGIONAL-OPERATIONS-v1** 的明确实施批准。批准后才从核对过的已验收main建立独立任务分支并开发；完成后提供候选供用户验收，暂不提交、推送或合并。后续Git交付仍需当前候选的验收和明确交付授权。

回滚只撤回本页客户端、本地化、构建登记和测试修改，保留其他未提交工作。原已验收程序及固定入口保留可恢复检查点；不整树reset、不覆盖真实存档，也不通过恢复旧存档撤销合法操作。

<details>
<summary>技术核查、实际路径、基线与策划证据</summary>

### 身份与授权

- task_id：ui-regional-operations；plan_version：RL-UI-REGIONAL-OPERATIONS-v1；state：waiting_plan_approval；pending_request：无。
- approval_source：无实施批准。2026-10-22主控直接人类原文“请协调策划线程安排下一步UI优化任务，并给出具体开发方案。”，只授权协调与准备方案。
- candidate_id、accepted_candidate_id、acceptance_source：无新候选或新验收。前一候选RL-UI-LOST-RECOVERY-v1-local-1b0c6c638de5保持已验收、已交付。
- 当前真实开发目录：E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1；治理根：E:\WorkPlace\Projects\C\Project Raidline。实际代码以前者为准，治理根旧指针分支不作为运行版本。
- 当前分支codex/ui-lost-recovery-v1，HEAD 68f6168753f7ca4a927c6a061a0f5def02662381；fetch后origin/main 8a086f435345deff312ac4a00b955eaab2d28c9f，两者树均d3b396a06d5502bca4a2fc1d17bb4cd097a769ae。
- PR189已普通合并；准确PR CI39765733418、main CI39768166682均attempt1 success，双平台各2507/2507及五项3ms通过。计划阶段只复核远端记录，没有新构建／测试／CI或Git交付。
- 拟实施分支：codex/ui-regional-operations-v1；本轮没有创建或切换分支。启动时再次检查main依赖与脏改动，保护已验收程序、原资源、固定入口和所有无关修改。

### 已核对的调用链与拟修改文件

当前真实路径是renderBaseRaidGate → renderRegionalOperations，入口来自app_ui_base_deployment.cpp的Regional操作。旧区域输入为SDL_MOUSE_BUTTON_DOWN → pendingBaseClicks_ → handleBasePointerClick；展示固定1080×580，动作依状态复用。当前中文入口为“区域路线”，子页标题为“区域基地候选点、路线与前哨”。

| 文件（相对于上述真实开发目录） | 拟操作与原因 |
| --- | --- |
| src/app.cpp、src/app.h | 接入区域专属状态、事件和生命周期清理；删除旧区域执行分支、旧绘制和无消费者几何。共享出击与准星规则不重写。 |
| src/app_ui_base_deployment.cpp | 打开、返回时重置本页状态与已失效风险确认，保留原出击页选择和导航关系。 |
| src/ui_regional_operations_presentation.h/.cpp（拟新增） | 只读投影、稳定ID选择、三标签布局、命中区和动作可用原因；不建立新业务规则或通用框架。 |
| src/app_ui_regional_operations.cpp（拟新增） | 渲染、原生事件、动作分发及结果刷新；调用原GameSession与tryDeployFromBase。 |
| src/ui_localization.cpp | 增加短中文／英文标签及完整说明，复用已有地点中文，不把内部ID当显示名。 |
| tests/test_ui_regional_operations.cpp（拟新增） | 专属投影、真实输入、隔离保存边界和离屏文字／数字回归。 |
| tests/test_ui_base_deployment.cpp、tests/test_ui_localization.cpp | 按需要更新区域往返和新增词条回归；保留已验收断言与原测试名。 |
| CMakeLists.txt | 仅登记上述客户端文件和专项测试。 |

只复用、不计划修改：src/regional_operations_domain.h/.cpp、base_migration_domain.h/.cpp、base_site_feature_domain.h/.cpp、base_siege_domain.h/.cpp、game_session.h/.cpp、raid_lifecycle.h/.cpp、存档DTO与内容目录。若发现必须改业务合同，另报范围，不在UI片内静默扩大。

queryRegionalRoute、queryEstablishRegionalOutpost、queryRegionalOutpostStaffing、queryRegionalBaseSiteClearance、queryBaseMigration、queryBaseSiteFeatureRepair、queryBasePerimeterSweep是资格、费用和时长的来源。原GameSession在候选保存成功后替换Profile，拒绝或保存失败不先改UI权威状态。

选择与按压使用RegionalBaseSiteDefinitionId／RegionalOutpostDefinitionId／MapDefinitionId等稳定身份，结合动作、档案身份、版本和相关上下文重验；不保存显示名、列表下标或跨提交容器引用。区域查询只显示原有元信息，不展示未授权地图内部布局、敌人或掉落。

迁移Plan提供来源／目标、migrationMinutes、arrival、missingRequiredFacilities与facilitiesEnteringReserve；特色Plan提供材料、劳动力、时长、到达时刻与manufacturingDurationPercent。原迁移唯一核心、旧基地转前哨、设施储备和队列暂停均由原领域原子处理，客户端不复写。

已有回归：test_regional_operations_domain.cpp、test_base_migration_domain.cpp、test_base_site_feature_domain.cpp、test_persistent_session.cpp、test_raid_lifecycle.cpp、test_save_repository.cpp、test_base_siege_ui.cpp及test_ui_base_deployment.cpp。现有UI测试只覆盖区域入口，不能代替新的区域页专项验证。

### 策划协调

- request_id：MAIN-UI-NEXT-20261022-AFTER-LOST-RECOVERY-01；request task_id：ui-optimization-next；version：RL-UI-NEXT-20261022-AFTER-LOST-RECOVERY-v1。
- baseline：main 8a086f435345deff312ac4a00b955eaab2d28c9f; tree d3b396a06d5502bca4a2fc1d17bb4cd097a769ae; accepted runtime RL-UI-LOST-RECOVERY-v1-local-1b0c6c638de5。
- 真实目标019feff4-477c-75c2-bf31-3f09ff1e3891，host local；turn 01a1636f-b4b2-73b0-bd8c-f7a388a9e3b9；message msg_09394c523a164a07016ad9f8ba1a8c8191a19f05d3a385e996。
- role planner；status proposed_readonly；chosen_task_id ui-regional-operations；plan_version RL-UI-REGIONAL-OPERATIONS-v1。完整原回复已读取，四元组和目标匹配；建议不构成人类批准。
- 主控按真实代码补充了旧按下执行入口清理、逐命令门禁及失败查询默认零值的显示边界，未增加业务规则；本计划保持策划指定的三个标签和原操作范围。
- 证据目录：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-next-after-lost-recovery-20261022；before.json、planner-request.txt、planner-send.json、planner-result.json和保护审计保留。

</details>

</details>

</details>
