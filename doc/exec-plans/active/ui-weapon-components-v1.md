# 武器组件改装界面统一 v1

**状态：用户已验收，正在提交、推送和合并。** 本方案让玩家在确认前看清当前装配、属性变化、连带拆卸和返还位置。它接续已交付的“供应与回收”页面，使用同一套界面风格。

**下一步：交付同一已验收候选，验证准确提交和合并提交的双平台CI。** 桌面入口和程序保持，不开始下一项开发。

## 已完成与复验

- 三栏改装页已经接入两个原入口；武器与组件独立分页，未选择操作不会自动恢复默认。实例、组件位和预览过期时停止确认，不换成其他同名物品。
- 六项属性、明确标注的详情／返还清单和完整原因已接入原查询；中文完整名称先翻译再组合。英文表头使用短词；长详情可滚动。确认、返回和所有取消路径均有输入保护。
- Windows Debug全目标构建通过；完整注册CTest **2306/2306**，新增改装专项 **25/25**，0失败、0跳过。原2281项全部保留，原扇形视野、手电和诱饵等3ms相关检查通过，未调整负载或门槛。
- 三档窗口、中英文63张离屏图及7张汇总图已检查；原资源、入口、存档结构、玩法代码和其他工作区修改保持。没有代为启动游戏或访问真实存档。

继续使用桌面 **Project Raidline Debug**，按[简短试玩清单](../../acceptance/ui-weapon-components-v1.md)复验。当前候选保持冻结，用户已验收并授权交付；新的提交、推送、合并和双平台CI状态见本文末交付记录。

## 玩家会看到什么

沿用1200×600逻辑面板、深色底和灰绿强调色。标题22、正文14、辅助12逻辑像素；不另起一套视觉风格。

| 区域 | 本版安排 |
| --- | --- |
| 左栏：武器 | 每页6把，短名、小图和选中状态。重复型号仍按独立实例列出，用必要的来源或装配信息区分。上一页、下一页和页码固定可见。 |
| 中栏：装配 | 当前武器的等比图片；枪管、护木、握把、武器手电四个组件位，明确显示当前组件或默认结构。下方显示所选位置的可用组件与“恢复默认结构”，每页6项。 |
| 右栏：本次变化 | 六项属性的当前值、预览值和变化量；完整拆卸清单、返还仓库页码和页内位置，或本次无法执行的原因。 |
| 底部 | 固定的“确认改装”、返回按钮和结果反馈；列表和长名称不能挤走主操作。 |

武器列表与组件列表独立翻页，鼠标位于对应列表上时可用滚轮翻页。返还内容或原因过长时在详情区域换行、滚动查看，关键失败原因不能仅用省略号代替。

列表使用短名，详情保留完整名称。图片复用现有已发布资源；缺图保留现有文字／几何占位。图片等比显示，不拉伸，也不承诺随配件实时拼装枪械外形。按钮悬停、选中、按压使用约120毫秒轻微渐变，点击区域保持固定。

## 操作怎样变化

1. 从“供应与回收”的“武器改装”进入，或在背包／仓库右键枪械选择“改装”。后者预选刚才点击的那把枪；只展示现有规则允许访问的实例。
2. 初次进入、换枪或换组件位时，**只查看当前装配**。未选择操作时，“确认改装”不可用，不能把“尚未选择”解释为“恢复默认结构”。
3. 主动选择一件组件或“恢复默认结构”后，才生成待确认预览。翻页清除待确认选择，不能保留不可见的旧操作；浏览、悬停和切换都不改物品。
4. 有效预览显示六项属性的前后差异，以及本次会拆下的全部零件。恢复默认或更换接口组件时，相关握把／手电的连带拆卸也列出。空间不足或接口不符时禁用确认并说明原因。
5. 同一确认按钮内按下并松开才执行一次。成功后显示结果并回到“只查看当前装配”；失败显示完整原因，保留可用目标。返回时放弃未确认操作，回到原来源页面。

供应入口保留已验收的返回行为和各标签页码；库存入口保留库存浏览位置。返回、Esc、Tab、失焦、切换窗口或其他菜单都清理未完成按压，不能穿透成开火、拖拽或另一个按钮的操作。

确认前重新检查同一实例、组件位及预览。如果来源、实例或档案版本已经变化，停止本次操作并刷新，要求重新选择；不能自动改成另一把同名枪或改成“恢复默认结构”。

