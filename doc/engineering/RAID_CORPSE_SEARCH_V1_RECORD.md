# 边境交换站搜尸：验收与交付记录

## 当前交付

用户于2026-10-13在主控线程明确：“边境交换站搜尸中文修正版验收通过，请完成提交与合并。” 已验收候选为`RL-RAID-CORPSE-SEARCH-v1-zh1-local-03f42d4333a9`，源码及程序hash核对未变。

本次范围为清单23个代码/测试/CMake路径及四份任务文档。其他未提交的指针、配置和报告模板修改保留。交付沿用现有origin普通合并策略，分别核对提交、推送、PR准确head双平台CI、合并树和合并提交CI。没有下一项开发授权。

本地专项104/104、完整CTest1924/1924已经通过，基线main的CI38145513346最终success。远端交付尚在执行；本记录提交在main可达且合并CI通过时为已交付。以下开发记录中的“待复验”属于验收前历史，不覆盖本节。

## 中文修正版开发记录（历史）

## 当前候选与授权

当前状态：**待用户复验，候选冻结，未交付**。搜尸方案为 RL-RAID-CORPSE-SEARCH-v1；中文修正方案为 RL-UI-ZH-CONSISTENCY-v1，用户于2026-10-13在主控线程收到该版本方案后明确“同意实施”。尚无本候选的人工验收。

| 项目 | 核对值 |
| --- | --- |
| 当前候选 | `RL-RAID-CORPSE-SEARCH-v1-zh1-local-03f42d4333a9` |
| 代码基线 / HEAD | `f6a288b4da7579814b69a682dde9652812543528` |
| 开发分支 | `codex/raid-corpse-search-v1`，index 为空 |
| 源码清单 | 23 个源码/测试/CMake 路径，支持目录 `tmp/raidline-ui-zh-v1-20261013/candidate.json` |
| 清单 SHA256 | `03f42d4333a99f21dcd3f48f84bccb6346db55dc5a13e0f6ac4adbfed8e243f5` |
| Debug 程序 SHA256 | `f2ab05438f5c82ae69f6c4b7ec589d202831bdde0229961f7691b94cc3d153f1` |

Debug 程序位于 `E:\WorkPlace\Projects\C\Project Raidline-collaboration-delivery-20261011\build\windows-debug\Project_Raidline.exe`。桌面快捷方式使用这个程序及其所在工作目录，资源和 DLL 保留在旁边。原候选的21路径清单没有改写，程序备份为支持目录 `previous-candidate.exe`；它仅用于追溯和回滚。

## 实际修改与影响

| 路径 | 修改目的 |
| --- | --- |
| `src/ui_localization.cpp` | 新增23个完整词条/片段，并把医疗设施等长名称放在短前缀前；地图和区域同一地点沿用既有中文名 |
| `src/ui_localization.h`、上述 cpp | 增加有实际调用者的 UI 短标签函数：先翻译，再按近似显示列数截取，保留完整 UTF-8 字符 |
| `src/app.cpp` | 尸体标题按真实资产位置类型显示；格子标题明确设亮色、提示移至y=92；三个短物品名称调用新的截取函数 |
| `tests/test_ui_localization.cpp` | 5项新增回归覆盖组合名称、前缀替换、英文保留、数量/按键及 UTF-8 截取 |
| `tests/test_raid_scene_container_ui.cpp` | 1项新增实际App离屏回归，对照尸体主标题和格子标题的字形；渲染前后Profile指纹一致；可按任务环境变量导出QA图 |
| 当前任务、状态和验收说明 | 更新真实授权、新候选和中文操作名称 |

`src/app.cpp` 和 UI 头文件是实现时必要的传播：图像检查发现原格子标题继承暗色网格笔色，提示行与标题过近；短名称又在翻译之前截断英文。修改只影响文字投影和间距，格子/按钮点击区域、输入捕获与业务命令不变。没有改变内容定义或增添正式资源。

四项App输入/捕获函数与已验收基线逐字一致；camera/aim/relocate/Base指针、通用敌人/命中、尸体领域/所有权和内容文件继续保留。schema52、边境交换站rules30、content64不变。原游戏checkout的旧分支、HEAD、代码和其他脏文件没有同步或重置，只有本任务四份文档更新。

