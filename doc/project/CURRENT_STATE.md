# 当前：物品菜单与悬浮信息 v1 已验收，交付中

用户于2026-10-19验收候选 **2f83b46cdd4d**，明确要求提交、推送和合并。游戏程序和固定桌面Debug入口保持已验收版本。

- `task_id=ui-item-context`；`plan_version=RL-UI-ITEM-CONTEXT-v1`；`state=accepted_delivery_in_progress`。
- `candidate_id`与`accepted_candidate_id`均为`RL-UI-ITEM-CONTEXT-v1-local-2f83b46cdd4d`；人类验收原文：“验收通过，请提交推送合并”。
- 交付冻结候选12项源码／测试／CMake及3份本任务文档；其他未提交文件继续保留。原本地完整回归2208/2208证据与验收版本一致，不重复无变化的本地C++周期。
- 新PR的准确提交双平台CI通过后普通合并，再验证合并树一致及合并提交CI；保留原3ms门槛，不绕过失败或用重跑覆盖失败历史。
- 不启动游戏、不改真实存档、信任／权限／模型，不启动下一项开发。[任务记录](../exec-plans/active/ui-item-context-v1.md)保留完整证据。

<details>
<summary>本次实施和前序记录（以文首状态为准）</summary>

# 当前：物品菜单与悬浮信息 v1，待验收

**RL-UI-ITEM-CONTEXT-v1** 已完成。候选 **2f83b46cdd4d**；继续使用桌面「Project Raidline Debug」，按[简短验收清单](../acceptance/ui-item-context-v1.md)正常游玩确认。

- `task_id=ui-item-context`；`plan_version=RL-UI-ITEM-CONTEXT-v1`；`state=waiting_human_acceptance`；`candidate_id=RL-UI-ITEM-CONTEXT-v1-local-2f83b46cdd4d`。
- 右键菜单统一标题、悬停和按压；同项按下／松开确认一次。物品悬停后只显示一张卡，保留全部原武器供弹提示。
- 原独立仓库页、图片比例、存档、玩法、准星及固定入口保留。Debug全目标构建及完整回归2208/2208通过，原3ms门槛和负载不变；三档窗口／中英文离屏检查通过。
- 分支 `codex/ui-item-context-v1`，依赖主线 `df45709`；本片仍未提交，远端CI未运行。没有推送、合并、启动游戏或操作真实玩家存档。
- 已有用户批准实施；尚无人类验收。候选已冻结，下一步等待本次反馈。[任务记录](../exec-plans/active/ui-item-context-v1.md)保留准确源码／程序指纹、实际授权与检查边界。

<details>
<summary>实施、批准及上一项交付历史</summary>

# 当前：物品右键菜单与悬浮信息统一 v1，实施中

用户已批准 **RL-UI-ITEM-CONTEXT-v1**；按[已批准方案](../exec-plans/active/ui-item-context-v1.md)实施，完成后提供候选及简短验收清单。

- `task_id=ui-item-context`；`plan_version=RL-UI-ITEM-CONTEXT-v1`；`state=implementing`；`candidate_id=无`。
- 实施在复用的库存工作目录、`codex/ui-item-context-v1` 分支，基线为已验收主线 `df45709`。原根旧分支和其他未提交内容保留。
- 保留原存档、已验收UI及玩法、桌面固定Debug入口与3ms门槛；本轮不提交、推送、合并或替用户启动游戏。

<details>
<summary>上一项交付和本次方案历史</summary>

# 当前：物品右键菜单与悬浮信息统一 v1，待批准

策划已指定下一项 **RL-UI-ITEM-CONTEXT-v1**，主控已核查实际代码并完成[具体方案](../exec-plans/active/ui-item-context-v1.md)。本轮只规划，下一步等用户批准该版本后再实施。

- `task_id=ui-item-context`；`plan_version=RL-UI-ITEM-CONTEXT-v1`；`state=waiting_plan_approval`；`approval_source=无`；`candidate_id=无`。
- 拟统一基地／突袭库存的物品右键菜单与悬浮信息卡，补足悬停、按压和边缘布局；现有武器供弹提示合并展示，物品操作权限保持。
- 拟将右键操作改为同一行内按下、松开确认一次；点外部只关闭，不穿透。其余已验收输入、玩法、独立仓库页、原存档及3ms合同保留。
- 已有“策划线程”原生回复已核验：`MAIN-UI-NEXT-PLAN-20261018-01 / ui-next-optimization / UI-NEXT-PLANNING-v1 / df45709c7cbd961e93a5e752c80277a7530262bb`。策划建议不等于实施批准。
- 本轮仅更新根目录这份状态和新方案；代码、已验收程序、固定Debug入口及其他未提交修改保持，没有构建、启动游戏、新建分支或Git交付。

## 上一项：独立仓库页 v2 已交付，CI收尾通过

候选 **f7a2af9c01c2** 已验收并完成Git交付与CI收尾。桌面 **Project Raidline Debug** 保持原已验收程序。