## 属性与返还以原规则为准

六项属性为后坐力控制、稳定性、操控速度、人机功效、精度和有效射程。当前值来自现有有效武器属性，合法预览来自现有改装查询；界面只计算展示用差值，不另算改装效果或综合评分。

没有待确认操作时显示当前值，预览列留空；查询拒绝时显示“无法执行”和原因，不把当前值重复一遍当作成功预览。

只有查询成功时，才将其返还清单显示为可执行结果。仓库页码和页内坐标通过现有独立仓库页映射转换，不承诺退回当前浏览页。查询失败时即使已返回部分临时结果，也不能将其展示成可以部分完成的方案。

查询会复制档案进行检查，因此仅在实例、组件位、候选或档案版本变化时重新查询；悬停和动画不每帧重建查询或扫描全仓库。没有操作时不发空命令。

## 保留和排除

保留已验收的准星、菜单、同页配装、独立仓库页、轮盘、搜尸、视野、手电电池、近战、诱饵和地图权限行为；保留原存档、固定测试入口以及原四项3ms性能负载和门槛。

改装继续使用原支持平台、可访问来源、接口依赖和一次性事务。普通对局不得改装；已有特殊活动及控制台权限保持原边界。默认武器结构仍能正常使用，缺少升级配件不代表缺少必需部件。

不增加枪械、配件、组件位、商品、费用、耗时、解锁规则或快捷交易。不改属性算法、资产拥有关系、死亡／寻回规则、仓库容量或存档结构。不同时翻新武器详情、电池页、医疗所、宿舍、工坊、出击页等其他页面。

不需要新美术或音频，不生产资源、不修改Manifest，不接触真实存档、外部GDD、信任、权限或模型。

## 获批后的开发与验收

先把“未选择／主动恢复默认”及两个入口的输入规则落实为可测状态，再接入统一布局、只读预览和文字，最后完成相关回归与离屏检查。实施完成后提供一个冻结候选和简短验收清单，等待人工验收；暂不提交、推送或合并。

继续使用桌面 **Project Raidline Debug**，不新增测试入口。正常验收以基地已有支持改装的枪械和组件为前提；难以在正常游玩构造的情况由隔离临时档案测试，不要求用户清空仓库或修改真实存档。

| 操作 | 应看到的结果 |
| --- | --- |
| 从“供应与回收”和背包右键分别进入改装，再返回。 | 右键入口选对枪；两个入口风格一致，返回对应来源页面，库存浏览位置保持。 |
| 浏览、换枪、换组件位、翻页，然后不选组件直接尝试确认。 | 只显示当前装配，不自动拆件；没有明确操作不能确认。 |
| 选择组件或恢复默认，查看本次变化。 | 六项属性对齐，所有拆卸物和返还去向清楚；接口不符时显示原因。 |
| 完成一次明确确认，再关闭重开页面。 | 只改动目标实例一次；装配和属性正确，没有残留按压或穿透。 |
| 切换三档窗口和中英文。 | 图片等比，短名易读，完整原因可查看，按钮及返回入口始终可用。 |

自动化重点验证：同型号多实例与超过一页的来源；只读预览不改变档案；无操作不执行；滚动／移出／失焦取消按压；失效实例及过期预览不能替换目标；接口不足、连带拆卸、仓库满、保存失败均原子拒绝；旧存档装配和手电／电池归属不变。成功持久化及重开也使用临时档案覆盖。

构建全部受影响目标，运行相关专项和完整注册CTest；不得删除、缩减或放宽原四项3ms检查。离屏核查1280×720、1600×900、1920×1080及中英文、空列表、长名称与长原因。代理不代为启动游戏或宣称人工验收。

<details>
<summary>主控核查、文件范围及协调证据（规划历史）</summary>

### 已核查的真实实现

以下均基于实际开发目录 Project Raidline-ui-inventory-presentation-v1 的已交付源码，不以路由根旧代码推断当前实现。