## 实际验证

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| Windows Debug 全目标增量构建 | 成功；UI头文件的全部消费者已重建 | `build.log` |
| 中文、尸体面板、菜单、准星、相机和瞄准专项 | 104/104，23.40秒 | `focused.log` |
| 最终完整CTest | 1924/1924，141.05秒 | `full.log` |
| 内容名称与地图说明静态核查 | 177项；24处候选遗漏已消除，部分为同名重复项 | `audit-after.json` |
| 尸体面板离屏图像 | 标题和短物品名称已目视核对；没有真实游戏窗口 | `build/windows-debug/ui-zh-qa/corpse-zh.bmp` |
| 工作区和已验收合同保护 | 原checkout代码、其他脏文件、branch/HEAD/index、指针函数均保留 | `preservation.json` |

除QA图所在构建目录外，上表日志/JSON均在支持工作区 `tmp/raidline-ui-zh-v1-20261013`。静态核查不是逐屏截图，不宣称所有领域提示和任何字密度都已验证；真实游玩手感仍待用户确认。当前候选未提交，无准确提交的远端Windows/Ubuntu CI。

实施过程证据没有覆盖写成通过：新组合名称测试先复现了4项漏译；第一轮专项仍有1项医疗设施组合失败，修正了替换先后。图像断言最初的像素颜色条件与生产笔色不一致，纠正后检查实际中文标题；中间1923项全量通过后，图像又发现英文先截断问题，因此补上短标签函数并对最终1924项重新完整验证。阶段日志和此前QA图均保留，不作为最终候选的结果。

## 回滚与等待边界

中文修正涉及的五个源码/测试文件都有维护前备份；新增UI头文件声明前另外核对了它仍与各自HEAD一致，再保存基线。回滚这一修正应只恢复这些文件及相应旧构建，保留原搜尸实现和其他工作。旧搜尸候选和所有历史授权仍可追溯。

原搜尸存档已经为schema52，不能用旧schema程序强写新图；这项既有约束不因中文修正改变。开发代理没有操作真实存档。等待验收期间不改当前候选、不启动下一任务、不提交或合并。

## 之前的批准方案与检查记录（历史）

下面保留中文修正前的记录，包括当时的待批准状态、旧候选与1918项检查。它们不覆盖本页开头的当前状态。

## 原搜尸候选开发与审批记录

这页供开发接管和核查证据。

当前任务的简要说明见 [任务页](../exec-plans/active/raid-corpse-search-v1.md)，实际试玩按 [验收清单](../acceptance/raid-corpse-search-v1.md) 操作。

下面先记录当前身份和授权，再记录已经完成的实现与检查。标为“原方案”的部分保留当时的范围和设计约束；其中“拟新增”“获批后”描述的是实施前计划，不要求重新开发。

## 2026-10-13 中文文本检查与待批准方案

当前正等待 **RL-UI-ZH-CONSISTENCY-v1** 修复方案的批准。原搜尸候选仍冻结、未验收；本节更新当前状态，后面的身份/审批记录是原候选的历史证据。

### 检查结论

- `src/ui_localization.cpp` 已有“Frontier Exchange → 边境交换站”“Hospital District → 医院片区”，中文模式的地图名称会使用这两个译名。`UiTextRenderer::render` 调用同一翻译入口；新设置默认中文，已保存的英文选择仍会保留。没有读取或修改真实用户设置。
- `src/app.cpp` 的尸体容器标题组合为 `{} | COMPARTMENT {}`。当前翻译表只翻译 CORPSE，因此中文投影会留下“尸体 | COMPARTMENT 1”。这个组合来自实际显示路径，不是新增设计。
- 区域路线与基地设施页同样把内容名称送到文字绘制入口。静态核查 177 个名称/地图说明字段，发现 24 处可能保留英文；其中有同名重复项，不能当成 24 个独立 Bug。示例包括“灰线场站 Relay”和“工业紧固件 Service Spur”，后者还把道路名称误当作物品名翻译。
- 现有 `UiLocalizationTest.exe` 的 **60/60** 项通过（本轮直接运行，未启动游戏）；现有测试没有覆盖上述全部组合，不能据此声称所有界面已完整中文化。