- `task_id=ui-inventory-presentation`；`plan_version=RL-UI-INVENTORY-PRESENTATION-v2`；`state=delivered`。实际人类验收原文保留在任务记录。
- [功能PR178](https://github.com/Disthinker/Project-Raidline/pull/178)与[测试收尾PR179](https://github.com/Disthinker/Project-Raidline/pull/179)均已普通合并，最新main `df45709`；合并树与已测试提交一致。
- 最新准确提交与合并提交的Windows/Ubuntu均各2185/2185通过，原3ms门槛保持。原CI两项准备存档失败及诊断限制如实留档。
- 已验收40路径、游戏程序／入口、旧工作目录和无关未提交修改保留；没有启动游戏或下一项开发。

[任务记录](../exec-plans/active/ui-inventory-presentation-v1.md)包含完整授权、CI原始记录与回退证据。

<details>
<summary>批准、实施及原CI失败历史（以当前已交付为准）</summary>

# 独立仓库页 v2 测试收尾已合并，等待最后CI

功能PR178、测试补强PR179均已普通合并，最新main df45709，合并树与d873361已测试提交一致。测试补强准确提交双平台均2185/2185通过，最新合并CI 39136049313进行中。原ea8d352的Windows失败历史保留，已验收游戏程序不变。state=accepted_delivery_in_progress。以下为历史。

# 独立仓库页 v2：测试隔离补强完成，等待远端验证

已验收程序f7a2af9c01c2不变；原功能PR178已合并。其准确提交双平台2184/2184通过，但合并后Windows 2182/2184，Ubuntu 2184/2184。两项失败均在视野UI测试准备存档时，原断言未记录原因；原失败保留，未重跑覆盖。

当前只补强`test_raid_light_cone_visibility_ui.cpp`：按测试名独占创建目录、清理限制在自有临时根、输出保存／读取错误，新增同种子目录互不复用和互不删除回归。原两项本地各重复25次均通过，尚未复现远端失败，因此不把临时目录缺口当作已证实的唯一根因。

测试补强本地全目标构建成功，视野专项11/11、完整2185/2185通过。原40项验收文件、游戏程序／运行资源／固定入口均保持；src、CMake、CI工作流、原断言、并发和3ms负载没有修改。

`task_id=ui-inventory-presentation`；`plan_version=RL-UI-INVENTORY-PRESENTATION-v2`；`ci_fix_version=RL-UI-INVENTORY-CI-ISOLATION-v1`；`state=accepted_delivery_in_progress`。这是用户已授权交付的普通内部测试失败收尾，不启动新的玩法任务；不另记未经人类给出的独立方案批准。仅提交该测试文件及本任务两份状态文档，在新准确提交与合并提交都验证双平台CI后收尾，不放宽检查。

以下保留完整诊断、原验收、Git交付与失败历史。

# 独立仓库页 v2 已合并：收尾CI的测试隔离补强

用户已验收的游戏程序仍为 **f7a2af9c01c2**，固定入口不变。功能提交fcecf2e和PR178已普通合并为ea8d352；合并树与已验收提交一致。**原合并后CI的Windows 2182/2184，Ubuntu 2184/2184；尚不能标为全部交付检查通过。**

`task_id=ui-inventory-presentation`；`plan_version=RL-UI-INVENTORY-PRESENTATION-v2`；`ci_fix_version=RL-UI-INVENTORY-CI-ISOLATION-v1`；`state=accepted_delivery_in_progress`。

## 已确认与未确认

- 原PR准确提交CI 39130421095：Windows／Ubuntu均2184/2184首轮通过。合并CI 39132015260：Windows两项失败，Ubuntu全部通过；原3ms性能项通过。原失败保留，不重跑覆盖。
- 两项失败均为`test_raid_light_cone_visibility_ui.cpp`准备阶段的save／continue断言，尚未运行视野断言；此文件原样来自既有main，没有在库存v2中修改。断言未输出保存错误消息，**现有证据不能确定根因**。
- 该夹具用时间戳拼目录名，没有独占创建保证；这是确定存在的隔离缺口，但没有证据证明它是本轮失败的唯一原因。并发目录碰撞或平台文件访问失败仍属假设。本地原测试重复诊断单独保留，不用本地通过替代远端失败。

## 本次只做必要的内部测试收尾

沿用用户“验收通过，请完成提交、推送和合并”的交付授权，并按根AGENTS允许普通内部编译／测试失败自主修正的规则执行。这不是新的玩法／存档方案，也不把本技术子步骤记为已获得新的独立人工批准。

1. 只修改上述视野UI测试夹具：按测试名隔离，用`create_directory`原子认领空目录；存在时选择新后缀，不复用他人的目录。清理前校验解析后的目录位于临时根且属测试前缀。
2. 保留原断言，补充save返回消息、continue持久化消息与测试路径。禁止生产保存重试、放宽断言、全套串行化、降低负载或修改3ms门槛。
3. 新增相同种子同时持有两个目录的确定性回归，验证互不复用／互不删除。先构建、专项及完整CTest，再测试修复提交／推送／PR；准确提交和合并提交都验证双平台首轮CI。

不修改任何src、资源、CMake、CI工作流或游戏入口；原40项冻结源码／测试路径、程序及资源指纹保持。复用当前库存目录，在`codex/ui-inventory-ci-isolation-v1`从ea8d352继续，不新建完整代码工作区或测试入口。修复测试之外的任何运行时根因若被新日志证实，另按实际影响准备方案，不在本步骤扩张保存行为。

证据目录：`C:\Users\Administrator\Documents\美术资源生成\tmp\ui-inventory-ci-isolation-v1-20261018`；原PR及失败日志：`C:\Users\Administrator\Documents\美术资源生成\tmp\ui-inventory-v2-delivery-20261018`。完成后保留全部原失败事实，只在新检查真实通过后更新收尾状态。以下保留先前完整记录。

# 独立仓库页 v2 已合并，等待合并后CI

PR #178已普通合并，提交ea8d352；合并树与已验收功能提交fcecf2e一致。准确提交的Windows/Ubuntu均2184/2184通过，合并CI 39132015260正在进行。state=accepted_delivery_in_progress；不开始下一任务。以下为完整交付记录和历史。

# 当前：独立仓库页 v2 已验收，交付中

用户已验收候选 **f7a2af9c01c2**，并明确授权提交、推送和合并。固定桌面入口 **Project Raidline Debug** 保持不变。

- `task_id=ui-inventory-presentation`；`plan_version=RL-UI-INVENTORY-PRESENTATION-v2`；`state=accepted_delivery_in_progress`。
- accepted_candidate_id：`RL-UI-INVENTORY-PRESENTATION-v2-local-f7a2af9c01c2`。acceptance_source：2026-10-18主控线程直接人类消息“独立仓库页 v2 验收通过，请完成提交、推送和合并。”。
- 验收源码40路径、程序及入口指纹均已复核；本地2184/2184、核心182/182、原3ms检查有效。只补交付记录，不改已验收程序。
- 先验证新PR准确提交的Windows/Ubuntu CI，再普通合并，并核对合并树及合并提交双平台CI；不绕过失败门槛，不自动开始下一任务。

[任务记录](../exec-plans/active/ui-inventory-presentation-v1.md)记录范围、批准、候选与交付证据。以下为完整历史，以本节当前状态为准。

# 当前：库存展示 v2 已完成，待验收

候选 **f7a2af9c01c2** 已完成独立仓库页、旧档无损迁移和固定入口。请使用桌面 **Project Raidline Debug**，按[简短验收清单](../acceptance/ui-inventory-presentation-v2.md)正常游玩。

- `task_id=ui-inventory-presentation`；`plan_version=RL-UI-INVENTORY-PRESENTATION-v2`；`state=waiting_human_acceptance`。实施批准已记录，尚无本候选人工验收。
- 六个独立8×8页、384格。旧合法位置不动，跨页物品迁移，余物只领取；大格、等比图片和既有UI／玩法保留。
- Windows Debug全目标构建、核心182/182、完整2184/2184通过；原3ms性能最高P95 1.3868ms。
- 复用原库存目录／分支，HEAD `b09c485`，代码及程序冻结。旧脏工作目录和v1快照保留；未提交、推送、合并、启动游戏或访问真实存档，不开始下一任务。

[开发记录与授权](../exec-plans/active/ui-inventory-presentation-v1.md)保留具体实现、候选指纹及完整验证边界。

<details>
<summary>历史状态（以当前v2待验收为准）</summary>

# 当前：库存展示 v2 已批准，实施中

独立6页仓库、旧档无损迁移及固定Debug入口。用户已明确批准v2，暂不提交、推送、合并或启动游戏。`state=implementing`。

[任务记录](../exec-plans/active/ui-inventory-presentation-v1.md)保留完整范围、批准原文和v1快照。下方为历史。

<!-- isolated-pages-v2-state:begin -->
# 当前：库存展示 v2 修订方案待批准

用户要求仓库各页真正隔离，并反馈每轮新入口造成混淆。v1跨页方案未获本轮人工验收，旧候选 `bfb2bd6823e8` 保持冻结，不提交、推送或合并。

- `task_id=ui-inventory-presentation`；`plan_version=RL-UI-INVENTORY-PRESENTATION-v2`；`state=waiting_plan_approval`。仅完成修订设计，v2无实施批准或新候选。
- 建议6个独立8×8页、总384格，领域规则禁止跨页。旧档只迁移跨页物品，放不下的旧物进入只领取的待整理区；大格和等比图片保留。未来统一固定桌面入口，复用当前库存开发目录，不再为每次修订新增入口／完整代码目录。
- 本轮只读检查及文档记录；原五工作区、v1源码／程序／运行资源／当前入口不变。没有构建、测试、真实存档迁移或Git交付操作。

[具体v2方案及旧档影响](../exec-plans/active/ui-inventory-presentation-v1.md)。当前以上方为准，下方保留完整历史。
<!-- isolated-pages-v2-state:end -->

<!-- ui-inventory-implementation:begin -->
# 当前：库存展示 v1 已完成，待验收

候选 **bfb2bd6823e8** 已完成仓库6页、大物品格和图片等比显示。桌面 **Project Raidline 库存验收** 指向独立程序；请使用[简短验收清单](../acceptance/ui-inventory-presentation-v1.md)正常游玩。

- `task_id=ui-inventory-presentation`；`plan_version=RL-UI-INVENTORY-PRESENTATION-v1`；`state=waiting_human_acceptance`。用户已批准实施，尚无本候选人工验收。
- Windows Debug构建成功；专项93/93、全量2137/2137通过；原32／100敌人诱饵P95最高1.2104ms，原3ms门槛及负载保持。
- 原存档、已验收UI／玩法与旧程序／入口保留。开发分支 `codex/ui-inventory-presentation-v1` / HEAD `b09c485`，14路径源码与程序冻结；未提交、推送、合并、启动游戏或操作真实存档，不开始下一项开发。

完整授权、候选与检查证据见[任务记录](../exec-plans/active/ui-inventory-presentation-v1.md)。下方为完整历史，以本节为当前状态。
<!-- ui-inventory-implementation:end -->

<!-- ui-inventory-presentation-v1:begin -->
# 当前：库存展示 v1 方案待批准

用户于2026-10-18要求策划安排下一步UI优化并给出具体方案，优先仓库分页、大物品格和装备图片等比显示。**本轮只完成策划与代码核查，尚未获批实施。**

- 单一任务 `ui-inventory-presentation` / `RL-UI-INVENTORY-PRESENTATION-v1` / `state=waiting_plan_approval`；`approval_source`、本任务候选及人工验收均无。
- 方案：仓库保留24×16共384格，显示为6页8×8；统一64逻辑像素格，左侧切换装备／胸挂／背包；跨页物品可整件定位和拖动，装备／网格／虚像图片保持原比例。容量、原坐标、存档与玩法规则不变。
- 策划PLANNING-02已按真实目标及request/task/version/baseline核对，正式回复为待人类批准的建议；前次无关回复不作为本任务结果。
- 已核查main仍为PR177合并后的 `b09c48542b46512b13c72f63af8d79892b069f82`，现有整合源码与其同树。批准后再从当时已验收main建立隔离分支；当前没有新代码、分支、构建、测试、提交、推送、合并或游戏启动。
- 原四个工作区、全部未提交修改、三个既有程序／运行资源／桌面入口保留。本轮游戏目录只增加[具体待审批方案](../exec-plans/active/ui-inventory-presentation-v1.md)及本状态入口。

下方完整保留此前交付与历史记录；本任务以本节和上述方案为准，旧任务已交付状态不变。
<!-- ui-inventory-presentation-v1:end -->

<!-- ui-decoy-integration-v1:begin -->
# 当前：UI与诱饵性能整合 v1 已验收并交付

候选 **5bf87b7d4c85** 已通过用户联合复验，并完成提交、推送和普通合并。**本轮不开始下一项开发。**

- 功能提交 `e41ebea1d1a04e331cc0a967b65f19e9fc468164`；[PR177](https://github.com/Disthinker/Project-Raidline/pull/177)；main合并提交 `b09c48542b46512b13c72f63af8d79892b069f82`。
- 准确提交CI `39112153044` 与合并提交CI `39113511470` 的Windows/Ubuntu均各2117/2117首轮通过；合并树与已验收提交树完全一致。原3ms及负载保留。
- `task_id=ui-decoy-perf-integration`；`plan_version=RL-UI-DECOY-PERF-INTEGRATION-v1`；`state=delivered`。20路径仅含验收集合及任务文档；旧PR176因同内容已交付而关闭，未单独合并。
- 桌面 **Project Raidline 整合复验** 仍是本次已验收程序。原UI验收、Debug程序/入口、其他工作区分支和未提交修改保留。未启动游戏或操作真实存档。

完整授权、提交/CI/回滚证据见[任务记录](../exec-plans/active/ui-text-foundation-v2.md)；[复验清单](../acceptance/ui-decoy-perf-integration-v1.md)保留为历史。下方是先前状态，当前以上方为准。
<!-- ui-decoy-integration-v1:end -->

<!-- ui-foundation-v2:begin -->
# 当前：UI基础 v2 已验收；诱饵性能整合方案待批准

2026-10-18，用户验收UI候选 **c0e7b3a8cd00**，要求先给出剩余诱饵性能处理方案，**暂不提交、推送或合并**。UI源码、程序与桌面入口保持冻结。

- UI任务为 `accepted_delivery_blocked`。UI专项12/12；最终完整回归原批次2110/2112，一项0xc0000043退出单次复跑通过、原因未明；100敌人诱饵P95 **3.5805ms > 3ms**仍未通过。
- 既有诱饵v2 **e970acbc3d71** 的准确提交CI首轮Windows/Ubuntu各2105/2105通过，但仍待人工复验、未合并。UI基线没有带入这份性能修复；本轮UI验收不覆盖诱饵v2。
- 建议 **RL-UI-DECOY-PERF-INTEGRATION-v1**：批准后在第三个隔离目录组合已验收UI和现有5文件性能补丁，重新构建、全量检查及3ms验证，再提供简短联合复验。当前 `waiting_plan_approval`，尚未实施。
- 现有 **Project Raidline UI验收** 和 **Project Raidline Debug** 入口均不改。本轮不改游戏代码、不构建、不重跑测试、不启动游戏。

范围、授权来源和证据见[UI任务与性能处理方案](../exec-plans/active/ui-text-foundation-v2.md)。下方原任务历史字节保留，最新状态以上方为准。
<!-- ui-foundation-v2:end -->

# Project Raidline 当前状态

<!-- current-plan:begin -->
## 当前：声响诱饵性能修复 v2 CI通过，待复验

新候选 **e970acbc3d71** 已完成 **RL-RAID-NOISE-DECOY-PERF-v2**。本地全目标构建、完整CTest2105/2105、专项118/118通过；准确提交的Windows/Ubuntu CI各2105/2105首轮通过。原3ms门槛和全部测试负载保留。

- 入口：桌面 **Project Raidline Debug**，使用[复验清单](../acceptance/raid-noise-decoy-v1.md)正常游玩。
- 按用户预先授权已提交、推送`b4475f8`至[草稿PR #176](https://github.com/Disthinker/Project-Raidline/pull/176)。**v2尚未人工复验，未合并**；不沿用v1验收。main仍为`5eee3fa`。
- 原目录、v1冻结候选、失败CI和其他修改保留；未启动游戏、操作真实存档或下一项开发。完整授权、源码/程序指纹和CI证据见[任务记录](../exec-plans/active/raid-noise-decoy-v1.md)。
<!-- current-plan:end -->

<!-- indoor-delivery:begin -->
## 上一项交付：室内双层视野 v1 已验收并合并

候选 **595cc7c1c5d6** 已通过用户验收，[PR #171](https://github.com/Disthinker/Project-Raidline/pull/171)已普通合并到main。三个室内的遮挡显隐与取物视线复核完成，窗口准星、菜单、搜尸和M地图权限保留。

- 提交 `79d164e`，合并 `7c8723b`；准确PR head与合并提交的Windows/Ubuntu各1944项通过，合并树一致。
- 桌面 **Project Raidline Debug** 为同一已验收程序。原代码、工作区分支与其他未提交修改保留，不启动游戏、操作真实存档或下一项开发。
- 完整交付及检查证据见[任务记录](../exec-plans/active/raid-indoor-visibility-v1.md)；[试玩清单](../acceptance/raid-indoor-visibility-v1.md)保留为历史验收步骤，无须重复验收。
<!-- indoor-delivery:end -->

<!-- delivery-status:begin -->
## 最新交付：边境交换站搜尸中文修正版

用户已验收中文修正版 **03f42d4333a9**；[PR #170](https://github.com/Disthinker/Project-Raidline/pull/170)于2026-10-14普通合并。搜尸和中文显示已交付到main，其他未提交修改保留。

- PR的Windows/Ubuntu各1924项通过；合并提交CI：**Windows/Ubuntu均success**。
- 这是上一项已完成交付；当前室内视野候选见上方。
- 桌面 **Project Raidline Debug** 现为上方室内视野待验收候选；搜尸交付与main状态不变。

准确提交和CI证据见[任务记录](../exec-plans/active/raid-corpse-search-v1.md)。下方均为历史，不覆盖本节。
<!-- delivery-status:end -->

## 提交时记录（历史）

用户于2026-10-13明确验收当前中文修正版，并要求提交与合并。本次只交付搜尸功能、中文修正和对应任务文档；其他未提交代码和配置保持原样。

- 候选编号末尾 **03f42d4333a9**，源码23路径与Debug程序核对未变。
- Windows Debug 专项104项、全量1924项通过；主线依赖CI通过。准确提交的Windows/Ubuntu CI通过后，沿用普通合并。
- 不启动游戏，不操作真实存档，不开始下一项开发。桌面 **Project Raidline Debug** 仍指向已验收程序。

本任务提交在main可达且合并提交CI通过时为已交付；准确提交、PR和CI以实际Git/远端查询为准，不从本页推断。详见[任务说明](../exec-plans/active/raid-corpse-search-v1.md)。

## 历史记录

以下保留当时的进度和证据，不代表现在还要执行这些步骤。当前状态以本页开头和当前任务说明为准。

## 2026-10-13 验收入口与反馈已简化

- 已按用户请求精简根指令和交付报告模板；聊天给结论、必要影响及下一步，详细证据保留[当前任务记录](../exec-plans/active/raid-corpse-search-v1.md)。审批/验收门槛不变。
- 桌面 `Project Raidline Debug.lnk` 指向下面同一搜尸待验收构建；目标和工作目录已读回核对。21路径候选源码/程序hash未变，未启动游戏、未提交或合并，仍待用户正常游玩验收。

## 2026-10-12 当前候选：Frontier 尸体搜索试点 v1（待用户验收）

- task `raid-corpse-search-v1` / plan `RL-RAID-CORPSE-SEARCH-v1`，用户明确批准后完成本地实施，候选 **`RL-RAID-CORPSE-SEARCH-v1-local-2b7c77f6d58b`**。三个既有护甲守卫、三份原ordinary Loot冻结与独立尸体根；F/格子取舍、终局和旧schema边界接通。schema52 / Frontier新rules30 / content64，正式资源未改。[完成证据与统一验收清单](../exec-plans/active/raid-corpse-search-v1.md)。
- Windows Debug全目标构建成功；定向 **261/261**，最终完整CTest **1918/1918**（111.93秒），新增20项。五seed旧/新部署审计逐项一致。四项App指针函数与camera/aim/relocate/Base指针文件原代码保留，窗口锚定/菜单回归通过。依赖main CI38145513346首轮失败、重跑第2次success，未改旧测试。
- 开发分支 `codex/raid-corpse-search-v1` / HEAD `f6a288b4da7579814b69a682dde9652812543528`，本片完全未提交、index为空，无push/PR/merge或本片远端CI。开发worktree `E:\WorkPlace\Projects\C\Project Raidline-collaboration-delivery-20261011`，正确验收程序 `E:\WorkPlace\Projects\C\Project Raidline-collaboration-delivery-20261011\build\windows-debug\Project_Raidline.exe`；原目录旧鼠标分支/HEAD、代码及其他脏文件继续保留，不用原目录旧程序验收尸体。
- 21路径+构建hash清单在支持工作区 `tmp/raid-corpse-search-20261012/candidate.json`；manifest `2b7c77f6d58b7473244b27a62ce46a55f5e4d7b028a9522911690e3625aee479`，构建SHA256 `3767cf6fc19e799add8bd0c1bf22b2d073ba24a045b0aebf9eb2874763243a6a`。候选冻结，尚无用户正常游玩验收；未启动游戏、未操作真实存档、未改信任/权限/模型，等待用户按清单复验，不启动其他开发。以下保留审批与实施过程历史。

## 2026-10-12 当前开发：Frontier 尸体搜索试点 v1

- task `raid-corpse-search-v1` / plan `RL-RAID-CORPSE-SEARCH-v1` / state **实施中**；真实用户明确“同意 RL-RAID-CORPSE-SEARCH-v1，请按方案开发，保留已验收的窗口锚定和菜单行为，完成后提供验收清单，暂不提交或合并。”。保留窗口锚定/菜单行为；不提交/推送/PR/合并，不启动游戏。
- 基线 `f6a288b4da7579814b69a682dde9652812543528` / 独立分支 `codex/raid-corpse-search-v1`，开发 worktree `E:\WorkPlace\Projects\C\Project Raidline-collaboration-delivery-20261011`；原目录旧鼠标分支/HEAD与其他未提交修改原字节保留。三个试点、三份原ordinary结果、独立尸体根、schema52/rules30/content64沿用[已批准计划](../exec-plans/active/raid-corpse-search-v1.md)。新候选完成前无本片验证/人工验收。
- 合并后CI38145513346首轮Windows旧临时保存用例失败，Ubuntu成功；失败job已重跑，单独记录，不写成通过。以下为审批前/交付历史。

## 2026-10-11 当前下一项：Frontier 资源守卫尸体搜索试点（待方案审批）

- 用户已要求 v2 合并后下一项。上一窗口锚定已交付 PR #169 / main `f6a288b4da7579814b69a682dde9652812543528`，准确 head Windows/Ubuntu各1898/1898、合并树一致；main CI `38145513346` 独立在运行，不据此宣称通过。原工作区代码、其他脏文件、历史、branch/HEAD/index均保留。
- 策划 NEXT-01 正式选题与 DELIVER-02 合并后确认均已核对四元组和真实目标；单一任务为 Frontier 三组已有资源护甲守卫的尸体搜索。只划转三份现有 ordinary Loot，不增奖励/重抽；不做压力波掉落、剥甲、AI、尸体破坏或正式美术。
- task `raid-corpse-search-v1` / plan **`RL-RAID-CORPSE-SEARCH-v1`** / baseline `f6a288b4da7579814b69a682dde9652812543528` / state **待计划审批**；[具体计划](../exec-plans/active/raid-corpse-search-v1.md) 已完成真实路径/独立尸体根/冻结绑定/复用格子/终局与schema兼容调查，等待本版本明确批准。策划建议、下一项请求与 CI 均不补足具体方案批准。
- 尚未实施下一项代码、产资源、构建新候选或提交新任务；原游戏目录仍为保留的鼠标开发分支 `codex/pointer-aim-continuity-v1` / HEAD `d391278`，干净隔离交付checkout在已接受main。批准后从最新已接受主线开始独立分支，不叠加旧在制分支。以下保留交付与旧过程记录。

## 2026-10-11 最新交付：窗口锚定 v2 已验收合并

- 已交付提交 `8fad7d297de07c2ea886c10a4c338cdc1d9acba1`，PR #169，普通合并 main `f6a288b4da7579814b69a682dde9652812543528`；准确 head 的 Windows/Ubuntu CI `38144550734` 均 **1898/1898**，合并树与验收提交一致；隔离本地 main 已快进。合并提交自身 main CI `38145513346` 当前 `in_progress` / `结果未出`，不以 PR CI 替代。以下‘交付中’为提交时记录。
- task `pointer-aim-continuity-v1` / plan `RL-POINTER-WINDOW-ANCHOR-v2`；用户明确“窗口锚定 v2 验收通过，请完成提交与合并，启动下一项开发。”。已验收候选 `RL-POINTER-WINDOW-ANCHOR-v2-local-cfff4c1d7de2`，源码清单摘要 `cfff4c1d7de2f1ad93fabd0e7a4913a04f39667c1907db481df7aa98c256c2eb`，准星窗口锚定/限位及菜单接续验收通过；原 v1 失败历史保留。
- Windows Debug 全目标构建与最终 1898/1898 CTest、定向 57+1 项通过；候选源码/构建 hash 核对未变。交付从已验收 main `772fba0d3068a69bb207566d21a01913eb224451` 的干净 checkout，分支 `codex/pointer-window-anchor-v2`，仅 15 个代码/测试/CMake 路径与[本任务计划](../exec-plans/active/pointer-aim-continuity-v1.md)、本入口记录；其他脏文件及原工作区 HEAD/branch/index 保留。
- 本入口随候选提交。该提交在 main 可达时为已交付，尚未合入时为交付中；准确 commit/PR/Windows 与 Ubuntu CI/普通合并需按实际 Git/PR 查询，不能从文档推断。无 CI 专用补提交，不启动游戏，不改信任/权限。
- 用户已要求下一项；CI 等待期间已通过现有三线程原生入口只读请求策划单一任务（`RL-POINTER-V2-20261011-NEXT-01`，baseline `8fad7d2`）；主控在交付完成后核对最终 main 并提出具体版本计划。没有该新任务的明确方案批准前，不编辑游戏代码/生产资产/提交新任务。以下旧状态均为历史，不覆盖本入口。

## 2026-10-11 当前入口：三线程协作配置维护 v1

- 最新协作规则 `RL-COLLAB-20261011-v1`；本次用户直接授权配置维护，不是游戏开发或资产生产。[维护任务](../exec-plans/active/collaboration-maintenance-v1.md) 记录方案/候选/真实授权及验证；已交付；配置提交 `c24aae1f960b988ce7de144f488ddc5ce8d67f40`，[PR #168](https://github.com/Disthinker/Project-Raidline/pull/168)，CI `38137564434` success；main merge `772fba0d3068a69bb207566d21a01913eb224451`，本地 main 已快进。鼠标修复未纳入，暂不启动下一项开发。格式/引用/加载、原三角色重读、两条真实通信通过；15 个隔离模型场景符合要求、辅助脚本 5/5，通过事实与未验证边界见[维护报告](../engineering/COLLABORATION_MAINTENANCE_2026-10-11.md)。
- 现有三个持久线程/角色及原生通信入口见 [THREADS](../coordination/THREADS.md)。恢复会话先查当前任务状态；“继续/下一项”不补缺失的计划批准、Bug 方案批准或人工验收。
- **独立保留的游戏在制项**：用户实机复验鼠标/准星修复未通过；菜单开关通过，建筑切换跳点未通过。窗口锚定修正方案 v2 已按用户批准完成本地修正和自动化，待用户复验；不提交/合并。本次已交付配置不追认鼠标批准/验收，其他历史记录保留。

## 2026-10-11 当前候选：窗口锚定修正方案 v2（待用户复验）

- 同一 task `pointer-aim-continuity-v1`；state **待用户验收**，plan `RL-POINTER-WINDOW-ANCHOR-v2`。真实用户已批准本版本，要求保留菜单行为、提供复验清单、暂不提交或合并。原 v1 用户确认菜单开关通过，建筑切换失败；该失败记录保留。
- 已将实际准星、控制目标与输入基准按相机变化转换，在建筑切换/移动的当前帧击发前同步；窗口严格限位取代 48 像素越界，反向输入立即生效。小地图允许窗口内空白位置，地图/弹道边界保持原规则。共用菜单捕获函数逐字节保留，基地跟随镜头同步同一合同，建造镜头沿用原路径。[v2 完成记录与一次复验清单](../exec-plans/active/pointer-aim-continuity-v1.md)。
- 冻结本地候选 `RL-POINTER-WINDOW-ANCHOR-v2-local-cfff4c1d7de2`；15 路径 SHA256 清单及 Debug 程序 hash 见支持工作区 `tmp/pointer-window-anchor-v2-20261011/candidate-v2.json`。分支仍 `codex/pointer-aim-continuity-v1` / HEAD `d391278a844f45c41cd624bdd4a17f797fc832f9`，index 未变；本片不提交/推送/PR/合并，无本片远端 CI。配置 main `772fba0` 的独立 CI `38138542850` success 属于此前配置，不代替鼠标修复证据。
- Windows Debug 全目标构建完成，修正前 3 项新回归如预期失败；修正后定向 **57/57 + 1/1 = 58**，最终完整 CTest **1898/1898**（127.25 秒），v2 净新增 9 项。本片 whitespace 与候选 hash 检查通过；日志 `build/windows-debug/pointer-v2-*.log`。其他脏文件、v1 计划和以下历史正文逐字节保留；schema/content/rules 仍 **51/64/29**。
- 未启动游戏、未操作用户存档、未修改信任/权限，正常游玩未验收。下一步仅由用户用 `build/windows-debug/Project_Raidline.exe` 复验窗口锚点、建筑首帧/下一帧、四角/后坐力以及菜单接续；不启动其他开发。

## 2026-10-11 当前开发：鼠标与准星位置连续性修复 v1

- 场景容器及已验收信号撤离依赖已由用户正常游玩验收并授权合并；[PR #167](https://github.com/Disthinker/Project-Raidline/pull/167) head `393663b` 的 Windows/Ubuntu CI 均 **1879/1879** 通过，普通合并至 main `d391278a844f45c41cd624bdd4a17f797fc832f9`，合并树与验收 head 一致。合并提交自身 main CI `38122471061` 已触发，结果尚待确认。
- 用户追加建筑/菜单鼠标重置缺陷，策划安排本项独立修复；当前 `codex/pointer-aim-continuity-v1` 从上述准确 main 建立。共用指针释放接续当前准星屏幕位置，界面移动不改变世界瞄准输入基准；重新捕获清除暂存位移/已消费点击。进出建筑平移瞄准到新玩家原点，新空间第一帧重接输入基准，保持原相对方向/距离并沿用合法边界限位。
- Windows Debug 全目标重建成功；定向 **35/35** 通过，新增 **10** 项真实 SDL/App 事件路径和 GameplayWorld 回归，涵盖 Base/Raid 背包、详情返回、暂停/设置、焦点恢复、关闭点击、测试场、建筑/场景容器与边界。最终完整 CTest **1889/1889**（138.77 秒）通过，日志 `build/windows-debug/pointer-full.log`；本片 whitespace 检查通过。[范围与复验清单](../exec-plans/active/pointer-aim-continuity-v1.md)。
- 本修复未提交/推送/新建 PR，待用户正常游玩复验；未启动游戏，未修改信任/权限。schema/content/rules 仍为 **51/64/29**。原有未提交维护/历史文件逐字节保留，CURRENT_STATE 原历史正文保留；以下为历史过程记录。

## 2026-10-06 当前开发：Raid 场景容器搜索与取舍 v1

- 用户验收信号撤离；保存为本地提交 `9545ac4`，无推送/PR/合并。远端 main 仍为 `cb522c0`。信号实现最后全量 1864/1864 通过；下方待验收/未提交描述为历史。
- 当前 `codex/raid-scene-containers-v1` 从上述已验收本地依赖建立，Frontier 办公室与货运装卸间各一个固定场景容器；各自迁入两个原有 Loot 结果。F 近距开箱、拖放/旋转/堆叠/交换/快捷转移/放回与持续模拟已接通，离范围或切空间关闭箱格子并清除拖拽。成功只带回携带物，失败不记录场景余物，异常回滚恢复出战前资产；旧冻结 Raid 不补箱。schema/content/Raid rules 为 51/64/29。
- Windows Debug 全目标构建及最终完整 CTest **1879/1879**（102.51 秒）通过，新增 15 项回归，包括实际 F 入口、指针捕获策略和中英文文字像素；原旧存档夹具继续验证历史冻结边界。[实施与正常游玩验收清单](../exec-plans/active/raid-scene-containers-v1.md)。本片代码未提交/推送/PR，无远端 CI，未启动游戏，待用户正常游玩验收。
- 原有维护、历史及支持工作区文件保留；不启动游戏，不修改权限，不自动合并。


## 2026-10-05 主控接管修正：信号撤离待用户验收

- 用户明确“继续下一步任务”；已修复控制结束帧漏中断、抓咬当帧中断延迟，以及 HUD 可呼叫提示与换弹/治疗/背包/寻回/室内入口门禁不一致。
- Windows Debug 全目标构建成功；专项 51/51、完整 CTest 1864/1864（115.75 秒）通过。新增 4 项回归并扩展既有消费者测试；详见[本片计划](../exec-plans/active/signal-extraction-call-v1.md)。下方 1860 项结果属于修正前版本。
- 当前 codex/signal-extraction-call-v1，HEAD cb522c0，全部本片代码未提交；无本片远端 CI、未启动游戏。原有维护修改保留。下一步为用户正常游玩验收，特别确认引敌距离与等待时长的防守压力；本轮未改变数值、失败政策或资源范围。


## 2026-10-05 本地开发：信号撤离主动呼叫与引敌 v1

- 分支 `codex/signal-extraction-call-v1`，基线已验收 main `cb522c0`；fetch 后远端一致，基线 main CI 36594256706 已确认 success。
- 高危现有信号区改为按 F 主动呼叫；每次接受只对本空间 1200 世界单位内存活敌人发送一次声音刺激，不向范围外遭遇组广播、不刷怪。等待可移动/射击/治疗，离区或受控中断后须重呼，普通受伤不直接打断。双语提示区分呼叫、进行中和中断。
- 普通/条件撤离、地图快照、资产/结算与异常退出回滚不改；不升级 schema/content，不改美术/音频/manifest。道路任意合法位置呼叫仍未实现，不把固定区当最终设计。
- 已完成 Windows Debug 全目标构建、35/35 定向回归与完整 CTest 1860/1860（99.78 秒），本片 diff whitespace 检查通过。未提交/推送/新建 PR，本片无远端 CI，未启动游戏；正常游玩验收待用户执行。原有指令维护等未提交改动保留。
- [本片实施与验收计划](../exec-plans/active/signal-extraction-call-v1.md)。下方是已合入版本与历史交付记录。

## 2026-09-29 最新交付：护甲感染者与独立战斗测试场已验收合并

- 用户复验测试场文字/鼠标修复后明确“验收通过，请合并”；[PR #166](https://github.com/Disthinker/Project-Raidline/pull/166) 已普通合并至 main `cb522c0bbf62b8ea3fb5a212a5a2c84db71939a5`，文件树与验收提交 `a86c759ff554eddfafec85f0b2df6abdac901ec8` 完全一致。
- 本片包含 Frontier 躯干护甲感染者、每发弹药穿甲接线、护甲耐久/破损和头腿绕甲，以及独立枪械/敌人测试场、临时装备发放和固定点刷怪。测试场文字/鼠标修复已包含；退出恢复原基地会话。schema/content/Raid rules 为 50/64/29。
- Windows 最终全目标构建成功，完整 CTest 1852/1852（101.84 秒）；验收提交的 [PR CI 36592613505](https://github.com/Disthinker/Project-Raidline/actions/runs/36592613505) Windows/Ubuntu 均 1852/1852 通过。
- 合并提交自身 [main CI 36594256706](https://github.com/Disthinker/Project-Raidline/actions/runs/36594256706) 已触发，最终结果尚未确认，不以 PR CI 代替。
- 本地已切回最新 main；原有未提交指令维护及历史记录保留，未纳入本片提交。代理未启动游戏。下方待验收/未提交和旧计数均为开发过程记录，以本节为准；后续开发范围待用户另行下达。


最后核对：2026-09-29。

## Git 与交付基线

### 2026-09-29 追加开发：独立枪械与敌人测试场

- 用户明确要求测试地图和发放/刷怪面板，作为当前护甲切片的直接测试入口，继续 `codex/raid-armored-infected-v1`；基线仍是 `9c96802`，未提交。
- 基地 **F10 → 进入战斗测试场**；固定 2048×1280 场地，三处敌人刷新点、两处掩体和补给控制台。入场自动打开面板，控制台附近 **F8** 重开，打开时暂停战斗。
- 可选择已发布枪械、兼容弹药与防弹衣；发放已装填测试套装及额外补给。选择普通/护甲敌人、刷新点和 1/3/5 数量；刷新清理旧目标与弹道。控制台附近可通过背包右键改装，F10 继续支持参数调试。
- 测试场会话没有存档仓库或 pending Raid，原基地会话被完整挂起；退出、放弃或死亡后恢复，不回流测试资产/损耗/奖励。进入前先完成基地检查点，禁止活动 Raid/基地战斗中进入。
- 新增 7 项测试，Windows 全目标构建成功；完整 CTest **1850/1850** 通过（106.49 秒），中文界面专项 63/63 通过。未启动游戏，实机排版、手感仍待用户确认；未提交、推送、合并或运行本片远端 CI。
- 用户复验发现测试场面板无字、鼠标隐藏：已修复文字沿用背景绘制色，以及测试场面板漏接鼠标捕获/瞄准遮挡判断。Windows 全目标重建成功，修复后专项 **45/45** 通过；新增 2 项实际离屏中英文文字像素与面板开关输入回归。此前全量 1850/1850 属于修复前版本，本次未重跑全量或启动游戏，待用户重启新版复验。
- [实施记录与验收步骤](../exec-plans/active/raid-armored-infected-v1.md)。下方 1843 项结果属于追加测试场之前的护甲实现。

### 2026-09-29 当前开发：Raid 护甲感染者与弹药取舍 v1（本地完成，待验收）

- 用户授权继续下一步，按策划建议从已接受 main `9c96802` 建立 `codex/raid-armored-infected-v1`。已 fetch 核对主线；HEAD 尚为该基线，新实现未提交。
- Frontier 三个资源守卫组各替换一名躯干护甲感染者，总人数、生命、速度、攻击及 Loot 保持原有配置。修复每发弹药穿甲参数接线，头/腿绕甲、胸甲独立耐久和破损失效已接通。
- 背心/破损标识、减伤与破损反馈、既有敌情档案的防护类型提示已接入；旧快照不补甲，空间切换保留耐久，异常退出沿用 Raid 前档回滚。schema/content/Raid rules 为 50/64/29。
- Windows 全目标构建成功，完整 CTest **1843/1843** 通过（新增 12 项专项）；8 种种子对照确认数量、位置与 Loot 未变。未运行新版本远端 CI，未启动游戏；正常游玩、视觉可读性与平衡尚未验收。
- 详见[护甲感染者计划与一次游玩验收清单](../exec-plans/active/raid-armored-infected-v1.md)。已保留指令维护和旧交付记录，未提交、推送或合并本片；下一步由用户正常游玩验收。
### 2026-09-27 武器组件与改枪成长 v1：已验收合并

- 基线为医院已接受主线 `e29996b`；本片分支 `codex/weapon-components-v1`。
- 单平台三组件、背包/仓库右键详情与改装、内容包补领、Loot、实际射击属性、保存及整枪寻回闭环已完成。
- schema/content/Raid rules 为 49/63/29；Windows 本地全目标构建和 CTest 1831/1831 通过。
- 用户明确“验收通过，请合并”；[PR #165](https://github.com/Disthinker/Project-Raidline/pull/165) 已普通合并至 main `9c9680240b5a65142c33b03a0d895b54855079b5`。
- 验收提交 `323a81b` 的 [CI 36294353875](https://github.com/Disthinker/Project-Raidline/actions/runs/36294353875) Windows/Ubuntu 均 1831/1831 通过；合并树与验收提交完全一致。
- 合并提交自身 [CI 36294962019](https://github.com/Disthinker/Project-Raidline/actions/runs/36294962019) 已确认 Windows/Ubuntu 均成功；该结果独立于 PR CI。
- 详见[改枪计划](../exec-plans/active/weapon-components-v1.md)。既有指令维护不包含在本次交付中。

### 2026-09-27 当前开发：武器组件与改枪成长 v1

- 用户同意策划的单平台三组件范围，分支 `codex/weapon-components-v1` 来自已接受 main `e29996b`。
- 本地已接组件定义/资产关系、枪匠原子改装、有效射击属性、Loot、保存与寻回；
  schema/content/Raid rules 为 49/63/29。尚未提交、远端 CI 或用户验收，不属于已接受主线。
- 前一轮 Windows 构建和 CTest 1825/1825 通过后，用户反馈背包入口和旧存档内容包缺失；
  已补枪械右键详情/改装、返回背包及开发内容包版本补领。返工后 Windows 全目标构建成功、CTest 1831/1831 通过；界面实机与正常游玩未验证。
- 实施与验证进度见[改枪计划](../exec-plans/active/weapon-components-v1.md)；原指令维护和医院收尾修改保留。

### 2026-09-27 当前覆盖：医院已验收合并

- 用户于 2026-09-26 明确“验收通过，请合并”；[PR #164](https://github.com/Disthinker/Project-Raidline/pull/164)
  已普通合并至 main `e29996b14e8be4adeac0466b5a8c8e29b0b50779`，完整文件树与验收提交 `4bbc0c4` 一致。
- 验收提交的 Windows/Ubuntu CI 均为 1815/1815；合并提交自己的
  [CI 36248518775](https://github.com/Disthinker/Project-Raidline/actions/runs/36248518775)
  双平台构建和测试亦已成功。本地交付前增量构建无待编译内容，医院/镜头/空间查询 33/33 通过。
- 医院地图、医疗 Loot 与既有消费者、室内跟随镜头、未购图移动局部地图及购图完整布局已收束；
  schema/content/Raid rules 保持 48/62/29。用户验收是独立证据，不由自动化推导。
- 合并后的[下一阶段策划评审](../handoffs/hospital-closeout-next-stage-2026-09-27.md)已完成；后续用户同意的改枪范围见上方当前开发记录。
  正式资源、NPC、导航及第三主题没有自动重开。现有未提交指令维护不包含在上述主线提交中。

以下为历史实施与验收前记录；其中 Draft、待 CI、待复验及旧主线描述不覆盖以上当前事实。

- 2026-09-13 室内验收返工：修复医院镜头不跟随；按用户修订，未购建筑图时 M 仍显示
  随玩家移动的局部窗口，其余黑色、不累计揭图；已购图显示完整静态布局。沿用 Draft #164。
  基线 52d0263 双平台 CI 已通过，本轮需独立验证；不改 schema/content/rules 48/62/29。
  本修复全目标 Windows Debug 构建及最终 1815/1815 CTest（137.89 秒）通过，用户复验未执行。

### 2026-09-13 医院标识与完整消费者收束（同一 Draft #164）

- 六处医院房间/出口中英文标识、地标建筑名称已接入；只读摄像机裁剪，不新增导航或泄露情报。
- 药品真实拾取→携物普通撤离→保存重开→原容器保留→实际用药，及失物两条寻回/重放/回滚已纳入回归。
- 128 seed 真实出击发现并修复遭遇人数冲突：保留 24～32 人，巡逻组最低 2 人；旧 pending 不变，加载拒绝不兼容群组容量。
- 主线仍为 6eae183；上一内部检查点 48cd26f 的精确 CI 34752775403 双平台现已通过。
  本次新增标识/兼容修复仍须独立精确 CI，不能继承结果；不得写为医院已验收合并。
- schema/content/rules 48/62/29；下一步是双平台 CI 和用户一次完整医院游玩验收，不继续横向扩张。
- 本轮 Windows Debug 全目标构建、最终全量 CTest 1813/1813（138.99 秒）通过；未启动游戏，人工验收仍待用户。

### 2026-09-13 医院正式内容接线（Draft #164，未合并）

- 医院定义已物化进入 core.json，正式地图选择可选择第五张地图；content 62，schema 48/rules 29 不变。
- 已接医院区域路线、八区/三地标、固定单层内部、三档医疗 Loot、高危与现有撤离；新增主要中英文名称。
- 真实 GameSession 三个 seed 出击、快照往返、室内进出后死亡不复活、主动退出及重复退出测试已加入。
- 修复医院后勤站守卫允许区与地标区不一致导致真实 Deploy 失败；旧 content 61 显式读取兼容。
- 仍为开发分支事实：完整消费者验收与用户正常游玩未完成，不宣布医院阶段关闭。
- 后续重点：成功携物撤离、死亡失物及两条寻回、跨进程旧 pending Raid，以及医院建筑/房间占位语义；不新增玩法机制。

### 2026-09-13 医院候选外围与室内

- 同一 Draft #164 中新增 12800×7200 八区外围、一个医院院落及两个辅助地标、
  固定单层医院和三档现有物品 Loot 表候选。真实加载/生成/通路测试覆盖这些数据。
- 高危/撤离锚点支持显式分区约束，小图回退密度按面积调整，Frontier 黄金结果不变。
- 数据位于 content/drafts，仅测试组合加载；正式地图仍四张，schema/content/rules 48/61/29。
  医院尚未开放出击，未获得用户正常游玩验收。上次 7e1056a 精确 CI 本轮核对仍排队。
- 下一步正式接线、双语和跨进出室内/撤离/失物/保存消费者验证；不新增医疗机制或资源生产。

### 2026-09-13 主题基础已合并，医院继续实施

- 用户授权后 #163 以主题基础改造范围普通合并至 main **6eae183**，树与
  **c468a86** 一致；精确 CI 34690209085 和 PR 双平台全部通过。
- 医院整体没有因此完成。后续分支 `codex/hospital-district-layout-v1` 来自该主线。
- 新增可选的地标局部建筑模板及内容边界/重叠校验；医院 U 形院落原型用于生成回归，
  不公开为新地图。旧地标缺省路径与 layout v4 哈希保持不变，保存格式未升级。
- 下一步仍是医院专用分区/地标/固定室内与资源消费者，而不是新工事或导航系统。

### 2026-09-12 医院主题实施中的内部检查点

- Draft #163 开始代码实施：提取现有分区填充规则，并新增三个修改前采集的 Frontier v4
  黄金 seed 哈希，约束地表/建筑/设备迁移不能改变已验收生成结果。
- 医院定义、固定室内与出击入口尚未实现；不是新增可游玩地图。schema/content/rules
  仍 48/61/29。完整构建/回归和精确提交 CI 以本次 PR 证据为准。
- 下一步加入医院专用分区、固定医院地标、合法 Socket 和回退布局，再接固定单层室内；
  继续同一宏切片，不要求用户验收内部接口。

### 2026-09-12 验收收尾：#162 已普通合并

- 用户明确验收通过并授权合并，主线更新为 **2ae1a8c**，完整树等于验收提交 **a3e67cb**。
- 精确提交 CI `34662866087`：Windows 1795/1795（279.89 秒）、Ubuntu 1795/1795（95.21 秒）；
  本地 1795/1795（121.59 秒）。PR 必需检查亦全部成功，没有强推或修改用户存档。
- 固定防御位与基础木制路障 v1 完整闭环已验收收尾。schema/content/Raid rules 仍 48/61/29。
- 下一片按路线先向策划同步主线并取得单一范围裁决，不默认追加守城、NPC、自由墙或导航。
  外部 GDD 的 2026-08-31 当前状态已滞后，不能将其中旧未实现项直接当作新任务。
- 策划已回复下一单一方向：医院区域 Raid 主题。开发侧已建立
  [正式计划](../exec-plans/active/hospital-raid-theme-v1.md)，当前只是规划检查点，医院尚未实现。

### 2026-09-12 最新覆盖：#162 补齐 B 工事操作，待整体验收

- main **7ee5f27** 为 #160@f263284 的普通合并，树一致；精确 CI `34617363067`
  双平台 1777/1777。下方 main c6d4597 / Draft #160 均是历史状态。
- `codex/base-fortification-build-ui` 从此主线继续。日常工事实体、弹道、残架/耐久双语
  显示已接入；有界状态缓存，不在命中时重建敌人或地图。服务只投影已提交 owner。
- #162 从 `04d7048`（精确双平台 1783/1783）继续：B 新增设施/工事分类，公共建材购买到储备、
  固定位置绿红预览、五档缩放/平移命中、右键换位/收纳/修复。预警只允许维修；交战关闭建设。
- 预览按 Profile/静态几何/玩家位置失效并按槽缓存，确认重新验证；残架保留占地，修复不得
  包住角色或地面物品。Esc 优先取消建设操作；拒绝和保存失败不改变 owner/碰撞。
- 本分支已开放完整建设—防守—修复流程，但未经过用户正常游玩验收、未合并。
  schema/content/Raid rules 仍为 48/61/29；最终自动化与精确 CI 证据见活动计划和 PR #162。

### 2026-09-11 最新覆盖：自动防守工事贡献与磨损，Draft 内部步骤

- #160 从 9bda96d 继续，main c6d4597 未变；基线精确 CI `34577357524` 双平台 1766/1766。
- 自动防守按冻结来向读取原工事，最多抵扣 2 Security、每参与实例扣 30 耐久；最低需求 8，
  零贡献零磨损。确认、保存失败重试、到期预设共用同一结算，旧预警无新增抵扣或消耗。
- 窗口显示双语抵扣/磨损；schema/content/Raid rules 仍 48/61/29。最终测试与 CI 见活动计划/PR。
- 本轮 Windows Debug 全目标构建、1777/1777 CTest（108.26 秒）通过；精确提交 CI 另记 #160。
- 下一步 B 目录/鼠标安装收纳维修及日常阻挡投影；未完整接通前不出售工事，不启动游戏或合并。

### 2026-09-11 最新覆盖：工事预警冻结与耐久持久化，仍为 Draft 内部步骤

- 同一 #160 从 2bf8834 继续；main c6d4597 未变，基线 CI `34554568291` 双平台通过。
- 新预警一次冻结来向、静态几何、波次和工事身份；开战只更新实际耐久与当前射击检查点，
  不因维修、整理库存或重开页面重抽。没有合法实时路线的新预警保持自动可用，不回退重抽。
- 工事损伤按稳定实例写回同一 Profile，与敌人、弹药进入现有合批/原子保存；终局、重试和
  同场恢复保留残架，不恢复满血。没有命中帧同步写盘、Enemy 重建或新导航框架。
- schema **48** / content **61** / Raid rules **29**；新预警 Defense v2，旧预警/活动保持 v1。
  本轮 Windows Debug 全目标重建、1766/1766 CTest（79.67 秒）通过；精确 CI 见 #160。
  不继承基线 CI 作为新提交证据，正常游玩未执行。
- 尚未交付：自动工事磨损、B 工事目录/鼠标放置/收纳维修、日常碰撞投影。购买继续关闭，
  完整闭环后再统一用户正常游玩；本轮没有合并、启动游戏或修改美术/用户存档。

### 2026-09-11 当前覆盖：工事真实拆障运行时，仍为内部检查点

- 同一 Draft #160 从 01c0c7a 继续，main 仍 c6d4597。基线精确 CI `34438941138`
  双平台 1736/1736 通过；#161 已验收合入父分支的状态不变。
- 已接入独立工事受损组件、真实防守抓击/一次失效、玩家/敌人/弹道阻挡及缺口通行。
  工事攻击按稳定 ID 绑定，不转为迟到的玩家伤害；破坏不重建 Enemy 或整张导航。
- 内部 runtime v2 检查点冻结实例/几何并恢复前摇、受损及失效状态，保留旧 v1 编码/hash。
  生产预警/Profile 仍有 v1 门禁，schema/content/Raid rules 仍 47/61/29；没有升级用户档。
- 待接入：预警冻结、Profile 签出/合批耐久写回、自动贡献/磨损及 B 建设鼠标交互。
  B 仍不出售未完成工事，本轮不请求内部 API 验收。最终本地/CI 证据见活动计划及 #160。
- 本轮最终 Windows Debug 全目标与 **1750/1750 CTest（74.58 秒）** 通过，新增 14 项；
  精确 head 双平台 CI 由本次提交触发，不继承上轮 CI 或人工验收结论。

### 2026-09-10 当前覆盖：命中修复已验收合入父分支，恢复工事

- 用户验收 #161@f394058 并授权合并；#161 已普通合入 #160 分支，合并提交 `b179ac9`，
  完整树等于已验收 tip。CI `34349114715` Windows/Ubuntu 各 1725/1725；没有合入未完成的 #160。
  `origin/main` 仍为 `c6d4597`，当前 `codex/base-defense-fixed-positions-v1` 保持 Draft。
- 本轮继续第 1 步内部检查点：固定 slot 安装/换位的纯空间查询、原子实例位置更新、
  同 slot 排他、设施/地面物净空、玩家与设施通路、真实防守适配器的合法来向证明。
  校验窗口保守裁剪并保留跨边界墙体；不改世界导航，不将重校验接入命中/逐帧路径。
- 保存失败、过期预览与动态阻挡重验不得修改实例/材料/敌人；schema/content/Raid rules/Defense rules
  保持 **47/61/29/1**。本轮测试与精确 CI 见 #160 和活动计划，不能继承 #161 测试数为新证据。
- 本内部提交本地全目标构建、11/11 新合同及 **1736/1736 全量 CTest（74.33 秒）** 通过；
  精确提交双平台 CI 另见 #160，不将本地证据写成用户已验收工事。
- **仍未交付可游玩工事**：安装事务是同一 Draft 的内部能力，B 目录尚不出售路障。
  实体/弹道阻挡和真实拆障、v2 预警冻结/恢复、自动防守磨损和 B 鼠标操作仍需联合接入；
  完整闭环后再统一交用户试玩，不要求验收这些内部 API。没有启动游戏或美术生产。

### 2026-09-09 历史：基地周边命中卡顿修复期间，工事暂停

- `origin/main@c6d4597`、#160@a709162 均未被本片合并/改写。当前
  `codex/base-perimeter-hit-checkpoints` **显式 stacked 依赖 #160**；用户现有 schema 47 存档
  不兼容主线 schema 46，因此不为修复降级存档，也不把未完成工事合入主线。
- 根因已复现：Daily 每次敌人 HP/死亡变化同步保存完整 Profile，约 29 ms/次；另有逐帧和逐命中
  不必要的整 Profile 复制。不是每枪重生成地图，旧 Raid 局部击发事务修复仍有效。
- 本片复用既有有界写者：250 ms 合并 Daily 检查点，命中只更新稳定 ID 的小型敌人参与者。
  普通事务/切档/出击/防守先排空写者；退出等待最新检查点。失败冻结并可点击重试，不重建敌人。
  F9 已显示 Daily 保存指标；schema/content/Raid rules/Defense rules 保持 **47/61/29/1**。
- 本片是保存策略变更，不是纯代码搬移；异常终止恢复最近完整检查点，而不是承诺每次命中即时持久。
  本地 Windows Debug 全目标与 **1725/1725 CTest（94.08 秒）** 通过，新增 12 项真实存储/会话/UI 门禁。
  构建、测试和精确提交 CI 以 [修复计划](../exec-plans/active/base-perimeter-hit-checkpoints.md) 及修复 PR 为准。
  正常游玩仍待用户；开发者未启动游戏。固定防御位后续功能暂不推进。

### 2026-09-09 当前覆盖：CI 修复合并，工事第一内部检查点

- #159 经用户授权普通合入 `origin/main@c6d4597`，完整树等于精确双平台通过的 `61187e3`。
  四项 Gameplay 性能测试串行隔离已在主线；没有放宽 25ms 门槛。最终合并 head CI 为 `34331350462`。
- 当前 `codex/base-defense-fixed-positions-v1` 从该主线开始，显式带入 #158 的已确认评审文本；
  #158 本身未合入。当前是工事宏切片的**内部在制检查点**，不是新的可游玩工事交付。
- 已接入工事储备/稳定 ID、建材事务、修复收纳、迁徙与保存；schema v47/content v61，
  Raid rules v29/Defense rules v1 保持。固定位置候选和旧事件迁移已有自动化，证据见实现 PR。
- **尚未完成**：安装与全局通路合同、真实受损/失效、v2 预警冻结、自动防守磨损、B 面板。
  当前购买目录不出售路障；本次不要求用户验收。见 [工事实施计划](../exec-plans/active/base-defense-fixed-positions-v1.md)。
- 当前内部检查点 Windows Debug 全目标、19/19 定向、1713/1713 全量（63.55 秒）通过。
  新增 16 项，包括 24 个真实布局/seed 防守准备组合；精确提交 CI 状态以实现 PR 为准。
  测试过程的一次旧寻回测试进程挂起及复查证据保留在计划中，根因未定，不声称已修复该挂起。

下方“当前门禁”是早先记录，不覆盖此处最新事实：

### 2026-09-09 合并后验证异常：当前门禁

- 用户验收并授权后，#149–#157 全部普通合入 `origin/main@c3eb19c`；完整树等于接受 tip `a68a82e`。
  合并后本地 Windows Debug 1697/1697（64.99 秒），但最终 main CI `34323812733` 的 Windows 有一项百敌性能计时失败（61.185ms > 25ms），不能宣布最终双平台通过。
- 独立分支 `codex/ci-gameplay-timing-isolation` 仅修正四项 GameplayWorld 性能测试漏配串行隔离，保持阈值、场景、断言、游戏代码与存档不变；见 [当前修复计划](../exec-plans/active/ci-gameplay-timing-isolation.md)。已记录二十次未改代码的本地单测通过，不据此抹去 CI 失败。
- 下一玩法的固定防御位/单种路障评审在独立 Draft #158（`370b1ca`），尚未实现；本 CI 修复不叠加该提案。先收束验证门禁，再确认新玩法范围。后续精确 CI 结果记录在修复 PR；开发代理未启动游戏。

下方为合并前收尾记录，不覆盖上述实际主线与 CI 状态：

- **当前唯一状态（2026-09-09）**：用户已验收集成代码 #156@`f16544d`；exact-head CI `34320466698` Windows/Ubuntu 各 1697/1697 通过。本次收尾复查 Windows Debug 构建无需重编，全量 CTest **1697/1697（66.16 秒）**。`origin/main` 仍为 `2ae898a`；#149–#156 全部未合并，验收集成 tip 不等于每个历史 head 均合格。
- **裁决**：Enemy Lifecycle 及已授权后续窄整合可以结束代码重构；没有证据要求继续大范围 Navigation/Combat/Session/Persistence 整合。共享合同、实际调用链、51 项三环境合同实例及保留 Policy 见 [收尾评审](../architecture/GAMEPLAY_FRAMEWORK_CLOSEOUT.md)。
- 本轮 `codex/gameplay-consolidation-closeout` 显式依赖已验收 #156，只同步收尾/架构/路线文档；没有修改游戏代码、内容、测试、资源或存档。下一步为**授权后的整条依赖链合并交付**，不能只合入有已知缺陷的 #149 后结束。新玩法暂不恢复。

### 历史切片记录（不得覆盖上方现状）

- 当前覆盖：用户已验收 #155@`c0b0664`，精确 head CI `34313046163` Windows/Ubuntu 各 1687/1687 通过，未合并。`origin/main` 仍为 `2ae898a`。本轮 `codex/combat-runtime-boundaries-v1` 显式依赖 #155，计划 `active/combat-runtime-boundaries-v1.md`；下方分支状态为历史记录。
- 本轮修复：成功读取/替换 Profile 时失效旧 Base 敌人缓存；成功出击、回营定位、外围换周期清除旧空间弹道与帧结果。普通同周期更新不重建，失败操作不清理；同场防守恢复保留自己的弹道/敌人/消耗。未修改帧顺序、数值、资源或保存政策，schema v46/content v60/rules v29 不变。
- 本地 Windows Debug 全目标构建、149/149 定向、1697/1697 全量 CTest（64.66 秒）通过，新增 10 项注册测试；精确 CI 证据以本轮 PR 为准，正常游玩待验收，未启动游戏。

- 最新覆盖：用户已验收 #154@`b7937c3`；双平台精确提交 CI run `34298809360` 成功，仍未合入。当前分支 `codex/combat-frame-order-v1` 显式 stacked 依赖 #154，计划 `active/combat-frame-order-v1.md`。`origin/main` 仍为 `2ae898a`；下方“待验收/时序未统一”属于之前切片记录。
- 本轮为有意时序变更：三环境均先敌人推进/接触、后射击/死亡清理。Daily/Defense 的枪声在成功击发后通知存活敌人，下一次敌人推进消费，不再重放本帧。防守突破已退休目标不能再算击杀；进入终局后不再产生新射击。保留活动策略、伤害数值和保护，未修改 GameSession、Raid 主循环、存档、资源或 manifest；schema v46/content v60/rules v29 不变。
- 本轮证据：Windows Debug 全目标增量构建、95/95 定向、1687/1687 全量 CTest（73.65 秒）通过，新增 14 项注册测试；精确提交双平台 CI 见新 PR。本轮人工验收仍待完成，不继承 #154 验收。

- 最新覆盖：当前为 `codex/enemy-contact-protection-v1`，显式 stacked 依赖 #153@`d70f861`。#153 精确 head Windows/Ubuntu CI run `34247787567` 成功，但仍是 Draft、未合并、待正常游玩验收；“继续计划”不记为验收。`origin/main` 仍为 `2ae898a`，下方切片状态为历史记录。
- 当前计划 `active/enemy-contact-protection-v1.md`：Daily/Defense/Raid 复用单敌人的接触—Grab/Bite—消费—0.25 秒保护解析。保护中的合法接触不排队、不追加控制；Daily 增加接触遮挡并同帧消费 Bite。这些是明确的行为修正，不是纯代码移动。帧顺序、安全区、波次、寻路策略、失败与保存政策保持；schema v46/content v60/rules v29 不变。自动化与用户验收分别记录在该计划及新 PR。
- 本轮证据：Windows Debug 全目标构建、89/89 定向和 1673/1673 全量 CTest（72.82 秒）通过，包括现有串行防守性能门禁。新增 16 项注册测试，另显式更新三环境 Grab 时序合同；精确 head CI 见 PR，正常游玩待验收。

- 本轮本地证据：敌人受伤事实切片 Windows Debug 全构建、12/12 新三环境测试及 1657/1657 全量 CTest（58.68 秒）通过；精确提交 CI 与正常游玩验收以新 PR 为准，不能继承上轮验收。

- 最新覆盖：#152@a98090a 已获用户验收，Windows/Ubuntu CI run `34188461419` 成功，仍未合入。当前显式 stacked 分支 `codex/enemy-incoming-damage-contract-v1` 基于 #152；活动计划 `active/enemy-incoming-damage-contract-v1.md`。新代码只统一敌人已命中后的受伤事实，不调整帧顺序或存档。
- 有意修正：外围攻击不再使用旧 1/2 HP 与统一 Scratch 推断；正式抓伤为 12 躯干伤害、咬伤为 18 头部伤害，并传递正确伤势与防具参数。数值为护甲结算前值。现有 0.25 秒保护、Base 救回与防守软失败不变，旧快照不重写。本轮测试/验收另行记录。

- 导航刷新选择切片本地证据：Windows Debug 全目标构建、1645/1645 CTest（55.63 秒）通过；新增 8 项选择合同。双平台精确提交 CI 和用户可见行为验收以新 PR 为准，不视为主线完成。

- 最新覆盖：#151@47c696b 已获用户验收，精确 head 双平台 CI run `34186994633` 成功，尚未合入。当前显式 stacked 分支 `codex/navigation-refresh-contract-v1` 基于该提交，仅提取 Raid/Defense 有界导航轮询选择；目标结果使用稳定 ID，保留各自刷新策略、Daily 直线转向及原更新顺序。活动计划：`active/navigation-refresh-contract-v1.md`。

- 2026-09-08 最新覆盖：用户已验收 #150@2ff1a69 的 Enemy Lifecycle 修复；#149/#150 仍未合入。当前独立 stacked 分支为 `codex/enemy-combat-contract-v1`，显式依赖 #150，活动计划为 `active/enemy-combat-contract-v1.md`。下方原生命周期“待验收”记录为历史证据。
- 本轮统一新生成普通感染者为 `enemy.infected.basic`（12 HP），Base/Defense/Raid 共用领域命中反馈投影；旧快照血量、伤势、死亡不重写。schema v46/content v60/rules v29。Windows Debug 全目标构建、155/155 定向及 1637/1637 全量 CTest 通过；本轮精确提交 CI 与正常游玩证据见新 PR，不视为已合入或已验收。

- 当前主线：`origin/main@2ae898a`，PR #148 经用户明确授权普通合并；首局整备—撤离—回营再出发已进入主线，schema v45/content v59/rules v29。既有阶段的验收证据保留。
- 当前开发分支：`codex/gameplay-framework-consolidation`，显式 stacked 依赖未合入的 #149@`d43bab0`；不改变 #149 原分支，不自动合并。
- 当前活动计划：`doc/exec-plans/active/gameplay-framework-consolidation.md`。新玩法暂停，仅 Phase A/B（复现与 Enemy Lifecycle）；完成后强制评估，不默认继续 Navigation/Combat Pipeline/Session/Persistence 全部重构。以下 #149 记录为依赖背景，不代表验收通过。
- 生命周期整合已完成本地 Windows Debug 全目标构建与 **1625/1625 CTest（54.60 秒）**；含 12 个真实三环境合同实例、死亡保存/拒绝恢复及跨活动目标失效回归。精确提交 CI 记录在 stacked PR；用户正常游玩仍待验收，不将本地证据写成主线完成。
- PR #149 最新返工为“入侵敌人像无敌、核心旁连射后停滞”：冻结防守前同步当前装备武器，新增波次恢复普通 Raid 的 12 HP 标度；听到但看不到玩家时保留防线目标，修复贴墙碰撞与导航额外间距不一致。旧事件仍保留冻结血量和射击检查点，不重抽/重置存档；旧 100 HP 波次需结束后再验证新波次平衡。回归与精确提交 CI 证据见活动计划/PR，仍待用户复验。
- PR #149 正常游玩发现预警按钮无响应、无法收起；本轮仅返工该交互。修复 Base 预警的事件路由，增加 Esc/X 收起和 F6/提示条重开；倒计时前后均通过同一领域命令处理选择，点击不穿透为射击，已有自动预设保持。新增 14 项无窗口输入/双语测试，最新完整验证见活动计划和 PR；此前 1587 项证据不是本轮结果。仍待用户复验，不合并、不启动下一片。
- PR #149 Windows Debug 全目标及全量 CTest **1587/1587** 通过（48.46 秒，新增 70 项回归）；性能测试隔离串行运行，1000 资产/1000 阻挡/16 敌人真实战斗和后台保存 copy P95/P99 为 0.903/2.366ms。首轮 Windows CI 复制性能失败后已优化稳定 ID 节点与完成缓冲复用，未放宽门槛；最终精确 head 双平台 CI 见 PR，不将无 SDL 压力数据写成可见全帧/发行硬件承诺。
- Home Region 开局可玩闭环收尾。已确认的玩家实时防守 v1 在 PR #149 中实施：3 波有限现有感染者、最多 16 名活动敌人、6 次突破软失败；共享自动防守结果，真实个人消耗、无个人资产全损。安全核心仍受保护，只有事件标记的缓冲走廊开放交战。
- PR #148 的 Windows Debug 全目标、93/93 定向与 1517/1517 全量 CTest（68.97 秒）已通过；head `a1e672c` 双平台 CI 同为 1517/1517，run `34083273520`。PR #149 新代码使用 schema v46/content v59/rules v29；具体门禁证据见活动计划，不能继承旧测试数作为本轮通过证据。
- Week29 `codex/week29-combat-feedback-and-attack-animation@6c23389` 未进入 main；正式 Grab/Scratch/Bite 图像及所有新正式美术生产继续暂停。用户于 2026-08-21 仅授权当前 ArtWorkbench P0 音效包接入。

## 当前产品里程碑

Core Extraction Alpha 到 Regional Operations 基础阶段、“首张可玩随机大地图”阶段、Content Beta 成长闭环、Home Region 外围探索、基地愿望 v2、愿望—远征研判、开局选址及首局到第二次出击均已进入主线。Home Region 起步阶段已收尾；实时防守及玩法框架整合已在 #156 集成版本通过用户验收，尚未进入主线。本轮完成代码收尾评审，主线交付仍待合并授权。

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
20. **Base 资源分配与基础需求 v1（历史）**：PR #78 把成功带回的新 Loot 放入独立待分配区并已以 `ba8283f` 进入 main；用户于 2026-08-25 明确废弃该返还行为，PR #87 已迁移为成功撤离保持原位置，BaseIntake 只兼容旧档。
21. **Base 世界时钟与每日需求 v1**：PR #79 建立 Base/Raid 共享的权威分钟时钟，把四项需求迁移到每日 00:00 幂等结算；暂停、模态页、主菜单、结果页和离线时间不推进，未结算 Raid 的时间随出击前存档回滚。已通过 exact-head CI 和用户正常游玩验收，以 merge commit `5d2a11a` 进入 main。
22. **Raid 往返行动耗时 v1**：PR #80 为三图增加版本化出发/正常返程/失败归队时间，出击前显示抵达昼夜预览；旅行、有效 Raid 时间与跨日需求作为同一活动事务提交，异常退出精确回到出发前时钟和资源。已通过 exact-head CI 和用户正常游玩验收，以 merge commit `defaac0` 进入 main。
23. **Base 枪匠全面维护服务 v1**：PR #81 已通过 exact-head CI 与用户正常游玩验收，并以 merge commit `ace7c69` 进入 main。
24. **Base 周期愿望与物资提交 v1**：PR #82 已通过 exact-head Windows/Ubuntu CI 与用户正常游玩验收，以 merge commit `eca7d62` 进入 main。
25. **Base 运营状态与即时枪械维护 v1**：PR #83 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `20d9f48` 进入 main。
26. **Base 付费医疗服务 v1**：PR #84 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `c01d431` 进入 main。玩家只支付货币即可立即恢复生命并清除流血，不消耗个人医疗物、不推进世界时间。
27. **Base 居民、床位与睡眠 v1**：PR #85 新增宿舍设施、8 名普通居民/10 个床位的迁移默认值、按人口计算的每日口粮和最多 12 小时的原子休息事务；已通过 CI 和用户验收并以 `2377035` 合入 main。
28. **Raid 普通幸存者安全转移 v1**：PR #86 为三张固定图加入一次性普通幸存者点；连续按住 F 2 秒后立即幂等接纳，后续死亡、主动退出或异常关闭均保留该人口事实，其余 Raid 状态仍回滚。已通过 CI 与用户正常游玩验收并以 `ee9ba48` 合入 main。
29. **Base 宿舍扩建与分类自动供给 v1**：PR #87 允许从统一自有资产显式加工建材并完成宿舍 1→2 级项目；成功撤离保持全部随身物原位置，食物/医疗/娱乐/安全菜单按物品定义保存自动供给授权，只有每日需求不足时才消费。已通过 exact-head CI 和用户正常游玩验收，以 `1be94bf` 进入 main。
30. **Base 居民伤病与医疗所治疗 v1**：PR #88 让 Ashworks 救援接纳一名受伤普通居民；医疗所从玩家明确授权为医疗供给的基地可访问自有物品中预览并原子消费准确数量，经过权威世界时间后恢复居民。玩家付费医疗仍是独立的货币即时服务。已通过 exact-head CI 与用户正常游玩验收，以 `987dc6b` 进入 main。
31. **Base 基础制造队列 v1**：PR #89 为损坏工坊提供一个生产槽；以 1 个废旧零件、1 个损坏电子元件和 1 名健康劳动力经过 6 小时制造一件真实武器维护包。已通过 exact-head CI 与用户正常游玩验收，以普通 merge commit `194f910` 进入 main。
32. **Base 正式士气与周期事件 v1**：PR #90 已通过 exact-head CI 与用户正常游玩验收，以普通 merge commit `1af0e56` 进入 main。
33. **Base 聚合岗位、专业人口与设施升级 v1**：PR #91 已通过 exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `12a2fa6` 进入 main。
34. **区域地图与对局情报 v1**：PR #92 已通过 exact-head Windows/Ubuntu CI，并按用户连续交付授权以普通 merge commit `bf8baf3` 进入 main。三图出击板提供难度警告、交通图/物资清单/敌情档案购买与选择，部署原子消耗并冻结权限；Raid 中 `M` 打开不暂停战术地图，显示本局探索迷雾、已发现撤离点及所选粗粒度情报。用户正常游玩验收留待后续集中进行。
35. **程序化室外空间基础 v1**：PR #93 已通过本地 1033/1033、exact-head Windows/Ubuntu CI 和用户集中验收，以普通 merge commit `1404b41` 进入 main。第四图 `Frontier Exchange` 使用稳定 PCG32 生成室外掩体并冻结 schema v21/content v29 布局、连通性与确定性回退合同。
36. **独立室内空间 v1**：PR #94 已通过本地 1039/1039、exact-head Windows/Ubuntu CI 和用户集中验收，以普通 merge commit `62ebd8a` 进入 main。`Frontier Exchange` 交换站办公室使用稳定空间 ID、独立尺寸/障碍/敌人/Loot 和 F 入口/返回 Socket；占位表现仅使用双语文字与代码几何。
37. **特殊地点随机合法放置 v1**：PR #95 已通过本地 1043/1043、exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `d2ceb59` 进入 main。content v31 为交换站办公室声明 6 个候选位置，Deploy 以独立 PCG32 流过滤本局动态冲突并确定性选择，最终坐标继续冻结在 schema v22。
38. **特殊地点发现与战术地图投影 v1**：PR #96 已通过本地 1048/1048、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `de3402c` 进入 main。玩家接近入口约 145 世界单位后才在本局显示精确世界入口和 `M` 地图特殊地点标记；同帧进入会先记录发现。
39. **建筑内部图永久情报 v1**：PR #97 已通过本地 1058/1058、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `a7b3cc2` 进入 main。content v32 为交换站办公室声明 180 货币的一次性内部图，schema v23 保存永久空间 ID 授权并冻结到 pending Raid。
40. **空间战术可靠性 v1**：PR #98 已通过 Windows Debug、1072/1072 CTest、exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `95fcd23` 进入 main。当前空间 LOS、最后已知位置、确定性绕障、贴墙合法接近点、成功实弹击发枪声刺激和近战遮挡均已接受。
41. **第二个代表性地点 v1**：content v33 在 `Frontier Exchange` 增加独立 `Freight Service Bay`，rules v15 冻结两个地点并保留 v12～v14 首办公室 pending Raid 兼容。多入口投影、返回点可达锚点、独立 Actor/Loot/内部图和双语代码占位均已通过用户正常游玩验收；多人追击热点修复也完成复验。PR #99 已以普通 merge commit `1d2fea1` 进入 main。
42. **Raid World 可扩展性能基础 v1**：PR #100 已通过 Windows Debug、1097/1097 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `d7c231b` 进入 main。公平轮转、敌人近邻格、每空间静态障碍索引、双导航后端、结构化压力门槛和双语 F9 性能面板成为接受基线。
43. **多敌人追击与攻击意图隔离**：PR #102 已通过 Windows Debug、233/233 定向回归、1108/1108 完整 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，以普通 merge commit `1c62064` 进入 main。最多 10 名敌人可同时攻击，超额成员继续施压并稳定轮转；0.25 秒受伤保护避免同一瞬间连续跳血。
44. **失物记录与行动老化 v1**：PR #101 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `7185d55` 进入 main。死亡/主动退出迁移携带资产树，异常退出继续回滚，schema v24/content v34 保存独立记录、老化窗口与 RaidResult 关联。Base Raid Gate 提供双语代码占位记录页和出击前到期二次确认。
45. **NPC 寻回任务与 Raid 自行寻回 v1**：PR #103 已通过 exact-head CI 和用户统一正常游玩验收，以普通 merge commit `f31d91a` 进入 main。schema v26/content v35 支持整单 NPC 委托与来源地图内自力寻回，两条路径共享唯一资产所有权。
46. **区域路线与轻量哨所基础**：PR #104 已通过 exact-head CI 与用户统一正常游玩验收，以普通 merge commit `dc19745` 进入 main。content v36/schema v27 建立稳定区域节点、路线和哨所 ID；满员 Old Service Relay 把 Riverside/Industrial/Frontier 单程最短时间从 90/150/210 分钟降为 55/95/125 分钟。
47. **哨所中断与恢复 v1**：PR #105 已通过 exact-head CI 与用户正常游玩验收，以普通 merge commit `cf555a1` 进入 main。content v37/schema v28 数据化 3 次安全捷径行动阈值和 Riverside 清剿地图；完成目标并成功撤离才原子恢复，失败保留中断，异常退出恢复出击前状态。
48. **区域基地候选点清剿 v1**：PR #106 已通过 exact-head CI 和用户正常游玩验收，以普通 merge commit `e6721e4` 进入 main。content v38/schema v29 发布 Greyline 与 Ashworks 候选点、清剿解锁、第二标准前哨和路线收益。
49. **唯一主基地迁徙 v1**：PR #107 已通过 exact-head CI 和用户正常游玩验收，以普通 merge commit `d035181` 进入 main。content v39/schema v30 发布唯一科技核心、五类稳定设施、厨房/净水建设、设施 Installed/Reserve 位置和两处基地的迁徙路线；迁徙原子推进 12 小时并保持 Stash、装备、人口和资产唯一所有权。
50. **区域基地独特设施 v1**：PR #108 已通过用户正常游玩验收，以普通 merge commit `bf0d383` 进入 main。content v40/schema v31 为 Greyline 与 Ashworks 发布类型化地点能力；Ashworks 重型物流工坊只在该地点为当前主基地且普通工坊已安装时缩短新制造订单。
51. **基地威胁预警与自动防守 v1**：content v41/schema v32 保存 Raid、人口和地点三类威胁、三分钟 Base 预警、首次手动确认及后续预设。公共 Security 决定成功或软失败；成功增加建材并进入 7 日安全期，软失败只损失有限公共储备和至多一名未被占用的普通居民，进入 12 日安全期，不损坏个人资产。满威胁但仍受安全期保护时持续显示 `QUEUED`、剩余保护时间和“仍可出击”。PR #109 已通过用户正常游玩验收，并以普通 merge commit `fab9f32` 进入 main。
52. **基地外围清剿 v1**：content v42 为每个主基地地点显式声明外围清剿地图和 40 点减值；威胁达到 40 后可从区域页发起，清空初始敌人并撤离才原子减值，提前撤离仍合法但不减值。schema v33 保存任务快照和结果实际减值，并把旧档隐藏的来源总量溢出确定性归一到共享 100 点容量。PR #110 已通过 Windows Debug 全目标、完整 CTest 1223/1223、exact-head Windows/Ubuntu CI 与用户正常游玩验收，并以普通 merge commit `7bd3b02` 进入 main。
53. **Frontier Exchange 超大主题分区随机地图 v3**：PR #111 已通过完整自动化、exact-head CI 和用户正常游玩验收，以 merge commit `636a40b` 进入 main。content v44/layout v3/schema v35 将地图扩展为 25600×14400，并冻结八个连续主题分区、三个固定地标、程序道路、五类地表、真实比例环境物件和全部既有玩法锚点；同时交付区块投影、分层导航、双语加载画面、F10 开发面板、准星边界和大地图帧顿挫修复。
54. **Frontier Exchange 资源点生态 v1**：PR #112 已通过完整自动化、exact-head CI 和用户正常游玩验收，以 merge commit `85f90ff` 进入 main。content v46/layout v4/schema v36 冻结普通、高价值和地点专属资源点、36～48 名初始敌人、真实加载阶段和鼠标 F10；连续射击和背包拖拽热路径不再复制整份超大地图 Profile。
55. **Frontier Exchange 遭遇与部署生态 v1**：PR #113 已通过完整自动化、exact-head CI 和用户正常游玩验收，以 merge commit `f095022` 进入 main。content v48/schema v37/rules v26 冻结巡逻、守点和伏击群组、同组声响响应及 1200 世界单位出生保护区；Raid 拾取和局内库存只复制、校验可修改的资产参与者，不再逐次扫描冻结超大地图。
56. **Frontier Exchange 既有消费者整合 v1**：content v49 保持 schema v37/layout v4，把 Ashworks 外围清剿指向 `Frontier Exchange`；`GameSession` 从冻结锚点建立救援、失物缓存和高危控制的类型化战术地图目标。救援和玩家主动选择的失物缓存属于开局 Briefed，高危控制需要探索；外围清剿状态投影为 ACTIVE/SECURED，最终减值仍由唯一 Settlement 提交。v48/v47 存档继续读取且不重生成既有 Raid。PR #114 已通过自动化、exact-head CI 和用户正常游玩验收，以 merge commit `7be91e8` 进入 main。
57. **Frontier Exchange 物资身份与 Loot 内容包 v1**：content v50 保持 schema v37/layout v4，为六类资源点提供独立 Loot 表，新增九种稳定生活、工业和高价值物品定义及双语占位。新 rules 27 冻结资源点—Loot 表绑定；rules 23～26 pending Raid 继续按旧映射读取，不重抽既有 Loot。新物资复用现有 AssetRegistry、Base 自动供给、建设加工和回收事务。PR #115 已通过用户正常游玩验收，并以 merge commit `6ab1724` 进入 main。
58. **Frontier Exchange 高危危机生态 v1**：content v51/schema v38/rules v28 每局从道路汇流、工业缺口和货运封锁中确定性冻结一个危机、合法目标资源点、精确压力来源、波次参数与主题高级 Loot。压力敌人会向危机目标汇聚，HUD/F10/战术地图提供可感知反馈。PR #116 已通过 Windows Debug、1296/1296 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，并以普通 merge commit `e285a2b` 进入 main。
59. **Content Beta 代表性枪械与口径梯度 v1**：建立六种职责武器、9×19/5.45×39/7.62×51 三个口径及每口径两档普通弹；保留现有 Pistol/Rifle/Ammo9mm 与旧 pending Raid，兼容性由领域定义而非 UI 名称判断。content v53 开发目录覆盖会把全部 42 种已发布定义一次性放入新档 Stash，并为旧档执行同一幂等补齐；满仓时零修改但不阻止读取存档。PR #117 已通过 exact-head CI 和用户正常游玩验收，并以普通 merge commit `8164e66` 进入 main。
60. **Content Beta 防具、胸挂与背包梯度 v1**：content v54 新增两档头盔、两档躯干护甲、两种胸挂和两种背包；客户端按内容定义绘制任意胸挂分区和连续背包格，并显示双语重量、防护、材料、耐久与容量。Stash 单向扩为 24×16，content v53 继续读取，v54 首次补齐扩展为当前全部 50 种定义。PR #118 已通过 exact-head CI 和用户正常游玩验收，并以普通 merge commit `e24da4c` 进入 main。
61. **Content Beta 战利品来源、经济与补给可持续性 v1**：content v55/rules v29 为三张固定地图和三类 Frontier 高危危机建立不同产出表；固定供应改为从 `market_buy_price` 定义派生，工坊增加三口径标准弹药批次和鼠标配方选择，内容加载同时拒绝制造—回收正收益循环。PR #119 已通过 Windows Debug、1324/1324 CTest、exact-head CI 与用户正常游玩验收，并以普通 merge commit `ecd8288` 进入 main。
62. **Content Beta 成长闭环与配装准备度 v1**：content v56 发布轻型搜刮、均衡作战和重型专精三种稳定职责；SDL 无关 Projection 只读取真实装备、随身兼容弹药和内容定义，并在 Raid Gate 显示 READY、具体缺口、建议地图和高危提示。PR #120 已通过用户正常游玩验收，并以普通 merge commit `f48b954` 进入 main。
63. **Home Region 新档起步与首次整备 v1**：普通新档使用有限 starter 资产，完整发布目录改由 Base/Raid F10 显式开发命令提供；content v57 让基础护甲与轻甲都成为轻型职责的合法选择，环境目标消费同一职责准备度，不建立独立教学关或强制门禁。PR #121 已通过 CI 与用户正常游玩验收，并以 merge commit `71a3c08` 进入 main。
64. **Home Region 大地图与基地板块 v1**：PR #122 已通过 CI 与用户正常游玩验收，以 `62e464e` 进入 main。BaseWorld 扩展为 12800×7200 安全室外空间；当前主基地作为固定内部结构板块放入确定性分区、道路、地表和环境物布局。
65. **Home Region 共享战斗能力 v1**：提取 Base/Raid 共用的 SDL-free 瞄准、散布、后坐力、逻辑弹道与表现投影；基地支持真实武器切换、弹药/耐久消耗和故障，但保持无敌人、无撤离、无 Settlement。Tab 仅在玩家完整位于基地板块内时连接 Stash，离开后退化为个人随身页；`M` 可打开只读 Home Region 全静态地图。PR #123 已通过用户正常游玩验收并以 merge commit `9106126` 进入 main。
66. **Home Region 地面资产 v1**：基地个人页可把随身或仓库根资产放到当前基地地面；世界与地图显示真实根资产，近距离可拾回，容器子树保持唯一所有权。schema v39 保存基地站点和世界坐标，旧档不生成地面资产。PR #124 已通过 Windows Debug、1354/1354 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，并以 merge commit `17a3514` 进入 main。
67. **Home Region 世界容器交互 v1**：地面容器可在正确站点和近距离内打开；右侧显示真实分区，并通过受限库存事务与随身容器双向拖拽或快速装备。地面容器页不连接 Stash，也不为特殊物品动作绕过访问边界。PR #125 已通过用户正常游玩验收，并以 merge commit `03028bb` 进入 main。
68. **Home Region 基地建设面板与可放置储物箱 v1**：`B` 打开底部紧凑建设栏，“购买 / 已有”页复用同一内容驱动设施卡片，滚轮切换建设视野；点击卡片进入绿/红虚像放置，已放置设施在世界中左键选择、右键打开功能页/移动/回收。PR #126 已通过用户正常游玩验收，并以 merge commit `50879bc` 进入 main。
69. **Home Region 建设镜头控制 v1**：建设模式和设施放置状态共用不写入存档的客户端镜头；WASD 平移、按住右键拖动画面，短按右键仍打开设施菜单，退出后恢复玩家跟随。五档缩放共用同一投影命中合同；七类固定设施可从建设界面打开原有功能页。PR #127 已通过 CI 和用户验收，以 `b990db3` 进入 main。
70. **Home Region 设施统一管理与状态展示 v1**：选择固定设施显示等级、运行/储备状态、人员与当前任务，并可打开已有功能页。PR #128 已通过 CI 和用户验收，以 `cb73ce0` 进入 main。
71. **Home Region 设施统一快速操作 v1**：无需额外选择的安排人员、设施升级、现有制造订单处理、居民治疗和自动补员可从管理卡片执行；需要配方或时长选择的操作仍在完整功能页。PR #129 已通过 CI 和用户验收，以 `722a20c` 进入 main。
72. **Home Region 基地运营总览与设施定位 v1**：建设面板集中显示待领取产物、活动升级/制造/治疗和缺员；点击条目把瞬态建设镜头定位到对应设施并选中管理卡片。PR #130 已通过用户验收并以 `ee44d52` 合入 main。
73. **Home Region 基地运营完成通知与设施标记 v1**：升级、制造和居民治疗完成后提供短时非阻塞通知；Raid 内完成事实延迟到 Settlement 成功后发布，建设面板突出对应固定设施。PR #131 已通过 CI 和用户正常游玩验收，并以 `e7590d9` 合入 main。
74. **Home Region 核心设施空间化与持久布局 v1**：仓库、医疗所、宿舍和工坊使用稳定设施 ID 与站点归一化位置；建设模式可移动已安装设施，非法重叠、越界或保存失败均零提交。schema v40 保存每个已发布基地地点的独立布局，schema v39 及更早版本确定性迁移到原位置。PR #132 已通过 CI 与用户正常游玩验收，并以 merge commit `0b5be88` 进入 main。
75. **Home Region 储备设施空间部署 v1**：储备设施不再绘制、碰撞或参与近距交互；建设面板“已有”页以绿/红虚像选择新址，并用单一事务同时提交 Installed 状态、任务恢复时间和站点布局。PR #133 已通过 CI 与用户正常游玩验收，并以 merge commit `411aaed` 进入 main。
76. **Home Region 厨房/净水设施空间化建设 v1**：首次建设完成后进入 Reserve 并补齐站点布局；玩家从建设面板选址部署后，设施进入世界并复用现有物资分配与需求页面。schema v41 显式兼容旧 v40 已建设施。PR #134 已通过 CI 与用户正常游玩验收，以 merge commit `2ec99c2` 进入 main。
77. **Home Region 基地建设项目目录 v1**：`B` 购买/建设页统一显示已发布工程和可购买设施，工程卡片展示等级、建材、劳动力、工期与领域状态并支持开始/取消；购买与已有页均使用稳定分页，区域迁徙页只负责跳转目录，不再直接提交厨房工程。PR #135 已通过 CI 与用户正常游玩验收，以 merge commit `c37fe32` 进入 main。
78. **Home Region 设施入口与作业区 v1**：活动设施发布确定性南向入口、近距交互区和建设作业净空区；世界 `E` 交互、建设放置阻挡、镜头定位和 Home Region 地图入口标记消费同一 SDL-free 几何。PR #136 已通过 CI 与用户正常游玩验收，以 merge commit `d1c9b35` 进入 main。
79. **Home Region 设施服务点状态反馈 v1**：入口服务点从现有设施管理领域投影可用、工作中、产物待取、缺员和阻塞状态；世界标记、近距 `E` 提示及 Home Region 地图使用同一状态，不建立第二套任务事实。PR #137 已通过 CI 与用户正常游玩验收，以 merge commit `95f830c` 进入 main。
80. **Home Region 设施作业点空间合同 v1**：五类空间化核心设施发布类型化、确定性的仓储装卸点、医疗床位、宿舍床位、厨房加工点和工坊工作台；活动任务突出对应点位。PR #138 已通过 CI 与用户正常游玩验收，以 merge commit `5da9591` 进入 main。
81. **Home Region 聚合岗位世界占位反馈 v1**：Workshop/Medical 的既有聚合岗位在对应作业点显示缺员、闲置、工作中或暂停；只读投影复用真实专业分配与任务状态。PR #139 已通过 CI 与用户正常游玩验收，以 merge commit `abe2886` 进入 main。
82. **Home Region 设施岗位现场操作 v1**：Workshop/Medical 作业点可直接选中设施并提供分配、清除和自动补员；所有按钮消费既有领域查询与持久化命令。PR #140 已通过 CI 与用户正常游玩验收，以 merge commit `0f74a67` 进入 main。
83. **Home Region 聚合居民空间状态 v1**：DormitoryBunk 显示聚合居民、健康/受伤、床位、可用/已分配/建设占用，并提供宿舍打开与自动补员入口。PR #141 已通过 CI 与用户验收，以 `2f05626` 进入 main。
84. **Home Region 基地物资流现场状态 v1**：仓库装卸点与厨房处理点显示统一库存、供给授权、资源池和预计短缺。PR #142 已通过 CI 与用户验收，以 `b1194a1` 进入 main。
85. **Home Region 作业状态信息层级与快速定位 v1**：作业点随缩放显示不同信息密度，运营总览支持状态筛选、分页和设施定位。PR #143 已通过 CI 与用户验收，以 `60c83fa` 进入 main。
86. **Home Region 外围探索与安全边界 v1**：安全核心外加入过渡缓冲与可探索外围；外围使用有限感染者、低阶地面 Loot、持久周期快照和本地返回/救回结果，不创建 Raid、撤离或 Settlement。PR #144 已通过 CI 与用户正常游玩验收，以 `674f65e` 进入 main。
87. **Base 愿望与资源取舍 v2（PR #145 已合入）**：五日周期按冻结人口生成一至三个愿望；玩家显式选择当前愿望与可贡献资产，预览贡献/超额后一次完成，完成与错过只进入既有士气账本。
88. **愿望—远征研判闭环（PR #146 已合入）**：关注愿望、合法情报下地图来源研判、冻结关注与真实带回摘要、分配页相关物筛选；不修改掉率、不自动交付。
89. **开局选址与首座基地建立（PR #147 已合入）**：未建家状态、三候选地块、唯一核心原子建立、旧档保留旧布局；建立后继续消费原有基地与 Raid 系统。普通合并提交 `805fd8b`。
90. **首局整备—撤离—回营再出发（PR #148 已合入）**：轻量供弹查询、可关闭上下文提示、冻结阶段/撤离解释、真实结算确认和第二局指导退场；自动化通过后按用户合并指令以 `2ae898a` 进入 main。

每个宏切片内部按领域、服务、客户端和证据形成可回滚提交，但不再为单个技术边界中断玩家功能交付。人工验证统一放在自动化和 CI 之后，由用户执行。

## 已接受能力

- `MainMenu → Base → Raid → RaidResult → Base` V0 流程和进程内多局会话。
- RL-INV-001/002/003：原子交换、Ctrl/Shift 锁定数量拖拽、同定义堆叠合并与 60 发上限。
- `ShotCommand → ShotResolution → HitResult` 窄边界；生产路径已使用非场景实体的 `LogicalBallisticFlight`，冻结本发并连续扫掠目标。
- 四个生产库、唯一业务源码编译所有权、强类型 DefinitionId、版本化 JSON ContentRegistry 和仓库本地 nlohmann-json overlay。
- Persistent Base 的长期 Profile、唯一 AssetRegistry、Stash/三槽配装、固定经济/救济、可行走三设施 Base、首次环境目标链和原子存档。

## 已接受的 Extraction Loop

- `ProfileState::AssetRegistry` 在 Base、Deploy、Raid Loot 与 Settlement 全程唯一拥有资产；装备根、容器子资产、已安装弹匣和 Raid 地面位置均使用稳定实例 ID。
- content v14 提供三张可选择的固定 Raid 地图；每图各有 3 组出生/撤离配对、3 组 4～6 敌人部署、10 个三路线 Loot 插槽、独立障碍和冻结的往返行动耗时。每局冻结 6～9 个有效 Loot，PCG32 命名随机流和所选 MapDefinitionId 写入 pending Raid 快照。
- schema v2 保存当前 HP、弹匣有序弹药、枪膛、Settlement 幂等记录和最近 RaidResult，并能读取旧 pending Raid；新生产 Deploy 不再把运行中 pending Raid 覆盖到磁盘。schema v1 可显式迁移。
- Base 与 Raid 共用按住拖拽库存交互；格子移动/交换/堆叠/配装均由领域预览和命令提交。Base 可将弹药拖到弹匣即时压弹、将弹匣拖到武器安装并按条件自动上膛；Raid 可拖动弹药到弹匣执行 0.2 秒/发的可中断压弹，也可拖动指定弹匣到武器并执行 2 秒换弹。
- 卸弹、显式上膛和 Medkit 使用物品右键情境菜单，不再依赖 `FILL MAG / INSTALL / CHAMBER / USE MED` 等验收按钮。Base 卸弹即时回到 Stash；Raid 弹匣卸弹为 3 秒可中断动作，完成时原子写入背包或胸挂通用格。`F`/`Ctrl+右键` 保留为 Base 快速转移捷径；可穿戴物在对应栏位为空且领域查询合法时优先快速装备，否则沿用容器转移。
- 玩家为 100 HP；Medkit 每件 3 次、每次恢复最多 30 HP，Raid 内治疗 5 秒且中断不消耗。
- 生产 Raid 的常规阶段为 180 秒；归零只进入无终局倒计时的持续高危，不产生时间失败。E 拾取真实 Loot，随身库存可移动和整理，打开时禁止射击/换弹/开始治疗但允许普通移动。
- 3 秒撤离成功保留合法随身资产、HP 以及每件资产的精确装备槽/容器格位；未来载具货物沿用同一位置保持合同。死亡和主动放弃仍是当前出击全损并恢复 100 HP，但当前分支不再删除实例，而是把携带树根迁移到不可直接使用的独立失物记录。关闭程序或异常退出不会结算、不会创建/老化记录，重开后加载出击前的完整 Profile；正式成功/失败结果仍使用唯一 Settlement ID 幂等提交。
- Raid 世界支持 Shift 奔跑，速度为普通移动的 1.5 倍；当前不引入耐力条、负重或复杂移动消耗。
- RaidResult 显示结果、成功带回物和货币变化；失败只提示失物记录已建立，详细资产在 Base 独立记录页查看。生产 Alpha 路径不再使用 V0 柜体、无限弹或 Timeout 结算。

## 当前自动化证据

- PR #119 已经用户正常游玩验收，并以普通 merge commit `ecd8288` 进入 main。
- PR #122 已通过 Windows Debug、1344/1344 CTest、exact-head Windows/Ubuntu CI 和用户正常游玩验收，以 `62e464e` 进入 main。PR #123 的 SDL-free `WorldShootingRuntime`、Base 真实武器事务、空间化 Stash 访问、Home Region 地图与客户端输入/表现接入也已通过 1348/1348 完整 CTest、exact-head Windows/Ubuntu CI 与用户正常游玩验收，并以 merge commit `9106126` 进入 main。开发代理未启动游戏。
- PR #114 的类型化目标投影、Ashworks 随机外围清剿和 Frontier 自力寻回冻结锚点已通过用户正常游玩验收并进入 main。当前物资身份切片已完成 Windows Debug 全目标构建、focused tests 和 1287/1287 完整 CTest；开发代理未启动游戏，Draft PR、exact-head CI 与用户正常游玩验收仍待完成。
- 当前多敌人攻击意图修复已完成 Windows Debug 全目标、233/233 定向回归和 1108/1108 完整 CTest；32 敌人压力约 119 ms、最慢约 1.45 ms，100 敌人压力约 172 ms、最慢约 1.96 ms。开发代理未启动游戏。
- Windows Debug 当前树全目标构建成功，`Project_Raidline.exe` 已生成但未由开发代理启动。
- PR #89 已通过 exact-head Windows/Ubuntu CI 与用户正常游玩验收，并以普通 merge commit `194f910` 进入 main。
- PR #90～#105 均已进入 main。当前候选点清剿分支已完成候选点/schema、清剿 Raid、第二前哨/路线收益三个玩家闭环步骤；相关领域、存档、生命周期、GameSession 和双语回归 233/233、Windows Debug 全目标、完整 1169/1169 CTest 与 exact-head Windows/Ubuntu CI 通过，开发代理未启动游戏。
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
- Raid 中按 `F10` 打开运行时开发面板；其中武器参数仍使用上下选择、左右微调、Shift 大步长、R 重置，`F` 切换只读战术地图迷雾表现，`I` 切换无限弹药。武器覆盖按当前实例隔离；迷雾和无限弹药在每个新 Raid 分别恢复为启用与关闭。三者均不修改 ContentRegistry 或 manifest；武器覆盖和迷雾不进入领域状态，无限弹药只在可丢弃候选 Profile 中验证循环供弹并拒绝提交弹药、revision、高水位或存档，关闭进程即清除。
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

- BaseWorld 保存最后一次水平朝向；静止状态不再由渲染器强制回到左向。共享 `collision` 模块对 Base 设施和 Raid `BallisticBlocker` 执行 X/Y 两轴连续首次接触解算；玩家及敌人的四向、斜向、长帧与攻击前冲均停在障碍边缘，未被阻挡轴仍可贴墙移动。
- SDL client 的全部玩家可见文本绘制统一经过 `UiTextRenderer` 和 `localizeUiText`；内容名称、动态计数、领域拒绝原因、菜单、HUD、库存、Base、RaidResult 与 F10 面板均支持 English/简体中文。
- 首次运行默认简体中文；主菜单与 Base/Raid 暂停菜单的设置页可点击语言项切换，`L` 是同一设置页快捷键。选择写入独立 `settings.json`，损坏或未知设置安全回退到简体中文，不改变游戏存档。
- Windows 客户端使用系统微软雅黑生成并缓存 Unicode SDL 纹理，不提交字体文件、不生成美术资源、不修改 manifest；纯数字物品数量继续使用 SDL 内建数字字形。

## Base 世界时钟与每日需求 v1 当前实现

- `ProfileState::WorldClockState` 以第 1 日 08:00 起算的整数世界分钟作为唯一权威时间；Base 与 Active Raid 使用相同的暂定 60 倍时间缩放，日、时分与昼夜只由领域投影生成。
- 未暂停 Base 世界和 Active Raid 才推进。Base 库存/设施模态页、Esc 暂停、MainMenu、RaidResult 与离线时间冻结；Base 每 30 秒有效模拟时间检查点保存，离开 Base 前显式保存。
- 四项需求改为每日 00:00 结算。多日跨度以常数时间补算，`resolvedDemandCycleCount` 保证同一日界线只结算一次；短缺仍不阻止游玩或损坏资产。
- schema v8 保存时钟与已结算日数；v1～v7 迁移到初始时刻且不重放旧 Raid 次数。Raid 内时间只随成功、死亡或主动退出 Settlement 提交，关闭程序或异常退出恢复出击前时间。
- Base、资源分配页与 Raid HUD 只读显示双语时间；未增加调试按钮、新资源、音频或 manifest 修改。

## Raid 往返行动耗时 v1 已接受实现

- `MapDefinition::travel` 为 Greyline Depot、Riverside Checkpoint、Ashworks Yard 分别配置 `45/45/90`、`90/90/180`、`150/150/300` 世界分钟的出发/正常返程/失败归队开发值；ContentRegistry 拒绝零值、过大值和失败归队短于返程。
- `queryRaidTravel` 无副作用投影出发与抵达时间；Base 出击面板显示抵达昼夜和三项耗时，RaidResult 显示本局实际提交的返程或归队时间。
- Deploy 快照冻结地图旅行值、出发前时钟和 Base 资源；成功撤离与主动退出使用正常返程，死亡使用失败归队。旅行跨越 00:00 时复用每日需求幂等结算。
- schema v9 保存旅行快照与结算耗时；schema v8 旧存档和旧 pending Raid 显式迁移。异常退出不能走 Settlement，只能恢复出发前 Profile，保证时钟、需求周期、资源与资产一起回滚。
- 当前不实现夜间视野、路线状态、旅行遭遇、哨所、情报、人口/床位/口粮、精力或睡眠；分钟值是集中内容调参，不声明为最终平衡。

## Base 枪匠全面维护服务 v1 历史基线

- PR #81 的 content v15 曾数据化全面维护价格和 240 世界分钟时长；这是已合入的历史存档合同，当前 PR #83 已按用户新决策替换新维护路径。
- `BaseServiceJobId`、`BaseServiceAssetLocation` 和单项 `GunsmithMaintenanceJob` 明确服务期间的唯一资产所有权。送修只接受 Stash 根层受损武器，原子扣款并保存；所有拒绝和保存失败保持 Profile 指纹、revision、货币与高水位不变。
- schema v10/v11 继续保存旧任务、高水位、冻结报价/完成点和服务资产位置；PR #83 不删除这些字段，以便旧存档中的武器仍由唯一位置持有并可安全领取。
- 当前新维护不会再产生这种任务；旧任务在加载后立即可领取，Stash 空间不足时仍零修改保留服务所有权。

## Base 愿望与资源取舍 v2 当前开发实现

- content v59 保留五日周期并发布食物、卫生/医疗、娱乐/士气和安全四类稳定愿望；人口档位在周期开始时冻结，当前分别生成一、二或三个不重复愿望。
- `BasePriorityState` 保存周期索引、冻结人口、各愿望定义 ID/完成位和累计错过数量。选择由 Profile、周期和人口的稳定随机流决定；打开界面、进程重启和周期内人口变化不会重抽。
- 玩家在 Allocation 页先选愿望，再明确选择仓库、装备或随身容器中的基地可访问资产。多个同分类物品可以共同达标；query/execute 共用贡献、门槛和超额规则，任何失败均零修改。
- 完成愿望只消费明确选择的完整资产并写入既有士气账本，不增加货币、抽象资源或奖励物。错过的每个愿望也只进入下一次士气日结原因。
- schema v43 保存多愿望和冻结人口；schema v42 及更早单愿望档案保留当前周期、定义、完成状态与累计错过，不补抽当前周期目标，下一周期才切换到完整规则。Pending Raid 的出发前快照使用同一迁移与回滚合同。
- Allocation 页使用双语文字和几何占位显示愿望列表、剩余时间、来源提示、贡献与超额；基地运营总览把未完成愿望显示为需处理事项并定位到资源分配设施。它不是任务面板，也不提供导航。

## Base 运营状态与即时枪械维护 v1 当前实现

- content v18 以四项资源各自每日需求为分母，只配置紧张/充足储备日数；已无消费者的枪匠耗时和运营耗时倍率已删除。
- `BaseOperationalProjection` 只读计算各项整日储备、精确最短板和运营档位，不进入 Profile、revision 或存档。首版状态仅作风险可读投影。
- 玩家枪械全面维护只消费货币：候选 Profile 原子恢复当前/最大耐久到出厂值并清除故障；不推进世界时间、不创建任务、不移动武器，也不改变枪膛和已装弹匣。运营资源不影响报价或资格。
- schema 维持 v11，并显式接受 content v16/v17 存档；旧计时任务立即可领取。Allocation 与 Supply 以中英文显示运营状态、最短板和即时维护反馈。
- 当前四项池仍只是早期运营储备；正式居民士气已由独立三档状态实现，不复用旧 `morale` 资源字段。人口、口粮/床位、制造和自动防守已成为实际消费者；更复杂的防御设施与实时守城仍延期。

## Base 居民、床位与睡眠 v1 当前实现

- `ProfileState::BasePopulationState` 只保存普通居民聚合人数和床位容量；玩家与未来具名 NPC 不计入该池，也没有逐居民姓名、情绪、装备或日程模拟。
- 旧档迁移为 8 名普通居民、10 个床位；每人每天消耗 1 单位口粮，因此旧固定食物需求仍为 8，升级不会改变既有每日消耗。原 `morale` 池在界面改称运营支持，仍不是完整版三档居民士气。
- Base 新增文字/几何占位宿舍。玩家可查看居民、床位/拥挤、口粮储备和下次日结，并正常选择休息 1、6 或 12 小时；休息不治疗玩家。
- `queryBaseRest/executeBaseRest` 在候选 Profile 上推进唯一 WorldClock、按人口结算跨越的日界线、同步五日愿望并原子保存。非法时长、Raid pending、过期 revision、重复事务和保存失败均不产生部分提交。
- schema v12/content v20 保存人口与床位；schema v11 及更早版本使用确定性默认值迁移。Base 实时流逝和 Raid 往返跨日也消费同一人口口粮需求。
- PR #86 已把普通幸存者接纳接入聚合人口；PR #87 增加一项真实建设项目；当前士气切片把聚合居民接入三档正式士气。岗位/专业、精力、具名 NPC 和疾病模拟继续延期。

## Raid 普通幸存者安全转移 v1 当前实现

- content v21 为三张固定图各声明一个稳定、一次性的普通幸存者救援点；内容加载拒绝重复 ID、未知类别、越界以及与障碍、高危区或撤离区重叠的定义。
- 玩家在区域内持续按住 F 2 秒完成安全转移；松键、离区、受伤、受控制或打开背包都会取消进度。界面只使用双语文字和几何占位，显示接纳后的居民、床位缺口和日口粮预测。
- `ProfileState::committedRescues` 与 schema v13 幂等保存已完成的稳定救援 ID。接纳立即增加聚合普通居民；床位或口粮不足只预警，不阻止接纳，同图不能重复增加人口。
- `GameSession` 同时维护活动 Raid 候选和不含 pending Raid 的干净恢复候选。救援写盘只保存人口事实；后续死亡、主动退出或异常关闭仍保留居民，但装备、HP、战利品、资源和 Raid 时间继续恢复到出击前状态。写盘失败则人口、账本和世界确认均不提交。
- 本切片没有新增或修改正式美术、音频与 manifest；具名 NPC、护送 AI、职业、伤病和逐人模拟继续延期。

## Base 宿舍扩建 v1 当前实现

- content v22 为废旧零件与损坏电子元件声明独立建材价值，并定义唯一宿舍 1→2 级项目：4 建材、3 名聚合劳动力、360 世界分钟、床位 10→14。四项运营资源不承担建筑材料语义。
- 用户于 2026-08-25 明确修订返还合同：成功撤离后，装备、弹药、战利品及其容器关系全部保持撤离瞬间的精确 `AssetLocation`；未来载具货舱也必须遵守同一规则。Settlement 不再把物品迁入 `BaseIntake`，该容器只保留旧存档恢复用途。
- Allocation 页从统一的基地可访问自有资产中显式选择回收物加工为建材；原资产实例不可逆消失。宿舍页显示等级、建材、可用/占用劳动力、剩余时间，并提供正常的开始或取消入口。
- 启动、取消和完成均通过候选 Profile 原子提交；取消返还锁定建材且不回退已流逝时间。Base 实时、休息和 Raid 往返共同推进项目，完成只结算一次。
- Deploy 快照冻结出击前建材、项目和床位；未结算 Raid 异常恢复时回滚建设进度和床位，但不会回滚已经安全转移并落盘的普通居民。
- schema v14 保存当前项目及 Raid 回滚快照；schema v13 确定性迁移为宿舍 1 级、0 建材、无活动项目。客户端继续只使用双语文字与几何占位，没有修改正式美术、音频或 manifest。

## Base 分类自动供给 v1 当前实现

- `BaseSupplyPolicyState` 按物品定义保存唯一供给分类：食物、医疗、娱乐或安全。勾选/取消是候选 Profile 原子事务，只改变授权，不移动、不锁定也不立即消耗任何物品。
- 菜单按分类聚合显示当前拥有且可提供对应贡献的物品定义、总数量和单件贡献。可乐可在食物与娱乐之间选择一种用途；基础药品提供较高医疗贡献，卫生纸提供较低医疗贡献；旧书提供娱乐贡献。新增内容仅使用文字/几何占位。
- 每日需求仅在现有储备不足时，按稳定资产 ID 消耗能够补足缺口的最少完整数量。未勾选物品、不匹配分类及非空容器均不消费；规则在物品耗尽后继续保留，后续获得同定义物品仍适用。
- Pending Raid 期间不自动消耗任何自有资产，使异常退出能恢复精确出击前资产状态；Raid 往返仍结算既有抽象储备消耗。成功返回 Base 后，物品继续保持玩家/容器原位置，下一次 Base 日结才可能按已授权策略使用。
- schema v15/content v23 保存策略并把 v14 旧档迁移为空策略；未知定义、类别无对应贡献、重复条目或坏分类均拒绝加载。该切片已由 PR #87 接受并进入 main。

## Base 居民伤病与医疗所治疗 v1 当前实现

- `BasePopulationState` 在普通居民聚合人数之外保存受伤人数；受伤居民继续占用床位并消耗口粮，但不计入宿舍建设的健康劳动力。当前没有逐人姓名、职业或病症模拟。
- Ashworks 的一次性安全转移冻结并接纳 1 名受伤居民；其余两张固定图当前接纳健康居民。伤病事实与救援账本一起幂等落盘，即使同局随后失败也不会重复或回滚。
- 医疗所同页保留两条独立路径：玩家付费医疗只扣货币并立即完成；居民治疗消耗 360 世界分钟，并从统一 `AssetRegistry` 的真实位置原子消费玩家已授权为医疗供给的物品。没有第二套公共医疗库存。
- 居民治疗预览与提交使用同一确定性计划：贡献高的物品优先，同级按稳定资产 ID；非空容器不可被消费，拒绝、过期 revision 或保存失败均保持 Profile 和资产不变。
- Base 实时、休息及 Raid 往返共同推进活动治疗；到期只结算一次。schema v16/content v24 保存受伤人数、活动治疗和出击前回滚状态；v15 迁移不会追溯制造伤病。
- 客户端继续只使用双语文字和几何占位，没有新增或修改正式美术、音频与 manifest。

## Base 正式士气与周期事件 v1 当前实现

- `BaseMoraleState` 与旧运营支持资源完全分离，保存低迷/稳定/高昂档位、趋势、连续低士气天数、恢复进度和上次每日原因账本。
- 统一日结顺序为需求消耗→士气结算→愿望轮换→事件轮换；愿望和事件写入下一日原因，短缺/床位不足优先，单个世界日最多移动一个档位。
- content v26 提供四个五日轮换事件和 120%/100%/90% 制造耗时系数。事件由 Profile 与周期确定并保存，打开界面或重启不能重抽；任意大时间跨度只按事件集合大小汇总。
- schema v18 保存士气、账本、当前事件及出击前回滚快照；v17 确定性迁移为当前日的稳定士气和事件。异常退出 Raid 精确恢复出发前状态。
- Allocation 显示正式士气、趋势、日结原因、低士气持续时间、当前事件与下次轮换；工坊显示当前士气下新订单耗时。客户端只读取领域投影，不自行推断变化。
- V1 不移除居民、不触发叛乱、不改变战斗难度，也不阻止出击；更复杂的愿望奖励、事件选择和人口后果后置。

## 尚未完成

- 高倍率圆形光学视野等待首个合法高倍瞄具定义、附件安装点和实际内容消费者后独立交付；当前基础开镜不伪造高倍镜。
- Rifle 当前只启用 Stovepipe；Misfire/Double Feed 需要通用的 Raid 动态地面弹药所有权，不能静默销毁或凭空生成退膛/抛出弹药。
- 组件级耐久和改枪台后续独立切片；当前枪匠服务只消费已有武器实例耐久与故障，不建立人口岗位或通用任务框架。
- 疼痛叫声的墙/门遮挡等待正式空间遮挡查询；当前只提供有消费者的距离刺激，不能扩张为通用音频事件总线。
- 旧 V0 `ItemId`/`ItemInstance` 与旧 GameplayWorld 路径仍保留给历史回归；生产 Alpha 已绕过，后续按消费者安全退场。
- 区域基地威胁已包含公共 Security 自动防守和主动外围清剿两条路径；实时基地战、敌人波次、防御设施、战斗小组、多档防守投资、个人资产损失和正式表现仍明确延期。
- Week29 分支继续不整体合并；已接受反馈均由后续独立切片按新边界实现。
- 正式攻击动画及所有新正式美术/音频生产。

## 明确停止扩展的 V0 合同

- 3 HP、180 秒直接失败、V0 只读 Stash、无限弹和旧 RaidSettlement 不是产品终态，不得增加新消费者。
- 生产 Alpha 只通过 Profile Deploy/Settlement 事务进入 Raid；不得重新引入 Profile 与 V0 库存的资产复制桥。
- 生产射击不得重新引入可渲染/可碰撞 Projectile 场景实体；武器、伤害、存档和 App 只能消费射击领域值、逻辑飞行投影与 HitResult。
- 普通命中最终不显示准星 X；爆头/弱点必须由击发准星意图与真实命中部位双重验证的领域结果驱动，App 不得猜测。当前敌人尚无数据化弱点区域，因此只有领域接口预留，不伪造弱点内容。


</details>


</details>

</details>

</details>

</details>