- src/app.cpp 中 componentWeapons、componentChoices、renderWeaponComponents、handleWeaponComponentsClick 和 weaponComponentPreview：现有双列表每页6项，四个组件位、六项属性；选择使用实例ID，临时值0同时被用作恢复默认。旧绘制会对失效武器自动选首项、失效组件自动设0；本片需要分清浏览选择与待执行操作，避免目标漂移。
- src/app.cpp 中 handleProfileInventoryUiEvent、handleProfileContextMenuPointer 及库存显示分支：库存右键预选原武器；改装页目前主要处理鼠标按下。需要在本页独立接收松开、移动、滚轮及取消事件，不改变详情／电池和库存其他输入。
- src/app_ui_base_supply.cpp 中 handleBaseSupplyEvent、renderBaseSupply、resetBaseSupplyUi：供应入口和库存入口共用同一改装页，供应改装内容目前仍转交旧按下处理；供应返回已经有按下／松开保护，本片沿用其交易页面行为。
- src/weapon_component_domain.h/.cpp 的 WeaponComponentPlan、queryWeaponComponentChange、effectiveWeaponUse：已有接口校验、属性前后值、连带拆卸和每项返还目的地。失败计划可能包含部分临时返还，只有canCommit=true才能作为完整可执行结果展示。查询内部复制Profile，不能每帧调用。
- src/game_session.cpp 的 executeBaseWeaponComponentChange：原领域命令后，候选档案保存成功才提交；保存失败保持原档案，成功后刷新原武器配置。客户端继续经此入口，不能直接迁移资产。
- src/profile_state.cpp 的 assetIsBaseAccessible：沿用现有可达拥有链；组件候选须同时为StoredAssetLocation。不能误改成只允许仓库直属，也不能纳入失物等不可达资产。
- src/profile_container_presentation.h/.cpp 的 ProfileInventoryViewport 和 fitInventoryTexture：复用现有独立页映射及等比图片规则；src/app_ui_base_supply.cpp 已有同类图片／占位显示。
- src/ui_localization.cpp：已核对“武器改装”“改装”“返回背包”“返回供应与回收”“枪管／护木／握把／武器手电”等实际中文入口。六项属性已有翻译；本片仅补本页短标签和原因。
- 既有 tests/test_weapon_components.cpp、tests/test_weapon_inventory_ui.cpp、tests/test_ui_item_context.cpp、tests/test_weapon_light_ui.cpp 及CMake注册已核对。普通Raid禁止改装、实例保存、连带返还、失败原子性均有现有回归依据。

以上是静态代码核查，不是运行时Bug复现或视觉验收。本轮未构建、运行测试或启动游戏。

### 拟修改与拟新增

| 路径 | 操作与原因 |
| --- | --- |
| src/app.cpp、src/app.h | 修改：两个入口、临时选择、退出清理和事件路由；将本页实现移出App主体。既有详情／电池页面行为保持。 |
| src/app_ui_weapon_components.cpp | 拟新增：改装页绘制、一次确认、分页、结果反馈与原查询／会话命令衔接。 |
| src/ui_weapon_components_presentation.h/.cpp | 拟新增：本页稳定实例行、明确的无操作状态、四个组件位、共享布局及命中区域；只读客户端投影，不拥有资产。 |
| src/app_ui_base_supply.cpp | 修改：进入／返回及事件转交到同一改装页；原购买、回收、维护和救济合同保持。 |
| src/ui_localization.cpp | 修改：本页短中文、空态、预览状态及完整原因的显示。 |
| src/app_ui_foundation.cpp、src/ui_item_context_presentation.h/.cpp、src/profile_container_presentation.h/.cpp | 按需复用；只有确有当前消费者时提取最小图片／文字辅助。默认参数和旧调用行为保持，不重构通用UI框架。 |
| tests/test_ui_weapon_components.cpp | 拟新增：两个入口、选择／取消／确认、分页、失效与过期计划、只读预览及完整结果显示。 |
| CMakeLists.txt | 修改：新增实现归属raidline_sdl_client，并注册测试；domain和simulation保持SDL-free。 |
| 既有相关UI测试 | 按新共享布局适配坐标夹具，保留原权限、输入和领域断言。 |

不计划修改weapon_component_domain、game_session事务、资产／存档定义、内容文件或正式资源。若必须改变这些公开行为，先修订方案，不能作为UI内部细节直接实施。

### 规划时的基线与保护（历史）