### 证据和限制

静态核查按当时 C++ 的“先精确匹配，再顺序替换片段”规则执行，结果在支持目录 `tmp/raidline-chinese-text-audit-20261013/audit.json`。这不是实际窗口截图，也未逐屏验证裁切或所有领域提示。后续实施范围、检查结果和状态见 [当前任务说明](../exec-plans/active/raid-corpse-search-v1.md)。

本轮只调整文档、指令及任务状态；没有修改游戏源码/测试/内容、重建程序或启动游戏。原 21 路径源码及程序哈希核对不变，branch/HEAD/index 与其他未提交文件保留。

## 本文用语

- 资产：游戏里的具体物品。
- 资产树：容器及其中嵌套的物品。
- 高水位：分配新物品编号时记录的上限。
- 幂等：重复执行也不会重复发放或结算。

## 版本、授权和验收状态

### 任务标识（task_id）

`raid-corpse-search-v1`

### 当前状态（state）

待用户验收；已批准版本实施与本地自动化完成，候选冻结；不提交/推送/PR/合并。

### 批准版本（plan_version）

`RL-RAID-CORPSE-SEARCH-v1`

### 依赖基线及其 CI（baseline）

已验收并普通合并的 main `f6a288b4da7579814b69a682dde9652812543528`；鼠标交付 PR #169 / head `8fad7d297de07c2ea886c10a4c338cdc1d9acba1`，准确 head Windows/Ubuntu 各 1898/1898。

合并树与该 head 相同。

合并提交自身 CI `38145513346` 首轮 Windows 两项旧 FirstRaidLifecycleTest 临时保存失败（其余1896通过）、Ubuntu成功；失败job重跑第2次已完成 success；首次失败仍保留诊断日志，不将其抹成通过。

PR准确head与合并树相同且此前双平台通过；不改旧测试或绕过检查。

### 真实用户授权（approval_source）

2026-10-12 主控线程真实用户明确“同意 RL-RAID-CORPSE-SEARCH-v1，请按方案开发，保留已验收的窗口锚定和菜单行为，完成后提供验收清单，暂不提交或合并。”；授权本版本实施、构建与回归，不授权 Git 交付。

此前2026-10-11 主控真实用户“窗口锚定 v2 验收通过，请完成提交与合并，启动下一项开发。”授权上一候选交付并要求下一项；按已部署三线程规则，先选题和提交具体版本方案，不据此补足本方案批准。

### 冻结候选与程序指纹（candidate_id）

`RL-RAID-CORPSE-SEARCH-v1-local-2b7c77f6d58b`，21 路径源码/测试/CMake SHA256 清单见支持工作区 `tmp/raid-corpse-search-20261012/candidate.json`，清单摘要 `2b7c77f6d58b7473244b27a62ce46a55f5e4d7b028a9522911690e3625aee479`。

构建 `E:\WorkPlace\Projects\C\Project Raidline-collaboration-delivery-20261011\build\windows-debug\Project_Raidline.exe`，SHA256 `3767cf6fc19e799add8bd0c1bf22b2d073ba24a045b0aebf9eb2874763243a6a`。

### 人工验收（accepted_candidate_id）

无；尚无本候选的用户正常游玩验收。

### 策划协调来源（planner_source）

`RL-POINTER-V2-20261011-NEXT-01` / task `pointer-aim-continuity-v1` / version `RL-POINTER-WINDOW-ANCHOR-v2` / baseline `8fad7d297de07c2ea886c10a4c338cdc1d9acba1`，策划线程 `019feff4-477c-75c2-bf31-3f09ff1e3891` 正式回复 `next_task_proposed_readonly`。

合并后 `RL-POINTER-V2-20261011-DELIVER-02` 同 task/version / baseline `f6a288b4da7579814b69a682dde9652812543528` 有效回复 `delivery_confirmed_next_proposal_retained`，确认同一试点建议及独立尸体根无产品冲突。

