# 基地显示减负：UI已验收，正在补齐CI验证

**游戏程序保持已验收版本。** 首轮Windows检查通过，Ubuntu发现旧测试的文字采样位置过期；仅同步两处测试坐标，继续同一PR交付。

- 本地重新构建、聚焦2项及完整2644项检查通过。
- [PR #192](https://github.com/Disthinker/Project-Raidline/pull/192)等待新提交的双平台检查，通过后合并。
- 原桌面入口、存档及已验收UI和玩法保持；交付后结束UI专项，不启动下一任务。

<details>
<summary>测试兼容修正与交付证据</summary>

- state：accepted_ci_pending；task_id：ui-final-consolidation；fix_plan_version：RL-UI-BASE-DECLUTTER-v1；CI兼容修正：RL-UI-TEST-RANGE-SAMPLING-COMPAT-v1。
- 人类验收与交付授权：2026-10-24主控直接原文“基地显示减负 v1 复验通过，请完成提交、推送和合并。”；已验收产品候选`RL-UI-BASE-DECLUTTER-v1-local-544b4a735d34`保持。
- 同一获批UI收官范围内，按AGENTS允许的普通测试失败修正，仅将原测试采样y126→118、568→560同步到已验收布局；20px高度、宽度、亮度>150、像素数>30、双语与其余断言保持，没有降低门槛或skip。没有新增或模拟人类批准，没有改动正式界面、玩法、存档、构建或CI配置。
- 原157份测试现为156份逐字节不变，1份仅两坐标变化；不能继续把157全不变写为当前事实。原五项3ms负载、计时、阈值和断言全部不变。
- 首轮[PR CI 40071692174](https://github.com/Disthinker/Project-Raidline/actions/runs/40071692174)准确提交1aacebf：Windows2644/2644通过，Ubuntu2643/2644，唯一失败为旧测试场文字采样。两平台原五项3ms通过；首轮整体为failure，不能当作交付通过。
- Linux已有SDL固定8×8字形，新文字118..125和560..567不落在旧126/568采样带；Windows字形较高所以原检查可过。已核对不是文字丢失、颜色变暗或字体加载失败；没有为通过检查调整游戏画面。
- 当前CI测试兼容候选`RL-UI-BASE-DECLUTTER-v1-ci-test-621613274734`；全源码/测试488文件指纹`621613274734167b1ce3de373ad65ad4b7357633c16d3cdf21d7d2bb7282e641`。相对人类验收仅原测试一文件改变，其余487份源码逐字节相同；正式程序仍为`d4caf356cbe3374415b52d995614d014665c81bccf6769847ecb45d02ca7c5ee`。产品验收仍绑定同一程序，未要求无变化游戏重新验收。
- Windows构建4仅重建DeveloperRangeUiTest，聚焦2/2、最新完整2644/2644通过，0失败/跳过，耗时519.73秒；五项3ms实际通过。原程序及其余156份可执行文件不变，资源74、DLL7、固定入口不变。
- 累计PR范围为原32文件加1份测试，共33文件；本次追加提交仅1份测试和同一3份交付记录。原25份无关文档、预览及五工作区其他修改不提交；Git钩子曾归一化4文档换行，已据事前备份/指纹恢复原字节，不改钩子或配置。
- 原提交1aacebf已推送，[PR #192](https://github.com/Disthinker/Project-Raidline/pull/192)尚未合并。追加提交后等待新准确HEAD的双平台检查，通过才合并，再核对主线CI。没有启动游戏，也不启动下一任务。
- 支持证据：`C:\Users\Administrator\Documents\美术资源生成\tmp\ui-base-declutter-v1-20261024\delivery\test-coordinate-compat`；完整full-2.log/xml/results和build-4.log/result在上一级证据目录。原CI日志/行号/哈希保留在delivery/first-ci-review，未覆盖失败记录。

</details>

<details>
<summary>前序提交、验收、方案与检查历史</summary>

以下旧状态为历史，当前以上方为准。

# 基地显示减负：已验收，PR双平台检查中

**当前候选已通过用户复验。** 已验收，PR双平台检查中。

- state：accepted_ci_pending；task_id：ui-final-consolidation；fix_plan_version：RL-UI-BASE-DECLUTTER-v1；parent_plan_version：RL-UI-FINAL-CONSOLIDATION-v1；pending_request：无。
- accepted_candidate_id：`RL-UI-BASE-DECLUTTER-v1-local-544b4a735d34`；人类验收与远端交付授权：2026-10-24主控直接原文“基地显示减负 v1 复验通过，请完成提交、推送和合并。”。
- 代码提交`1aacebf1feb211490405d3916805bd5add0a864d`，已推送到codex/ui-final-consolidation-v1；[PR #192](https://github.com/Disthinker/Project-Raidline/pull/192)。仅提交约定32文件，未夹带原25份无关脏文档或预览。
- PR双平台检查正在运行；通过前不合并。 [PR CI 40071692174](https://github.com/Disthinker/Project-Raidline/actions/runs/40071692174)绑定上述准确提交。尚未合并，合并后继续核对主线双平台CI。
- 已验收源码488文件SHA256：`544b4a735d34afa76fee258a91967c75e0c8e7fb0b100c56cc2283827cb5125c`；程序SHA256：`d4caf356cbe3374415b52d995614d014665c81bccf6769847ecb45d02ca7c5ee`。原桌面Project Raidline Debug保持，实际开发目录`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1`；为保护未提交资料，仍保留当前开发分支，不切换其他工作区。
- 本地Windows构建、50项UI及2644项全量检查已通过；原157份测试源码、原五项3ms门槛、74份运行资源及7份DLL保持。没有读取真实存档或启动游戏。
- 本次结束UI专项，不启动下一任务。完整证据见`C:\Users\Administrator\Documents\美术资源生成\tmp\ui-base-declutter-v1-20261024\delivery`；Git钩子曾归一化4份未提交文档的换行，已据原备份逐字节恢复并核对事前哈希，没有修改钩子或配置。

本段为交付进度的本地补记；不为复制CI状态新增提交。

<details>
<summary>提交时记录及前序方案与检查历史</summary>

以下“正在交付”“待复验”“待批准”均为历史，当前以上方实际结果为准。

# 基地显示减负：已验收，正在交付

**RL-UI-BASE-DECLUTTER-v1 当前候选已通过用户复验。** 本次合并包含完整UI收官及基地显示修正，保留已验收行为。

- state：accepted_delivering；task_id：ui-final-consolidation；fix_plan_version：RL-UI-BASE-DECLUTTER-v1；parent_plan_version：RL-UI-FINAL-CONSOLIDATION-v1。
- accepted_candidate_id：`RL-UI-BASE-DECLUTTER-v1-local-544b4a735d34`；acceptance_source：2026-10-24主控直接人类原文“基地显示减负 v1 复验通过，请完成提交、推送和合并。”。本次明确授权提交、推送和合并。
- 已验收源码488文件SHA256：`544b4a735d34afa76fee258a91967c75e0c8e7fb0b100c56cc2283827cb5125c`；程序SHA256：`d4caf356cbe3374415b52d995614d014665c81bccf6769847ecb45d02ca7c5ee`，交付前重新核对一致。
- Windows本地构建、UI专项50/50、完整2644/2644及原五项3ms通过。准确PR双平台CI尚未执行，须通过后再合并；不得用前序主线CI代替。
- 交付范围为原UI收官与本次修正共32文件；原25份其他脏文档和.codex/tmp不提交。原存档、固定入口及其他工作区修改保持。
- 实际目录`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1`，分支codex/ui-final-consolidation-v1，已核对origin/main与开发基线均为`5f534963e13bb42dfd154e45cf243e279978342a`。
- 本次结束UI专项；不启动下一开发任务，不启动游戏。准确提交、PR、CI及合并结果将在本记录与支持证据中继续记录。
- 支持证据：`C:\Users\Administrator\Documents\美术资源生成\tmp\ui-base-declutter-v1-20261024\delivery`；冻结及本地验证证据位于上一级目录。

<details>
<summary>验收前方案、修复与检查历史</summary>

以下待复验、待批准及禁止交付字段为历史；当前以上方人类验收与交付授权为准。

# 基地主界面减负：修复完成，待复验

**RL-UI-BASE-DECLUTTER-v1 已完成并冻结，候选 544b4a735d34 等待用户复验。** 这是本次UI收官候选的验收修正，不另开下一轮UI专项。

继续使用原桌面 **Project Raidline Debug**，按[这份简短复验清单](../../acceptance/ui-final-consolidation-v1.md)检查。尚未获得本候选人工验收；未提交、推送、合并或启动游戏。

## 现在的显示

- 基地底部不再常驻区域/敌人数、长快捷键和大生命/武器框。已知普通通知约3秒淡出，错误和未知原因保留原文并可滚动。
- 设施每处最多一行短名，门框、床铺、工作台、箱体与单个人员存在图形替代常驻台账；空心/实心和警示形状辅助区分状态，完整数量仍在原管理页。
- 靠近目标时只显示一条底中交互提示，保留原地面物品优先及设施距离查询。日常安全状态隐藏大生命框，受伤、外围和防守显示紧凑生命、弹药及异常；疼痛抑制不会被当作痊愈。
- 真实整体图发现已完成教程仍有大块“目标已完成”，本次一并停止绘制该空目标。未完成教程、首Raid引导、H隐藏和原推进/保存保持。

## 实际验证

- Windows Debug x64全目标构建成功；UI专项 **50/50通过**；完整CTest **2644/2644通过，0失败、0跳过**。父候选原2636个测试名全部保留，本修正新增8项，已验收基线的原157份测试源码逐字节不变。
- 原五项3ms的负载、计时、阈值、断言及注册保持，并在本次完整运行通过。失物寻回38项串行、其余注册项并行4，完整覆盖且每项恰执行一次；原始两组JUnit和日志保留。
- 修改前后均生成并实际查看完整基地构图，覆盖三种窗口尺寸、中英文、密集设施、靠近目标、普通通知淡出、完整错误、受伤、外围、防守和建设缩放；看图名单及边界见支持目录审图记录。生成数量不等于逐张全尺寸细读数量。
- 保留原存档和固定入口；五工作区非本修正拥有文件、索引、HEAD及分支核对保持，74份运行资源和7份DLL保持。没有读取真实存档，罕见状态用隔离档案。

自动化与离屏审图不能替代本候选的人工复验。未提交候选没有新远端CI；将来获准交付时仍需准确提交的双平台检查。

<details>
<summary>候选、授权与保护证据</summary>

- task_id：ui-final-consolidation；parent_plan_version：RL-UI-FINAL-CONSOLIDATION-v1；fix_plan_version：RL-UI-BASE-DECLUTTER-v1；state：awaiting_human_acceptance；pending_request：无。
- candidate_id：`RL-UI-BASE-DECLUTTER-v1-local-544b4a735d34`；accepted_candidate_id：无；human_acceptance：无。父候选`RL-UI-FINAL-CONSOLIDATION-v1-local-3a67e1a7b83d`未通过基地显示项，保留为前序证据，不再作为当前程序。
- approval_source：2026-10-24主控直接人类原文“同意 RL-UI-BASE-DECLUTTER-v1，请按方案修正，保留已验收的UI和玩法行为、原存档、固定测试入口及3ms性能门槛。完成后提供简洁的复验清单，暂不提交、推送或合并。”。
- 实际目录：`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1`；分支`codex/ui-final-consolidation-v1`；HEAD`5f534963e13bb42dfd154e45cf243e279978342a`，未产生提交。
- 源码488文件SHA256：`544b4a735d34afa76fee258a91967c75e0c8e7fb0b100c56cc2283827cb5125c`；算法与映射见candidate.json。程序`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1\build\windows-debug\Project_Raidline.exe`，SHA256`d4caf356cbe3374415b52d995614d014665c81bccf6769847ecb45d02ca7c5ee`。
- 固定快捷方式`C:\Users\Administrator\Desktop\Project Raidline Debug.lnk`，SHA256`6c2d05a98e66a1a724dd119dbc676cf130f075b0a688722324fda93b6ec61c28`；目标仍为上述程序、工作目录不变、空参数。
- 支持证据：`C:\Users\Administrator\Documents\美术资源生成\tmp\ui-base-declutter-v1-20261024`。before.json、baseline-source（父候选全部488文件）、parent-program.bin及before-docs保留修正前检查点。
- 改动相对父候选恰7份客户端/自有测试文件：src/app.cpp, src/app.h, src/app_ui_final.cpp, src/ui_base_management_presentation.cpp, src/ui_base_management_presentation.h, tests/test_ui_base_management.cpp, tests/test_ui_final_consolidation.cpp。App.cpp仅变化update中的客户端计时钩子、renderBaseWorld显示和renderBase空完成目标；原命令、管理页及输入处理未动。
- 本轮仍是原收官联合候选，交付范围为父候选29份源码/CMake加3份文档共32文件，其中7份源码含本次更新。不能只交付补丁而漏掉未提交的父UI依赖，也不能夹带原25份无关脏文档或.codex/tmp预览；准确清单见candidate.json的delivery_scope与completion-verification.json。
- 最终构建build-3.log与build-3-result.json；专项newui-3.log、newui-3.xml、newui-3-results.json与preview-dirs-newui-3.json；完整full-1.log、full-1.xml、full-1-results.json，耗时524.61秒。原登记在registered-before.json，最终在registered-full-1.json；所有case来自实际CTest。
- 审图与保护：base-declutter-visual-review.md、base-glyph-visual-review.md、root-visual-review.md、readonly-protection-review.md/json；父候选未改页面的审图来源仍保留在原父证据目录。
- 第一次专项已通过50项，随后整体图暴露旧完成目标大框，按获批日常减负范围修正并加未完成指引反向验证；图形补回批准表格要求的单个人员存在提示，仍不编码人数。首次专项不充当最终证据，最终以上述冻结版本检查为准。
- 第二轮专项49/50：新增地图开关检查把旧Profile的原首次外围初始化放到了展示基线之后。实际DTO差异为原外围资产21/22、9名敌人快照、原事务/next_asset_id/revision及checksum；测试将同一原Flow初始化作为前置，展示、通知和真实地图来回期间的完整Profile不变断言保留，未改变生产业务。原失败日志、base-initialization-profile-diff.json和最终notice-focus-3.xml保留供复核。
- 仅精确已知普通消息可淡出，包含存档载入、地图关闭等；未知、拒绝、备份恢复与未完成Raid恢复信息不会按子串被当作成功。两个保存blocked及原独立保护/重试层保持，展示计时不进入Profile或改变uiMessage_供其他页面使用的原文。
- 未改变领域/模拟/服务、内容定义、存档schema、地图权限、基地安全区、费用、时间、人物或物品所有权；没有修改信任、权限、配置、模型或外部GDD，没有生产美术/字体/音效。
- 回滚只恢复本修正拥有文件及已备份父候选程序，先按指纹核查，不做整树reset/clean。完成后冻结游戏文件等待复验；验收交付后结束UI专项，不启动下一开发任务。

</details>

<details>
<summary>批准前方案与前序收官记录</summary>

以下“待批准”“尚未修复”和旧通过数字属于历史；当前以上方冻结候选为准。

# 基地主界面减负：修正方案待批准

**RL-UI-BASE-DECLUTTER-v1｜诊断完成，尚未修复。** 用户截图已明确指出底部面板占地过大、设施周围文字堆叠。此次只修正本次收官候选的基地日常显示；不是新一轮UI专项。

目标是让日常画面以场景为主：短名称和简单图形足以识别设施，靠近再显示一条操作提示，详细数据仍在原管理页面查看。确认本版本后实施，再提供新候选复验。

## 已确认的原因

- `renderBaseDailyHud`常驻绘制快捷键栏，并对任何非空`uiMessage_`绘制宽650、高54的逻辑像素面板；“存档已载入”没有对应的自动消退展示状态。
- `renderBaseWorld`另画区域/敌人数面板、靠近交互面板，并无条件调用大生命/武器框。普通日常画面也会出现“生命100”“未装备枪械”等大块说明，窗口放大后占地同步放大。
- 普通基地缩放1.0即显示全部设施摘要；所有启用设施同时绘制名称、入口状态、工位、人员、居民或资源统计。选中或放大到1.25还增加第二行。通用标签避让角色并不解决标签彼此堆叠。
- 用户截图和真实源码相互印证；本轮没有启动游戏独立重现，也没有检查用户当前运行进程或读取其存档。冻结候选488份源码及程序已重新按哈希核对不变。

## 拟修正的显示

| 部分 | 预期行为 |
| --- | --- |
| 底部日常信息 | 移除常驻区域/敌人计数大框和长快捷键说明栏；原地图、管理及开发页面保持。靠近物品或设施时，仅保留一个当前目标的短交互提示。 |
| 普通通知 | “存档已载入”等已知成功/普通提示改为小型单行通知，约3秒后淡出。只改变基地展示，不清空其他页面使用的原反馈；保存失败、警告和无法确认类别的错误不自动吞掉。原重试与风险说明保留。 |
| 生命与武器 | 日常安全状态取消大生命框和“未装备枪械”常驻说明；受伤、出血、疼痛或需要战斗信息时显示紧凑状态，外围与基地防守保留必要生命、弹药及动作信息。Raid战斗HUD不在本修正范围。 |
| 设施与工作点 | 场景中每设施最多一行短名；门用门框，宿舍/医疗用床与枕头，工坊用桌面与桌腿，仓储用箱体等现有SDL线段/矩形示意。保留状态色，并用实心/空心或警示符号辅助，不依赖颜色单独表达。人员沿用已有小人示意。 |
| 详细数值 | 不再常驻入口“可用”、工位长名、人员职业、健康/伤员/床位和资源台账。完整状态、数量、原因和操作仍由原建设检查卡及设施专页承接，缩放/选中也不把整套台账重新铺回世界层。 |

## 实施范围与保护

| 已核查文件 | 获批后拟操作及原因 |
| --- | --- |
| src/app.cpp | 仅调整renderBaseWorld的标签、底部显示和原近距提示呈现；补入基地普通通知计时的客户端更新。保留原interactableFacility、nearestBaseGroundAsset、服务投影和全部输入/事务路径。 |
| src/app_ui_final.cpp、src/app.h | 调整日常HUD/紧凑状态及仅客户端展示计时字段；不改Profile或存档格式。 |
| src/ui_base_management_presentation.h/.cpp | 按需承载已有真实消费者所需的短名、图形和显示策略；不新增UI框架或通用消息系统。 |
| tests/test_ui_final_consolidation.cpp、tests/test_ui_base_management.cpp | 更新本收官候选新增的显示预期，并补真实基地整体构图、通知淡出/错误保留、设施密集场景和危险状态检查。原业务覆盖、原五项3ms负载/阈值/断言保持。 |
| 本任务、CURRENT_STATE及原联合验收清单 | 获批实施后更新同一份清单与新候选证据，不增加程序入口。 |

不改变地图情报权限、基地安全区、交互距离、人物与物品所有权、设施计时、岗位/资源/人口规则、准星和菜单行为。只用现有代码图形，不新增美术、字体、音效或设置选项；其他已验收页面保留。

## 如何确认

1. 实际调用完整renderBase离屏绘制，检查普通基地、接近目标、建设选中/缩放、受伤和防守状态；覆盖三种窗口尺寸及中英文，实际审查整体画面，不能只在空背景上单独画HUD。
2. 检查普通通知消退，保存失败及原重试仍可见；验证靠近提示仍与真实可交互目标一致，菜单/建设操作不误触。
3. 获批后执行Windows Debug全目标构建、相关专项和完整注册CTest，保留原五项3ms门槛。生成新源码/程序指纹，不能沿用旧候选的2636项通过结论。
4. 复验继续使用原桌面入口：基地站立看底部是否干净；绕宿舍/工坊走近，看是否只出现当前目标短提示；打开原管理页确认详细数据及操作仍完整。无需新建档案或故意损坏真档。

回滚只针对本修正拥有文件，实施前保存当前冻结源码及程序备份；不能整树reset/clean或覆盖其他未提交工作。仍不提交、推送、合并或启动游戏；待新候选通过用户复验后再按用户指示交付。

<details>
<summary>状态、截图与基线证据</summary>

- task_id：ui-final-consolidation；parent_plan_version：RL-UI-FINAL-CONSOLIDATION-v1；fix_plan_version：RL-UI-BASE-DECLUTTER-v1；state：awaiting_fix_approval；pending_request：无。
- fix_approval_source：无；反馈来源为2026-10-24主控直接用户附图，标注“文字过多且杂乱，可以通过简单图形示意”和“太大且不必要”。图像只作验收反馈，不当作修复版本批准。
- parent_candidate_id：RL-UI-FINAL-CONSOLIDATION-v1-local-3a67e1a7b83d；accepted_candidate_id：无。该候选仍保留，本项验收未通过；没有新修复候选。
- 实际目录：E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1；分支codex/ui-final-consolidation-v1；HEAD5f534963e13bb42dfd154e45cf243e279978342a。
- 原源码指纹3a67e1a7b83d78efde18ed9518d1a5cab4661f88959545a9220e07ff158aae7f；原程序7f553f6bb6bec52cbb0e05703f1e6c2a9f30e62a517f361656d6012e1565e2de，本轮复核一致。
- 截图：C:\Users\ADMINI~1\AppData\Local\Temp\codex-clipboard-b00c1c3b-a17e-46bc-a13d-dd8aee4892bf.png；SHA256为065c6a17e13ee82cb1c892a3895ceec6ec91124b48146e4d499ae4be9cab2bcb。
- 只读临时分工复核了世界标签来源与原地图入口，未改文件或运行测试；主控负责本方案和两份状态记录。无新策划选题、外部GDD或美术包变更。
- 支持证据保存在原实施证据目录的base-declutter-v1-proposal-20261024.json；本轮前的两份任务文档保存在base-declutter-proposal-before，不覆写原候选验证证据。

</details>

<details>
<summary>原收官候选、自动检查与前序计划</summary>

以下通过结果属于未改动的原候选；用户反馈后本项仍待修正批准，不表示修正已实现或验收通过。

# 全部剩余UI收官版：待联合验收

**RL-UI-FINAL-CONSOLIDATION-v1 已完成并冻结，候选 3a67e1a7b83d 等待用户验收。**

本轮一次完成基地建造管理与防守、战斗HUD、独立物品详情、地图提示、引导和开发面板；已验收的配装、轮盘及基地业务页保留主体，并完成兼容回归。

继续使用原桌面 **Project Raidline Debug**，按[这一份联合验收清单](../../acceptance/ui-final-consolidation-v1.md)试玩。没有新增程序入口。用户尚未验收；未提交、推送、合并或启动游戏。

本次作为UI专项收官候选，验收交付后结束主动UI优化，不再安排下一张美化页面；本轮不自动启动下一任务。

## 已完成的变化

| 部分 | 最终行为 |
| --- | --- |
| 基地管理与防守 | 建造、已有、运转、设施检查及右键统一；说明按需展开，收起后保留原工事放置区域；围攻、防守结果和保存重试显示完整风险。厨房旧页只显示真实资源与需求，不再出现没有对应处理器的交易按钮。 |
| 战斗与地图 | 必要生命、护甲、弹药、医疗、动作、手电、危机和交互保留；诊断由F9查看。地图标题图例、加载、首局提示及世界标签统一；标签避让角色并使用清楚的浅色字，撤离倒计时补全中文，交互范围、权限与可见性仍消费原查询。 |
| 独立详情与容器 | 枪械、手电电池、近战、诱饵详情统一，电池列表分页，完整失败原因可滚动；尸体、场景箱与基地地面容器保留访问和资产规则。 |
| 公共与既有界面 | 修正小按钮文字高度、菜单/供应长反馈；菜单保留已验收的按下恢复时机。其余新增离散按钮通过匹配的松开提交，取消失效目标和旧按压。已验收页面保留主体。 |

全部批准清单的逐项处理结论在支持目录 `final-coverage.md`；文件与实际测试对应，未开放的未来系统标为不适用，未留下“下一轮UI”工作包。

## 实际检查

- Windows Debug x64全目标构建成功；最终源码共488份，原473份全部存在，其中157份原测试源码逐字节不变。
- 完整CTest **2636/2636通过，0失败、0跳过**；原2594个测试名全部保留，新增42项。原五项3ms的负载、计时、阈值和断言保持，通过同一全量运行。
- 新增UI专项42/42、既有UI预览矩阵15/15通过；中英文和1280×720、1600×900、1920×1080实际生成预览。实际审图采用尺寸/语言联系图和关键原图，具体看图名单见各审图报告，没有把输出图片数量当作逐张细读数量。
- 五工作区95/25/16/3/14项无关原修改逐项哈希一致；索引内容一致。74份运行资源、7份DLL及固定快捷方式保持，未读取真实存档。

这些是本地自动化和离屏检查，不能替代用户验收。本未提交候选没有运行远端双平台CI；将来获准交付时仍须遵守准确PR和合并后main的CI门槛。

<details>
<summary>候选、授权与可复核技术证据</summary>

- task_id：ui-final-consolidation；version：RL-UI-FINAL-CONSOLIDATION-v1；state：awaiting_human_acceptance；pending_request：无。
- candidate_id：`RL-UI-FINAL-CONSOLIDATION-v1-local-3a67e1a7b83d`；human_acceptance：无；accepted_candidate_id：无。
- approval_source：2026-10-24主控直接人类原文“同意 RL-UI-FINAL-CONSOLIDATION-v1，请按方案一次性完成剩余所有UI，保留已验收的UI和玩法行为、原存档、固定测试入口及3ms性能门槛。完成后提供一份简洁的联合验收清单，暂不提交、推送或合并；本次交付后不再继续UI专项优化。”。
- 基线HEAD：`5f534963e13bb42dfd154e45cf243e279978342a`；基线tree：`eba658139eebe8b5435e07652bc5c71657ec973b`；当前分支：`codex/ui-final-consolidation-v1`。HEAD保持该已验收main，修改未提交。
- 实际开发目录：`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1`。
- 源码指纹：`3a67e1a7b83d78efde18ed9518d1a5cab4661f88959545a9220e07ff158aae7f`。算法：CMakeLists.txt及Git登记/未忽略的src、tests全部文件相对路径与SHA256映射，按键排序的紧凑JSON再取SHA256；488文件。映射在`candidate.json`。
- 程序：`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1\build\windows-debug\Project_Raidline.exe`；SHA256：`7f553f6bb6bec52cbb0e05703f1e6c2a9f30e62a517f361656d6012e1565e2de`。157份构建可执行文件的映射在同一候选记录。
- 固定快捷方式：`C:\Users\Administrator\Desktop\Project Raidline Debug.lnk`；SHA256：`6c2d05a98e66a1a724dd119dbc676cf130f075b0a688722324fda93b6ec61c28`；目标为上述程序，工作目录为同目录，参数为空。
- 证据目录：`C:\Users\Administrator\Documents\美术资源生成\tmp\ui-final-consolidation-v1-implementation-20261024`。原始`before.json`、不可变`baseline-source`与`original-dirty`用于前后核查；`accepted-program.bin`是原已验收程序的恢复备份。
- 最终构建：`build-18.log`、`build-18-result.json`；完整检查：`full-3.log`、`full-3.xml`、`full-3-results.json`，耗时422.34秒。
- 新UI：`newui-8-results.json`、`preview-dirs-newui-8.json`；既有矩阵：`visual-existing-1-results.json`、`preview-dirs-existing-1.json`。原测试登记在`registered-before.json`，最终在`registered-full-3.json`。
- 审图：`base-visual-review.md`、`final-aux-review.md`、`final-details-review.md`、`root-visual-review.md`、`final-accepted-inventory-review.md`、`final-accepted-base-services-review.md`、`final-accepted-operations-review.md`、`final-supplemental-visual-review.md`。主控审图与临时分工均有实际来源文件名；补充probe只用于隔离离屏审图，链接同一冻结客户端库，不改变正式源码、CTest登记或程序，不计入2636项回归。
- 边界复核：`final-invariants-review.md/.json`；原领域、模拟、服务、存档、内容定义、资产资源未改。新CMake源只归客户端，不向domain/simulation引入SDL。
- 本轮实现中发现的编译、文字高度和兼容失败已在同一获批版本修复；早期失败日志保留，不作为最终通过证据。最终保留旧菜单down-resume与未分区兼容世界敌人数，已发布地图不显示敌人总数；工事说明默认收起，不扩大业务规则。
- 首轮完整2632项通过后，真实场景补证发现原有角色遮挡交互标签、深色文字与新背板对比不足，以及撤离倒计时漏译；均在本已批准收官范围内修正并加4项真实绘图及观察回归。避让仍消费原缓存观察，文字不能移到墙后或未知区域；撤离区域名称放上沿，保留原进度位置。首次内部指纹53c0cf7216f7没有交给用户验收；`superseded-first-freeze-candidate.json`保留该中间记录，最终以本页当前候选和最终全量日志为准。补充probe首轮的尸体来源fixture和中文XML路径输出失败也保留原日志，不冒充生产缺陷或最终通过。
- 合格标签延后绘在原观察信息层内的角色上方，原墙体／扇形合成不变。新增真实尸体回归逐笔画对照独立双语文本，允许三角纹理与矩形参照缩放造成的1个物理像素差；旧敌人遮挡的六张图均不能通过该判定，当前六组均通过。保留原观察缓存次数、可见区域及Profile不变断言，详见`corpse-occlusion-distance-negative-control.json`和补充审图记录。
- 第二轮全量的唯一失败停在原寻回夹具临时目录创建，未进入业务断言；并发时钟命名冲突是分析假设，未借此修改原测试或产品代码。最终完整覆盖改为其他注册项并行4、全部38项寻回夹具串行1，两组各自原始CTest日志/XML均保留，合并报告只复制真实testcase且核对每项恰执行一次。具体命令、边界与原失败见`full-suite-isolation.md`；不是远端CI结果。
- 本轮仅拥有14份既有源码/CMake、15份新源码/测试、本任务与当前状态及一份新验收文档。原25份脏文档和`.codex/tmp`隔离预览不是交付代码，不得夹带后续提交。
- 回滚仅针对本候选拥有文件：使用不可变源备份与原已验收程序恢复；先按指纹检查，不用整树reset/clean，不删除或覆盖他人修改。此处是恢复说明，未执行回滚。
- 未修改信任、权限、模型、配置或外部GDD；未生产美术/音效，未分批推出新候选，未暂存/提交/推送/创建PR/合并。

</details>

<details>
<summary>已批准方案与前序策划证据</summary>

以下“待批准”“未实施”文字属于批准前历史，当前以上方冻结候选为准。

# 全部剩余UI一次性收官方案

**RL-UI-FINAL-CONSOLIDATION-v1｜待用户批准，尚未实施。**

这次完成当前游戏全部剩余UI，不再逐页推出优化任务。已经验收的页面保留主体，只做必要的一致性修补。内部可分工、分步验证，但最终只提交一个完整候选和一份联合验收清单。

用户已明确：本次验收交付后，结束主动UI专项优化。具体开发方案尚未获批；当前程序和固定桌面入口保持原样。

## 本次改什么

| 部分 | 本次结果 |
| --- | --- |
| 基地建造与管理 | 建设目录、已有设施、基地运转、设施检查和右键、放置移动、厨房与净水相关旧页统一；状态、条件、成本和耗时清楚。 |
| 基地防守 | 工事、围攻预警、自动／亲自防守选择、战斗信息和结果统一，完整显示风险及保存失败。 |
| 战斗画面与交互 | 生命、护甲、弹药、动作、近战、投掷、手电和撤离信息分层；开发诊断退出常驻HUD，仍可通过原开发入口查看。 |
| 独立物品详情 | 枪械、电池、近战和诱饵详情统一图片、属性、操作及返回；保留换电池、卸出等原有能力。 |
| 地图与辅助显示 | 两类地图的标题图例、加载、建筑／容器／尸体／救援／撤离提示、首局引导、通知、确认和错误统一，避免遮住操作。 |
| 开发测试界面 | 战斗测试场、性能和运行时开发面板统一控件、长列表、反馈和关闭恢复；不增加新测试能力或入口。 |
| 已验收页面 | 主菜单、暂停、设置、配装、仓库、右键、轮盘及全部基地业务页、出击、寻回、区域行动、对局结果只做一致性和异常状态回归，不推倒重做。 |

## 统一到什么程度

- 沿用现有深灰／灰绿风格，统一字号层级、间距、边框、按钮和悬浮反馈。物品图片等比；型号、口径和FPS／ms等合理缩写保留。
- 主页面只留关键状态和操作；长说明按需展开或滚动。空列表、长名称、禁用、选中、按下、处理中和错误都有明确显示，危险状态不只靠颜色。
- 显示布局与点击范围一致。整理后的离散按钮防连按、旧按压和点击穿透；拖拽、持续交互、轮盘和相机操作保留各自规则。
- 失焦、换语言、缩放、翻页或目标变化后，失效按压不能操作旧对象。已有危险操作确认次数、返回层级和快捷键不变，不给日常操作增加通用确认。
- 保存失败保留完整可读原因及原重试路径，不能被成功提示盖住。界面不能推测数量、价格、战利品收益、损失清单或未授权地图信息。
- 八分区空心轮盘、模糊背景及隐藏指针行为保持；HUD不套上全屏业务页外框。已验收配装布局、独立仓库和物品格大小保持。

源码中的英文可能已经经过统一翻译，不能全局替换后就宣称完成。实施后按实际绘制检查中文和英文；不改变内部稳定ID。

## 一次性完成的标准

1. 下方全UI清单每一项都有处理结论和证据，没有靠“下一次再优化”留下的页面或子菜单。
2. 当前正常流程和已有开发入口可达的页面均可用，关键信息不被遮挡、不溢出；长列表和多层弹窗能浏览、能返回。
3. 中英文及1280×720、1600×900、1920×1080均检查；小显示器仍按现有可用尺寸约束，不改世界视野或缩放规则。
4. 已验收玩法、原存档、物品位置、地图权限、窗口准星和菜单行为不退化；原2594项测试保留，五项3ms负载、阈值及断言不变。
5. 实施后冻结包含全部工作包的一个候选，通过同一份正常游玩清单验收。若需返工，仍属于本总任务，更新候选后复验，不另立下一张UI美化任务。

**收官后：**关闭主动UI优化专项，策划不再安排新的单页美化任务。真实Bug仍按项目修复流程处理；将来新玩法必需的界面随该功能交付，沿用本次样式，不重启一轮UI专项。本轮不自动启动下一项开发。

## 实施顺序与验证

以下是同一个版本内的工作包，不是多轮审批或分批候选。

| 工作包 | 退出条件 |
| --- | --- |
| A．公共一致性与覆盖登记 | 清单、公共控件状态、布局和输入规则明确；只补有实际调用方的公共辅助。 |
| B．基地管理与防守 | 建造、设施、厨房与净水、工事、防守全链均已收口，原费用、计时与保存规则保持。 |
| C．独立详情与容器边界 | 三种详情及电池操作完成；原库存上下文、所有权、来源失效拒绝和容器访问条件保持。 |
| D．Raid、地图与加载 | 玩家HUD清楚，诊断按需；地图和交互继续只消费原权限及可见性结果。 |
| E．联合收口 | 菜单、业务页、引导、错误及开发面板回归完成；全部修改合入同一最终候选。 |

逐包做必要专项检查，最后做完整构建和CTest；App类布局若变化，重建全部受影响目标。新增检查验证实际文字／数字、图片比例、控件遮挡、目标身份、重复操作和关闭恢复，不靠删测试、弱化断言或只检查矩形存在来通过。

离屏检查覆盖修改页的中英文、三尺寸和关键子状态，并实际审图。罕见死亡、防守胜败、保存失败、旧档恢复等用隔离档案验证，保留预览供复核，不要求玩家故意丢装备、破坏真档或刷稀有事件。

正常游玩仍使用原桌面 **Project Raidline Debug**。同一清单依次检查：菜单与设置→基地建造管理／原业务页→独立详情和电池→一局Raid的HUD、搜索和地图→结果与返营→已有开发测试入口。防守等条件开放入口在自然遇到或隔离预览中补看。没有新的一批桌面入口。

构建、自动化和离屏图不能代替人类验收。批准后连续完成本范围；完成先待用户验收，不自行提交、推送或合并。交付前按项目规则再检查准确PR及合并后main双平台CI。

<details>
<summary>完整覆盖清单：本次修改、验证保留和不适用</summary>

“验证保留”只允许必要的一致性修补，不重做已验收主体。

| 当前入口／子状态 | 处理 | 边界 |
| --- | --- | --- |
| 主菜单；无档、继续失败、新档覆盖确认 | 验证保留 | 原继续／新游戏／退出及确认语义。 |
| 暂停；继续、设置、返回主菜单、退出、防守放弃 | 验证保留 | 原退出后果、暂停策略与确认。 |
| 语言、窗口及设置保存失败 | 验证保留 | 仅既有设置；不新增音量、键位等功能。 |
| 同页配装、随身容器、独立仓库、数量选择与拖拽 | 验证保留 | 原页隔离、格位、胸挂通用空间、物品图片比例。 |
| 右键菜单、悬停卡、失效目标与边缘避让 | 验证保留 | 原操作资格、关闭与返回。 |
| 场景容器、尸体、失物箱、基地地面容器 | 验证保留主体 | 原访问条件、空态、关闭及拖拽取消。 |
| 枪械、安装部件、手电、电池候选／卸出／拖换 | 本次修改 | 原事务；长列表可按需滚动／分页，不新增电池或改装能力。 |
| 近战详情 | 本次修改 | 原伤害、耐久、重量与使用规则。 |
| 诱饵详情 | 本次修改 | 原数量、重量、用途与限制，不新增投掷物。 |
| 医疗／投掷轮盘 | 验证保留 | 八分区、分页、背景、松键选择与后续确认。 |
| 供应回收、改装、医疗、工坊、宿舍、资源分配 | 验证保留 | 全部原业务及空态、长列表、禁用、错误、确认、返回。 |
| 出击、失物寻回、区域行动、对局结果 | 验证保留 | 原风险、情报、分页、历史数据、保存重试及返营不重复结算。 |
| 基地日常HUD、建家选址确认、场景状态 | 本次修改 | 原时间、威胁、资源、人口与资格；不改变建家条件。 |
| 建设购买／已有目录、分页、项目取消／完成／阻挡 | 本次修改 | 购买入储备、放置、建设和即时服务分别显示真实状态。 |
| 基地运转筛选、分页、点击定位 | 本次修改 | 原全部／需处理／进行中／就绪和相机定位。 |
| 设施检查、等级、工作人员、任务、右键快捷操作 | 本次修改 | 包含居民区、岗位和资源区域子菜单，原命令裁决。 |
| 设施安装、移动、收回、回收及合法性预览 | 本次修改 | 原价格、阻挡、建筑合法性、相机平移缩放不变。 |
| 厨房与净水相关旧页 | 本次修改 | 独立条件开放入口；新档默认inactive，不新增烹饪／净水玩法。 |
| 工事购买、储备、防御位、放置／移动／收回／维修 | 本次修改 | 原安保抵扣、预警期间限制与所有权保持。 |
| 围攻排队、预警、倒计时、关闭重开、防守选择 | 本次修改 | 原自动／亲自选择、世界时间、暂停和放弃规则。 |
| 防守HUD、胜败、公共损失、检查点失败重试 | 本次修改 | 原消耗、结算、损失和幂等。 |
| Raid玩家HUD、近战／投掷／手电、动作和危机 | 本次修改 | 必要信息保留；Flow／Settlement／AI等诊断走原开发入口。 |
| 门、拾取／搜尸、控制点、救援、撤离及信号提示 | 本次修改 | 目标名、按键、持续进度和阻挡原因来自有效原查询。 |
| 室内／外战术地图与无情报／未知状态 | 本次修改显示 | 原地图投影、权限、出口信息、世界推进及移动减速。 |
| 基地所在区域地图 | 本次修改显示 | 原玩家／设施状态和仓库连接说明；不加导航或传送。 |
| 出击加载、阶段、进度与失败 | 本次修改显示 | 不伪造进度完成，不改变部署事务。 |
| 首局引导、H隐藏、跳过、完成通知、确认和错误 | 本次修改／已完成部分保留 | 不盖操作，不重复结果页引导，原保存与推进语义。 |
| F8战斗测试场、F9性能、F10运行时开发面板 | 本次修改 | 原能力、暂停、参数含义、真实档案隔离及恢复不变。 |
| 非Alpha的旧库存／仓库／结果路径 | 兼容回归 | 不扩展、不强删、不新增玩家入口。 |
| 多档管理、新设置、未实现系统 | 不适用 | 没有完整现有功能，本次不为未来玩法预造界面。 |

完整核查入口和证据见支持目录的ui-inventory-evidence.md与策划原始回复。覆盖必须包括每一类的空、满、长、禁用、确认、失败和多层窗口等实际适用状态；不适用者记录理由。

</details>

<details>
<summary>具体文件范围、合同、验证与回滚</summary>

当前核查过的实际文件和拟定操作：

| 文件／位置 | 计划操作与理由 |
| --- | --- |
| src/app.cpp、src/app.h | 整理现有UI分发、对应输入与呈现状态，收口管理、HUD、地图、加载及开发面板。共享接缝由主控单写者整合。 |
| src/app_ui_foundation.cpp、src/ui_foundation.h/.cpp | 复用既有主题、按钮、窗口映射和布局；仅增有实际消费者的辅助，不引入框架。 |
| src/ui_text_renderer.h/.cpp | 复用测量、换行、裁切与翻译接口，原则上不重写字体后端。 |
| src/ui_localization.h/.cpp | 补充实际缺失的短中文及对应英文；不改内部ID或把源码英文全局替换。 |
| src/app_base_defense.cpp、src/app_base_fortification.cpp | 仅分离／整理UI绘制及对应客户端输入，防守、工事业务和时间规则不变。 |
| src/app_melee.cpp、src/app_throwable.cpp | 仅详情与HUD显示，攻击、投掷和模拟算法不改。 |
| src/app_developer_range.cpp、现有developer_range_panel布局 | 控件、长文与反馈一致，既有发放／刷新／清理／退出命令不变。 |
| src/first_raid_ui.cpp | 引导排版、避让和显示层级，保留H隐藏、跳过和原保存规则。 |
| 现有app_ui_*、ui_*_presentation | 已验收页面只在实际不一致或异常态缺口处最小修补，其余回归保留。 |
| 拟新增ui_base_management_presentation、ui_base_defense_presentation、ui_item_details_presentation、ui_raid_hud_presentation的.h/.cpp | 对应本次四类实际消费者的只读显示模型与布局；不建立全能UI系统，不把业务搬入App。 |
| 按实际拆分拟新增对应app_ui_*.cpp | 承接从旧文件拆出的绘制与客户端接缝，保持原入口和结果；不为拆文件额外重构业务。 |
| 现有相关tests；拟新增上述四类UI回归及test_ui_final_consolidation.cpp | 保留全部原2594注册测试与业务断言，新增有意义的显示／输入／身份与全流程覆盖。位置断言应指向真实绘制，不能弱化检查。 |
| CMakeLists.txt | 仅登记实际新增客户端文件及测试，不新增依赖。 |
| 本计划、CURRENT_STATE；实施后拟新增doc/acceptance/ui-final-consolidation-v1.md | 保存同一任务进度、唯一候选和一份简洁联合清单。 |

只读显示仍消费现有GameSession查询和投影；显示禁用不代表领域授权。所有提交继续走原命令，资产所有权、格位、价格、扣款、时间、建设、岗位、日结、愿望、寻回、防守、撤离、损失、保存和异常恢复不改。

准星窗口锚定、相机与坐标变换、地图权限和视野遮挡、尸体生命周期、射击和战斗模拟不改。地图可整理标题图例和面板，不能泄露隐藏目标。各类模态页、地图、围攻预警和开发面板维持原暂停／移动规则，不统一冻结世界。

只缓存显示布局和确实不变的内容；动态时间、可见性、交互资格必须使用当前有效查询。不得因缓存或每帧资产／敌群扫描引入过期权限和额外游戏计算。

资源只复用已批准图片和代码回退；不生产正式美术、音效、字体包，不改Manifest、存档结构、内容定义、信任、权限、模型、外部GDD或全局配置。重大范围变化需另行说明，不静默扩张。

验证安排：Windows Debug按项目VS x64／UTF-8环境全目标构建；各工作包专项后完整CTest；原五项3ms不改计时、负载或阈值。中英文三尺寸实际离屏绘制检查文字数字、长文、比例与遮挡；交互检查包括按下移出、失焦、翻页／目标失效、连按Enter、关闭恢复及穿透。保存、失败、旧档、围攻和恢复用隔离档案，绝不读取或破坏真实存档。未实施前不运行这些检查，不把基线CI当成新任务通过。

获批后从届时已核查的accepted origin/main建立codex/ui-final-consolidation-v1；当前参考基线是main5f53496。保留原程序检查点与唯一桌面入口，不生成多个测试入口。实施完成先冻结并待人工验收，未另获授权前不提交／推送／合并。

回滚只撤本任务的客户端、文本、测试、构建登记和任务记录，恢复原程序检查点，保留其他修改和真实存档。没有存档迁移，不通过回滚撤销用户合法业务结果，不整树reset／clean。

</details>

<details>
<summary>状态、实际授权与策划通信证据</summary>

- task_id：ui-final-consolidation；plan_version：RL-UI-FINAL-CONSOLIDATION-v1；state：awaiting_plan_approval；pending_request：无。
- planning_source：2026-10-24主控直接人类原文“请协调策划线程，让他给出一次性完成剩余所有UI的优化方案，这次优化UI之后就不再继续优化UI”。这是准备整体方案及确定收官原则的授权，尚无实施批准。
- approval_source／implementation_approval_source：无；candidate_id／accepted_candidate_id／acceptance_source：无本任务候选或验收。
- baseline：main 5f534963e13bb42dfd154e45cf243e279978342a; tree eba658139eebe8b5435e07652bc5c71657ec973b; accepted runtime RL-UI-RAID-RESULT-v1-local-fc00d531a705；实际开发目录E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1；当前分支codex/ui-raid-result-v1、HEAD 7694e68426a5d42ff6695545f133e949a9dbbbc1；HEAD与main同树。当前未创建新分支。
- 已核查PR191已普通合并；main CI39981087159准确SHA5f534963e13bb42dfd154e45cf243e279978342a为success，双平台2594/2594及原五项3ms为前序已交付证据。
- 有效策划结果：request_id MAIN-UI-FINAL-CONSOLIDATION-20261024-01；task_id ui-final-consolidation；version RL-UI-FINAL-CONSOLIDATION-v1；baseline main 5f534963e13bb42dfd154e45cf243e279978342a; tree eba658139eebe8b5435e07652bc5c71657ec973b; accepted runtime RL-UI-RAID-RESULT-v1-local-fc00d531a705。目标019feff4-477c-75c2-bf31-3f09ff1e3891、host local；turn 01a16bbb-e019-7e12-8155-2d8fee35af7b；message msg_09394c523a164a07016adc18a6264c8191bf58d247c16a57b5；role planner；status proposed_readonly。已读完整原文并核对四元组，不以发送回执或建议代替批准。
- 当前仅只读核查代码与已交付资料，修改本计划和当前状态两类记录；没有启动游戏、实际窗口视觉审查、构建／测试、新候选、资源生产或Git交付。视觉观感和硬件效果尚待实施后验证。
- 来源证据：C:\Users\Administrator\Documents\美术资源生成\tmp\ui-final-consolidation-v1-20261024；before.json、五工作区原修改备份、ui-inventory-evidence.md、planner-request.txt、原生发送／完整回复、planner-result.json及audit.json。代码与资源保护通过对应哈希核对，规划不改游戏。

</details>


</details>


</details>


</details>

</details>

</details>

</details>