- task_id：ui-weapon-components；plan_version：RL-UI-WEAPON-COMPONENTS-v1；state：waiting_plan_approval。
- approval_source：无。2026-10-20主控线程直接人类原文“请协调策划线程给出下一步优化方案”只授权本轮协调与方案，不是实施批准。
- candidate_id：无；accepted_candidate_id：无；acceptance_source：无。本任务尚无实现候选，不能沿用上一任务验收。
- 已验收依赖：PR182，main 038a073d5763a90b7233ae969355a54505fa2572，树990e99f42f859af533203464a0dc93190a7dd733。准确PR和main双平台均2281/2281首轮通过；运行39271551958／39273665194。本轮fresh fetch及远端状态已复核。
- 实际开发目录：E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1；分支codex/ui-base-supply-v1；HEAD 713acfe894d33c7b3f511d4e93fe26c8e632522d，树与上述main一致。路由根E:\WorkPlace\Projects\C\Project Raidline仍保留旧分支及原未提交修改。
- 获批后再从最新已验收origin/main创建codex/ui-weapon-components-v1，复用实际开发目录和唯一桌面入口；切换前原字节保留未提交文档，不强切或清理。当前未创建分支或改动源码。
- 原桌面快捷方式：C:\Users\Administrator\Desktop\Project Raidline Debug.lnk。目标build/windows-debug/Project_Raidline.exe的SHA256为7dcb3b7c7838308373b257f2c8366f454958367cb7b2bf65c6b2356e79c9a2cb。
- 原候选RL-UI-BASE-SUPPLY-v1-local-536582b5da36保持；本轮已调用既有交付只读审计，错误为0。五个相关工作区的原修改及原缺失文件保留。
- 回退：方案未批准时仅保留本计划和状态记录；获批后实现若需回退，只撤本任务改动并恢复已验收程序，不覆盖原存档或其他工作区。源码／程序检查点与候选绑定，任何实质变更均须新候选复验。
- 本轮Git行为仅fetch和读取PR／CI；未提交、推送、合并。实现完成先等验收，远端交付另按真实人类指示执行。

### 策划协调

- 原目标：策划线程，019feff4-477c-75c2-bf31-3f09ff1e3891，host local。已用原生list/send/wait及定向read核对，未创建替代线程。
- request_id：MAIN-UI-NEXT-POST182-20261020-01；task_id：ui-next-selection；version：RL-UI-NEXT-SELECTION-20261020-v1。
- baseline：main 038a073d5763a90b7233ae969355a54505fa2572; tree 990e99f42f859af533203464a0dc93190a7dd733。
- 有效回复turn：01a156f0-a4a0-7a61-a35c-03a93ad4158d；message：msg_09394c523a164a07016ad6c570557c8191b8a06bb0139d03ea。
- role：planner；status：proposal_complete_waiting_human_approval；chosen_task_id：ui-weapon-components；plan_version：RL-UI-WEAPON-COMPONENTS-v1。四元组和目标一致，pending_request：无。
- 主控采纳选题并核实真实代码；补充明确6行分页、失败计划不展示部分提交、查询缓存及两个原入口的输入／返回范围。未扩大改装玩法。
- 证据目录：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-next-after182-plan-20261020；保存请求、原回复、五工作区与入口／源码保护记录。旧交付证据保留在ui-base-supply-v1-delivery-20261019。

</details>


## 实施授权与启动记录（历史）

- 启动时state：in_progress；task_id：ui-weapon-components；plan_version：RL-UI-WEAPON-COMPONENTS-v1。
- 实际人类批准：主控线程01a0ddf7-3cbe-7403-8f5b-92e2a0cab9ad，2026-10-20原文：“同意 RL-UI-WEAPON-COMPONENTS-v1，请按方案实施，保留已验收的 UI 和玩法行为、原存档、固定测试入口及3ms性能门槛，完成后提供简洁的验收清单，暂不提交、推送或合并。”
- 分支codex/ui-weapon-components-v1从已验收origin/main038a073创建，HEAD仍为038a073；切换后五工作区原未提交内容已逐项核对保留。未提交新内容不能以HEAD当候选。
- candidate_id：无；accepted_candidate_id：无；acceptance_source：无。构建和回归正在进行，尚无通过结论。
- 保护／实施证据：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-weapon-components-v1-20261020。原程序单独保留为accepted-program.bin；入口不变，不访问真实存档。

## 冻结候选与本地验证（验收前历史）