两次四元组与目标均核对，策划不是实施审批者。

### 待收协调请求（pending_request）

无策划待收项；用户已批准上述版本。外部 GDD 保持只读，不将其候选设定改成长期已确认设定。

## 实际实施入口

从上述已接受 main 独立分支 `codex/raid-corpse-search-v1` 开发，复用干净游戏 worktree `E:\WorkPlace\Projects\C\Project Raidline-collaboration-delivery-20261011`；原游戏目录仍保留旧鼠标分支/HEAD及全部未提交成果，仅本任务/状态记录同步。

新构建入口为该开发 worktree 的 `build/windows-debug/Project_Raidline.exe`，不能用原目录旧程序验收尸体。

## 2026-10-13 验收前反馈与 Debug 入口优化

用户直接要求简化开发计划/建议/修改反馈，并提供根目录或桌面的 Debug 入口；这是本次指令/入口维护授权，尚未验收尸体玩法。

两份实际 checkout 的根 AGENTS 与交付报告参考更新为简短结论、必要影响和下一步，技术证据保留本任务记录；审批、验收与 Git 门槛不变。

桌面快捷方式 `C:\Users\Administrator\Desktop\Project Raidline Debug.lnk` 指向本任务开发 worktree 的 `build/windows-debug/Project_Raidline.exe`，工作目录为该程序所在目录，资源/DLL仍在构建目录。

仅创建并读回核对快捷方式，没有启动程序。

以后候选入口变化时，在报验前核对并更新此快捷方式，避免打开旧构建。

本次仅核查指令格式/引用、快捷方式目标/工作目录及现有文件；21 路径候选源码和程序 SHA256 全部与原冻结清单相同，候选 ID 不变，原构建/CTest仍为2026-10-12的证据，没有重新运行或冒充本次新增测试。

其他未提交文件、branch/HEAD/index保留。

未提交/推送/PR/合并，未触碰游戏代码、资源、真实存档或信任/权限/模型，继续等待本候选正常游玩验收。

本次快照/核查见支持目录 `tmp/raidline-feedback-debug-20261013`。

## 实际完成与验证（2026-10-12）

候选 `RL-RAID-CORPSE-SEARCH-v1-local-2b7c77f6d58b` 在上述独立分支从 main `f6a288b4da7579814b69a682dde9652812543528` 完成，HEAD仍为基线，index为空。

原游戏目录的分支/HEAD/index、代码、其他未提交文件逐字节保留，仅本任务与CURRENT_STATE记录同步；支持目录原补丁/资源未操作。

没有本片commit/push/PR/merge、没有本片远端CI，未启动游戏或操作真实用户存档。

实际行为：仅三个既有护甲资源守卫绑定一份原有 ordinary Loot。

Deploy冻结后存活根不可见、不可访问；现有死亡值事实一次开放最终中心位置，确认后清有限队列，重复事实不移动/增发。

独立尸体根一直保留至本局终结，搜空仍可放回。

F和既有格子命令/访问失效继续消费领域结果，不让尸体进入演员、弹道目标、阻挡或导航。

尸体内容不参与剥甲，压力波次没有来源绑定或新增收益。

schema默认读写升为52，当前发布的Frontier新Raid为rules30；不启用该试点的其他地图/取消护甲能力的对照内容沿用既有规则。

content仍64，core.json和正式manifest均未改。

旧schema/pending快照没有尸体字段，不补生成；向51及更早写新根/来源明确拒绝。

三类终局和异常回滚沿用原合同，余物只随尸体树清理。

与路径表的必要传播：`src/ui_localization.cpp` 增加尸体双语标签；`tests/test_armored_infected.cpp` 与新共享 `tests/raid_corpse_legacy_fixture.h` 保持禁用护甲对照及旧schema夹具，恢复原Loot地面来源，而不降低新数据校验。

护甲能力未声明时不启用试点；发布内容仍恰好三名守卫。

没有新玩法或资产成本扩张。

- Windows Debug重新配置、完整全目标构建成功；新增类型/布局的全部消费者已重建。

  构建日志 `build/windows-debug/corpse-configure.log`、`corpse-build-full.log`、`corpse-build-regression.log`。

  本片whitespace检查通过，CMake层间/唯一编译归属配置检查通过。
- 定向 **261/261**（44.17秒），日志 `corpse-focused-final.log`；最终完整CTest **1918/1918**（111.93秒），日志 `corpse-full.log`；净新增 **20** 项（15项领域/运行时、5项实际App/SDL尸体路径）。

  首次20项回归中的2项非法DefinitionId夹具、随后旧schema/禁用护甲对照失败均已修正并通过，不当成最终结果。

  初始编译的布尔指纹类型已修正。
- 真实App事件覆盖F入口、指针释放/重新捕获、快速取出/放回、中英文字像素、受控/离距/切空间撤销拖拽、持续模拟、空间往返后的内容/位置不变。

  用隐藏dummy SDL与离屏软件渲染，没有App.run或真实游戏窗口。

  菜单/建筑窗口锚定回归及现有Raid、Base、Defense性能与保存检查均包含于最终完整CTest。
- 四项App核心函数 `makeGameplayInput`、`makeBaseGameplayInput`、`shouldCaptureWorldPointer`、`syncRaidPointerCapture` 与验收基线代码一致。

  camera/aim/relocate/Base指针源码、通用EnemyLifecycle和HitResolution保持原样，资产内容文件未变。
- 使用保留基线的领域库和最终候选领域库分别运行同一个只读部署审计程序；seed **1/2/7/128/9917** 的完整Loot ID/定义/数量/原位置/来源及敌人位置/大小/HP/护甲结果完全相同。

  证据在支持目录 `loot-baseline.json`、`loot-candidate.json`、`loot_audit.cpp`；只改变三份所有权来源，不重抽随机结果，不增总奖励。
- 依赖main CI `38145513346` 第2次重跑success，独立于本片未提交证据。首次Windows旧临时保存失败、本地基线7/7以及重跑事实均保留；未修改旧FirstRaidLifecycle测试，不宣称首次失败根因已穷尽。

尚未验证：真实鼠标设备手感、尸体占位/提示的实际可读性以及正常游玩节奏；由用户使用上方本次开发worktree的Debug程序按下方统一清单验收。不要使用原游戏目录的旧Debug程序测试本候选。本阶段不扩张试点或开始下一任务。

## 已批准范围（原方案，2026-10-12）

在 Frontier Exchange 击杀三个既有资源守卫组的护甲成员后，玩家可以靠近搜尸，使用现有格子界面决定取出、放回或交换哪些物资，成功撤离后只带回实际携带物。尸体仍在战斗现场，不直接向仓库发奖励。

1. 只覆盖 `encounter.frontier.resource_guard` 三组中已存在的护甲成员（当前 Deploy 为每组首成员配置护甲），总计最多三具可搜索尸体。不增加敌人数量、敌人类型、生命/护甲/AI/射击参数或地图。
2. 每具绑定 **一份既有普通 Loot 结果**。

   Deploy 在原随机生成完成后，按守卫组稳定实例 ID 顺序，从 Frontier 户外 ordinary 资源点中选择离该守卫组最近、尚未使用的一份常规结果；距离相同时按原 Loot 稳定资产 ID 决定。

   不消耗新随机流，不改变物品 ID、定义、数量或状态，不增加总奖励。

   排除高危、高价值、地标专属、已有室内箱及保障性/救援/寻回物资；不足时整次候选拒绝，不替代抽取或增发。
3. 未击杀时对应物资不作为可拾取地面物或可访问尸体出现；死亡只开放该已冻结资产树。保留原 Loot 来源审计，关闭界面、进出建筑和重复命中均不重新生成内容。
4. F 在既有 72 世界单位范围内打开；复用格子取出、放回、旋转、堆叠、交换及快捷转移。世界继续运行，受控、离距、切空间、死亡或结束 Raid 时关闭并清理拖拽；原门口/恢复缓存/动作门禁优先级不变。
5. 尸体根不可携带，不攻击、不作为弹道目标、碰撞或导航阻挡。三具尸体（包括搜空或玩家放回物品的尸体）保留到本局终结；显示“可搜索/已搜空”双语提示，空尸体仍可放回物品。数量固定有界，不做时间清理。
6. 成功只返回携带树；死亡/主动退出只按原合同记录玩家携带树，尸体余物不进入玩家失物记录；异常退出恢复准确出击前 Registry，不能留下尸体资产或新高水位副作用。重复终局不再结算。