- task_id：ui-weapon-components；plan_version：RL-UI-WEAPON-COMPONENTS-v1；state：waiting_human_acceptance。
- approval_source：2026-10-20主控线程直接人类批准，原文保留在前述实施授权和before.json中。candidate_id：RL-UI-WEAPON-COMPONENTS-v1-local-b78d1d0595b6；accepted_candidate_id：无；acceptance_source：无。
- 当前分支codex/ui-weapon-components-v1，HEAD仍为已验收基线038a073d5763a90b7233ae969355a54505fa2572。本候选为未提交工作区，不能把该HEAD称为新候选提交。441项源码／测试／CMake清单与原字节哈希冻结，指纹b78d1d0595b62ff609481dc8bc1a6fa1294f0e9f72a2064b9d681bc1354d9b22。
- 实际程序：E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1\build\windows-debug\Project_Raidline.exe；SHA256：fe66fe26f9a69951f740f4daaaa9f37843514f3c991aa2e19b9c3da2cd33ca5d。同一桌面入口及工作目录未变；上一已验收程序保存在本任务证据目录accepted-program.bin，未另增用户入口。
- 实际修改12项源码／构建／测试路径，见source-freeze.json的changed列表。其中新增3项客户端实现／投影文件和1项测试；其余为两个入口、取消清理、短文本与2项旧UI夹具的几何坐标适配。没有修改领域算法、会话事务、存档、商品或资源；与批准方案没有实质偏差。
- 最终构建日志incremental-5.log；完整CTest命令保留在full-final-console.log，结果full-final.xml／test-results.json，2306/2306，耗时289.74秒，完整回归首轮通过。最终专项component-final.xml，25/25；两者对应同一源码和144项测试／游戏二进制哈希。源、程序、74项运行资源、7项DLL、入口及五工作区保护核对见audit.json，错误为0。
- 显示证据visuals-frozen：63张预览、7张汇总，均已检查。包括中英文／三档窗口、预览／拒绝、连带返还、长原因滚动、首尾页、空列表、缓存与保存失败；和最终程序再次捕获的像素一致。缺图沿用原占位，没有生产美术。
- 未执行：正常启动与人工游玩验收；真实存档读写；新提交、推送、PR、合并及当前候选远端CI。没有待收策划请求或新增资源包。

### 已解决的实施检查问题

- 本机8个锁定版本的vcpkg缓存目录为空，首次配置未能加载清单；从已有对应Git对象恢复，并逐blob核对哈希，未改依赖版本。随后确认所有请求包已安装并完成配置；不改信任／权限／模型。
- 首次编译发现新增测试的string_view拼接类型错误，已修正。首轮21项专项因临时目录末尾分隔符的清理断言失败，另有1项仓库页预期写错；改为核对真实绝对目录，按既有24列仓库规则更正预期。没有改仓库规则或放宽检查。
- 中间170项及172项相关专项通过。离屏检查发现英文表头过长和名称组合产生部分英文，已在本页内修正后重新构建、冻结并完成上述完整回归。
- 一次单独GoogleTest执行的25项主体结束后，因报告目标含中文路径，报告写出退出码3；不将其记作成功命令。改用ASCII临时报告路径，再复制报告，最终同一候选25/25、退出0。原日志component-frozen.log保留。
- 原失败、诊断与中间日志均保留；上述问题已解决，没有用旧程序、跳过测试或降低3ms门槛替代验证。

候选现保持冻结。后续任何实质修改需重新绑定候选并复验；没有开始下一项开发。

## 当前交付记录（2026-10-20）

- task_id：ui-weapon-components；plan_version：RL-UI-WEAPON-COMPONENTS-v1；state：accepted_pending_delivery。
- 实际人类验收及远端授权：主控线程01a0ddf7-3cbe-7403-8f5b-92e2a0cab9ad，2026-10-20原文：“武器改装界面 v1 验收通过，请完成提交、推送和合并。”。此授权对应原样冻结的RL-UI-WEAPON-COMPONENTS-v1-local-b78d1d0595b6，accepted_candidate_id与candidate_id一致。
- 交付范围为冻结清单中12项源码／构建／测试文件及本任务3份文档。7份无关历史文档修改明确排除；五个工作区原改动与缺失文件按字节保留。
- 已核对已验收依赖PR182、main038a073、准确main双平台成功。没有重复开发、重建或重做已通过的完整本地回归。
- 新提交、远端分支、PR和CI尚待完成；不会将“已验收”直接写成“已合并”。只作普通PR合并，不改写历史、强推、删除分支或绕过检查。
- 交付证据：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-weapon-components-v1-delivery-20261020。源码和程序仍由原freeze.json识别；最终Git／CI及策划同步完成后补记，不另建仅抄CI状态的提交。