不做其他敌人/压力波次掉落、敌人真实装备消耗、护甲剥取、玩家尸体或寻回替换、搜索计时、搬运/破坏/腐败/肢解、跨局尸体、战斗小组、新枪械、新地图、AI 或导航扩张。本片不是所有尸体规则的最终设计。

## 实施前调查与约束（原方案）

- `EnemyLifecycle::removeDead` 已返回按稳定 `CombatTargetId` 标识的 `EnemyRemovalFact`，删除演员和附属状态；`WorldShootingAdvance.removals` 已将该事实传给 GameplayWorld。

  目前世界只累计击杀分数。

  复用此值事实，通用命中/死亡算法不生成资产或奖励，Base/Defense 行为保持原样。
- `RaidEnemySnapshot` 尚无可持久化的尸体来源身份，当前 runtime 导入时分配 CombatTargetId。

  为三名试点加入稳定 `source_enemy_id`（基于被冻结的 encounter 实例与成员身份），导入时绑定现有 CombatTargetId；不得把 vector 下标/对象地址作为长效绑定，不重写通用身份分配。
- 现有 `RaidSceneContainerAssetLocation` 校验只允许两个指定室内的固定坐标，并要求每空间一个根、每根恰好两个冻结 Loot 来源；不能放宽该合同来塞入动态尸体。
- 新增独立 `RaidCorpseContainerAssetLocation` 权威根类型，保存本局/空间/来源敌人及可选死亡位置；存活时无死亡位置、不可见/不可访问，首次合法死亡事实一次写入最终位置。

  复用既有箱格子定义作为容器容量，UI 按权威尸体类型显示名称，不冒充可携带护甲或敌人真实背包装备。
- 用窄的 SDL-free `raid_corpse_search` 领域函数完成冻结绑定、死亡开放、合法性/范围查询及终局资产收束；GameSession 只组合调用，App 只提交库存命令和绘制投影。

  死亡开放失败时保留至成功确认的有限事实，重复消费幂等，不丢失已移除敌人的最终位置。
- 库存查询与提交共享访问凭据并重验同一根、空间、距离、活动与控制状态；拒绝时所有参与者、revision、物品数量和高水位均不变。复用格子操作机制，但保留固定箱和尸体各自访问/来源校验。
- 数据合同有实际变化：计划 **schema 51 → 52、Raid rules 29 → 30，content 保持 64**。

  新快照记录三个绑定、资产根与 Loot 来源；旧 schema/pending Raid 迁移为无尸体绑定，不补尸体、不重抽。

  向旧 schema 写入新位置或绑定必须拒绝。

  不增加运行中 Raid 续玩能力，不写用户存档做试验。

## 核查过的文件与当时拟操作

以下路径相对于游戏仓库 `E:\WorkPlace\Projects\C\Project Raidline`。拟新增路径已确认不存在；现有代码按所列合同修改，未列路径不预先扩张。

| 路径 | 操作 | 目的 |
| --- | --- | --- |
| `src/raid_corpse_search.h`、`src/raid_corpse_search.cpp` | 拟新增 | 有界尸体领域命令/查询，冻结、开放、访问与收束 |
| `src/profile_state.h`、`src/profile_state.cpp` | 修改 | 独立尸体根、稳定来源绑定、资产图/来源校验和指纹 |
| `src/raid_lifecycle.cpp`、`src/raid_lifecycle.h` | 修改 | 三份既有 ordinary Loot 划转、版本化新快照、终局/异常回滚，不改变随机产物 |
| `src/gameplay_world.cpp`、`src/gameplay_world.h` | 修改 | 导入试点来源→CombatTargetId 映射、消费现有死亡值事实及有界确认，不保存演员引用 |
| `src/game_session.cpp`、`src/game_session.h` | 修改 | 组合尸体事务/只读投影，与固定箱共用库存入口，保留访问失效与优先级 |
| `src/inventory_domain.cpp`、`src/inventory_domain.h` | 修改 | 同一库存机制接纳合法尸体凭据，尸体根不可转移，原固定箱合同不放宽 |
| `src/app.cpp`、`src/app.h` | 修改 | 尸体代码占位、双语提示与格子入口；保留窗口锚定/菜单捕获行为 |
| `src/save_repository.cpp`、`src/save_repository.h` | 修改 | schema 52 读写/显式旧档迁移与拒绝非法序列化 |
| `tests/test_raid_corpse_search.cpp` | 拟新增 | 成功、重复、拒绝、普通预算守恒和稳定身份绑定回归 |
| `tests/test_raid_lifecycle.cpp`、`tests/test_profile_state.cpp`、`tests/test_save_repository.cpp` | 修改 | Deploy/三终局/异常回滚、来源审计、旧档/损坏/备份回归 |
| `tests/test_raid_scene_containers.cpp`、`tests/test_raid_scene_container_ui.cpp`、`tests/test_pointer_continuity_ui.cpp` | 修改 | 共用库存与真实 F 入口、失效、持续战斗、菜单/准星回归 |
| `CMakeLists.txt`、本计划、`doc/project/CURRENT_STATE.md` | 修改 | 编译与测试注册，真实批准、候选和验证记录 |

`src/enemy_lifecycle.h`、`src/world_shooting_runtime.*`、`src/hit_resolution.cpp` 已核查，现有死亡事实足够，本方案不修改它们。

`assets/content/v1/core.json` 已核查三组/普通预算，暂不修改内容或资产定义；不创建 manifest 或正式资源文件。

## 当时的资源与验证安排

无需正式美术或新音频。使用现有代码几何与文字占位，不制作尸体贴图/死亡动画，不调用美术生产。外部 GDD、资产 catalog 及正式 manifest 均不修改。

获批后 Windows Debug 全目标构建（公共 Profile/输入/世界布局的消费者全部重建），定向 domain/Deploy/save/App 与固定箱/指针回归，再跑完整注册 CTest、CMake SDL-free 与 whitespace 检查。

多 seed 比较 ordinary Loot 的 ID/定义/数量/随机结果与总预算，只允许三份来源位置变更；同一初始敌人数量/生成位置/护甲不变。

检查零/重复/未知绑定、重复死亡、旧字段缺失、满包/过期凭据、失败事务、空尸体放回、三种终局、异常恢复、重复结算及现有性能约束。

当时方案要求统一进行正常游玩验收。具体操作已展开为 [试玩验收清单](../acceptance/raid-corpse-search-v1.md)，验收内容没有删除。

## 当时的实施顺序与回滚方式

明确批准 `RL-RAID-CORPSE-SEARCH-v1` 后，再核对最新 `origin/main` 与其 CI/差异；从已接受依赖建立 `codex/raid-corpse-search-v1`，保留原工作区全部未提交成果，不从旧鼠标在制分支叠加开发。

按“冻结绑定/唯一根及校验 → 死亡开放和终局 → 库存/F/占位 → 完整自动化和候选”完成一个可玩闭环，不拆成多个半成品人工验收。本片获批实施不自动授权验收前 commit/push/PR；在用户正常游玩验收前保持未提交候选。不得自行启动游戏。

未提交时只还原本片明确路径差异及新文件，保留其他修改。交付后的代码可按独立提交回退；schema 52 写出的存档不能用旧程序无损倒读，回滚前保留原档及安全备份，不用旧 schema 强写新图。不会为回滚触碰真实用户存档。

- [x] 上一任务 v2 验收、准确提交 CI、普通合并及树核对
- [x] 策划单一建议、合并后有效确认及真实代码调查
- [x] 用户明确批准本版本
- [x] 代码实施与自动化、冻结候选
- [ ] 用户正常游玩验收及另行交付
