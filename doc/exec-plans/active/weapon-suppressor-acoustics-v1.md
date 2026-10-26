# 制式5.45步枪消音器与枪声取舍 v1

**方案：`RL-WEAPON-SUPPRESSOR-ACOUSTICS-v1`。状态：用户已验收，正在交付。**

让玩家用资金、重量和少量操控性能，换取较小的开枪暴露范围。只扩展现有制式5.45步枪，不重启UI专项。

候选`d0b4a091f1fa`已完成。请继续使用桌面 **Project Raidline Debug**，按[五步清单](../../acceptance/weapon-suppressor-acoustics-v1.md)正常游玩复验。下方范围保留获批方案，实际结果见文末；尚未提交、推送或合并。

## 玩家将得到什么

在“供应与回收”→“购买”取得一件消音器，或去边境交换站的“制造厂库存点”搜获。回到基地，使用现有物品右键菜单的改装入口，将它装到制式5.45步枪的新“枪口”槽。成功撤离后保留，拆下后恢复普通枪声与操控属性。

| 项目 | 本次方案 |
| --- | --- |
| 支持平台 | 仅制式5.45步枪；其他枪械不可安装。 |
| 安装位置 | 新增一个枪口槽，最多一件；默认枪管与现有精确枪管均兼容，不要求导轨护木。 |
| 规格 | 2×1格、可旋转、350克、不堆叠；安装后整枪占格不变，重量只计一次。 |
| 操控代价 | 操控速度−8；后坐力控制、稳定性、人机工效、精准度和有效射程不额外改变。 |
| 敌人听声范围 | 当前活动原枪声半径×0.5：Raid为900→450世界单位；基地外围和亲自防守为1500→750。 |
| 买入 / 回收 | 240 / 60普通货币；使用现有付费购买和回收事务。 |
| 搜获来源 | 既有制造厂库存点物资表新增相对权重10、每次命中产出1件。 |
| 使用成本 | 不新增独立耐久、过热、耗材、特种弹药要求或制造配方。 |

掉落权重10不等于10%掉率。资源点数量、容量、抽取次数、其余条目权重和敌人不增加；只影响之后新生成的合法Raid，已冻结的物资不重抽。

这件组件仍不是“静音”。近处敌人可以听见；Raid内一名敌人听见后，其同组成员可能在半径外一起警觉。已看见玩家的敌人不会因装了消音器失去目标。

伤害、射速、弹速、射程、弹药兼容、故障、武器磨损、枪口火光和视觉暴露规则保持。基地安全区、外围视觉发现距离、防守波次和高危压力也保持原样。

## 本次表现与范围边界

沿用已验收的改装界面，只增加新功能必需的枪口槽、简短名称和属性前后对比。不增加管理页、不重排背包和仓库、不继续UI美化。

暂复用已授权P0枪声作为听感占位：本发枪声播放增益×0.55，本发户外远尾增益×0.25。只调整这一次播放，普通枪声、全局音量和其他声音保持原值。没有正式消音枪声样本，不能把本片交付描述为正式消音音效完成。

敌人听觉按独立的0.5范围倍率执行。关闭声音、改变音量或音频设备不可用，都不能影响敌人反应。

仅在基地改装，保留原有隔离开发测试场入口。长期GDD允许局内改装，当前实现仍限制基地；这是既有阶段差异，本片不扩大局内改装，也不将当前限制改写为长期规则。

不做其他枪型配件、制退器、亚音速弹、声音穿墙模拟、新敌人、NPC、地图、配方、商人成长或独立配件损耗。不生产新图片或音频，不修改美术和音频Manifest。消音器使用现有合法组件占位图及中文名。

现有供应窗口增加240货币商品，是本次明确提出的付费获取途径；不把高级组件纳入长期基础必需品保底设定，不扩张GDD商人体系。

## 怎么确认完成

继续使用桌面 **Project Raidline Debug**，不新建存档，不增加新的测试入口。完成后交付可识别的冻结候选和一份简洁清单，等待用户正常游玩验收。

| 普通操作 | 预期结果 |
| --- | --- |
| 在“供应与回收”购买并在基地安装 | 扣240货币，组件只存在一件；整枪增加350克、操控速度降低8。 |
| 查看不兼容武器的改装选择 | 不能装上此组件；不扣钱、不丢失或移动物品。若界面已有过滤，不要求玩家强行构造非法操作。 |
| 未被看见时持枪出击并开火 | 播放较轻的临时枪声；附近敌人仍可能调查，不承诺半径外同组者无反应。 |
| 成功撤离并正常重开 | 消音器仍安装在原武器，弹药及其他装备按原规则保留。 |
| 拆下消音器 | 组件回到合法仓库位置，重量、操控和普通枪声恢复；没有复制或吞失。 |

精确半径、边界、敌群传播、外围和防守的同一倍率由隔离自动化验证。不要求玩家量距离、刷随机掉落或损失真实装备。

<details>
<summary>开发接线、存档保护和检查范围</summary>

### 已核查的实现与拟修改

实际源码：`E:\WorkPlace\Projects\C\Project Raidline-ui-inventory-presentation-v1`。治理入口目录的旧源码不作为当前实现证据。

| 真实路径 | 已确认事实与实施操作 |
| --- | --- |
| `src/item_definition.h`、`src/content_registry.cpp` | 当前组件仅四槽。将Muzzle追加在枚举末尾，旧枚举不重排；在静态能力中增加有界、有限的枪声倍率及必要播放反馈。旧定义默认1，非法值和错误兼容引用拒绝加载。 |
| `assets/content/v1/core.json` | 拟增加`item.component.suppressor_5_45`，兼容`item.weapon.rifle_5_45_service`；按上表设置价格、重量、占格、操控与降噪。仅向`loot.frontier.fabrication_stock_v1`加入权重10条目，保持每次产出1件。内容更新为拟定`weapon-suppressor-acoustics-content-71`。 |
| `src/weapon_component_domain.cpp` | 复用`effectiveWeaponUse`和`query/executeWeaponComponentChange`。新增槽合法性及有效声学属性，仍先在候选档案中原子准备；满仓、保存失败、过期状态与重复事务不得半完成。 |
| `src/game_session.cpp`、`src/world_shooting_runtime.h/.cpp`、`src/gameplay_world.h/.cpp` | 当前Raid成功击发固定调用`emitPlayerNoise(900)`。让已成功击发的本发消费对应有效武器参数，并携带本发只读音频反馈；换枪、后续输入或客户端显示不得追改本发。保留听声后的组内传播及原先视线、警觉和追击规则。 |
| `src/base_world.h/.cpp`、`src/base_defense_runtime.h/.cpp` | 外围和亲自防守目前以1500判断听声。消费同一0.5倍率，保留外围`Perimeter`条件、防守`playerExposed`条件、原边界比较和敌人先行动/射击后警觉顺序；不把Raid生命周期接入基地。 |
| `src/game_audio.h/.cpp`、`src/app.cpp` | 目前`GameAudioOutput::play`无单次增益。增加受控、仅本播放实例的倍率，原调用默认1；根据实际本发事实播放0.55枪声/0.25户外远尾，音频失败不回写玩法。 |
| `src/ui_weapon_components_presentation.h/.cpp`、`src/app_ui_weapon_components.cpp`、`src/ui_item_context_presentation.cpp`、`src/ui_localization.cpp` | 当前安装投影是固定4元素，槽按钮也是4项。同步为5槽并提供必要前后属性/降噪说明、中文名称和选择命中；延续既有风格，不重做其他UI。详情只展示权威属性。 |
| `src/profile_state.cpp` | `populatePublishedWarehouseCatalog`当前会考虑普通组件。排除新消音器免费发放，覆盖新建档、旧目录兼容补领及对真实档案的开发目录补领；保留原赠送与绷带卷排除。独立测试场临时物资仍隔离。 |
| `src/save_repository.cpp` | 已安装组件关系只保存父武器ID。本片不新增持久字段，保持schema57；接受内容70及原有旧内容升级路径，并在覆盖旧主档或备份前保留不可覆盖、逐字节核对的升级源副本。延续内容70的强制内容校验，不能只改版本字符串。 |
| 现有相关测试及拟新增`tests/test_weapon_suppressor_acoustics.cpp`、`tests/test_weapon_suppressor_save.cpp` | 分别覆盖真实击发/听觉/音频和存档资产闭环；在`CMakeLists.txt`登记独立目标，避免继续扩张已很大的存档测试单元。 |

`loot.frontier.fabrication_stock_v1`对应边境交换站的`resource_point.frontier.fabrication_stock`，实际中文为“制造厂库存点”；原地图只有1个、容量3，保持不变。“供应与回收”购买投影读取`fixedSupplyItemIds`，正数买入价即可复用现有目录与`GameSession::executeProfileEconomy`，不需要新商店系统。

有效声学属性沿用当前武器属性解析，不添加每帧或每发重复扫描全部资产的路径。空击、拒绝开火、未击发故障不发枪声刺激；实际击发后故障仍恰好一次。声音只在当前活动空间传播，保留现有组警觉合同，不新增声学遮挡规则。

### 保存、资产和回滚

安装关系继续使用`InstalledWeaponComponentLocation{weaponAssetId}`，不建立第二套持有者。组件随父武器按现有规则进入撤离、失败、失物、两类寻回和回收，不能复制、漏计重量或漏回收。异常退出仍恢复出击前Profile。

旧武器默认倍率1；旧档不补发消音器；旧pending Raid的布局、敌人、物资和次数保持冻结。新内容只影响之后合法创建的Raid。制造中、待领取或不可访问资产继续拒绝普通移动和使用。

实施前备份旧程序和内容70并保留当前玩家数据指纹。回退时只能成套使用旧程序、旧内容和升级前备份；不让旧程序读取含新组件的内容71档，不删除新物品强行降级。如实施确需新持久字段或扩大玩法范围，先修订方案，不自行增加scope。

### 实施顺序与退出条件

1. 静态组件、解析/校验、安装资产及内容兼容；建立领域与存档回归。
2. 接入Raid、外围、防守的真实击发及本发音频占位；验证默认武器不变、成功击发一次、声音设置与AI独立。
3. 接入五槽和必要中文、购买/搜获/拆卸完整流程；完成构建与全回归后冻结候选，一次交付用户复验。

头文件变化后重建全部受影响目标。Windows按`windows-debug`配置，先做组件、内容、资产、保存、经济、射击、听觉、音频和UI针对性回归，再跑完整CTest。保留原2734项回归与原五项3ms负载、计时、断言及阈值，不skip或放宽；包含32/100目标下的正常真实消费者检查。

额外验证满仓拆卸零修改、保存失败、重复事务、空枪/故障、换枪、默认/精确枪管、三活动半径边界、Raid半径外同组传播、关闭音频或无设备仍产生相同听觉结果、重量只计一次、成功/失败/寻回/回收及内容70真实磁盘重开。测试不读取或改写玩家真实档案。

本轮只有静态调查、远端事实核对和文档检查，没有构建、运行测试或启动游戏。2734项及原五项通过是PR194的基线证据，不能当新功能检查结果。新功能远端CI须待本片取得提交/推送授权后，在准确HEAD上进行。

</details>

<details>
<summary>当前授权、基线和策划协调凭据</summary>

| 接管字段 | 当前值 |
| --- | --- |
| task_id / state | `weapon-suppressor-acoustics-v1` / `accepted_delivering` |
| plan_version | `RL-WEAPON-SUPPRESSOR-ACOUSTICS-v1` |
| 已验收依赖 | PR #194；`origin/main=7ee8600dc54d89195d44b3de6caa09133c9192b1`，2026-10-26已fetch核对。 |
| 现有分支 / HEAD | `codex/weapon-suppressor-acoustics-v1` / `7ee8600dc54d89195d44b3de6caa09133c9192b1`；功能未提交，须用候选源清单和程序指纹识别。 |
| 实施分支（批准后拟建） | `codex/weapon-suppressor-acoustics-v1`，以实施时复核的已验收依赖为起点，保留无关改动。 |
| planning_source | 2026-10-26本主控线程用户：“请协调策划线程安排下一步功能开发任务，并给出具体开发方案。” |
| approval_source | 2026-10-26本主控用户：“同意该方案，请你执行开发任务”；批准RL-WEAPON-SUPPRESSOR-ACOUSTICS-v1实施，尚未授权本片提交、推送或合并。 |
| candidate_id / accepted_candidate_id / acceptance_source | `RL-WEAPON-SUPPRESSOR-ACOUSTICS-v1-local-d0b4a091f1fa` / 无 / 无；用户已验收本候选，正执行已授权交付。 |
| verification | Windows全目标编译、针对性208/208（组件181与供应27）、完整2765/2765及原五项3ms全部通过，0失败/跳过；六组静态界面已查看。新功能远端CI未运行。 |
| pending_request | 无，策划有效回复已接收；下一步完成准确PR及main CI和已授权Git交付。 |

策划目标为原线程`019feff4-477c-75c2-bf31-3f09ff1e3891`，host `local`，没有替代或新建策划线程。有效回复：turn `01a17711-21ff-7063-88ab-6018a9554665`，message `msg_09394c523a164a07016adf02a9960081919d3460e0271edbb9`，`role=planner`、`status=proposed_readonly`。

已核对请求四元组：`main-next-function-20261026-after-pr194` / `post-field-medical-next-function` / `RL-NEXT-FUNCTION-PLANNING-v3` / `7ee8600dc54d89195d44b3de6caa09133c9192b1`。中途一条旧视野任务commentary已指出并排除，最终回复明确不重开该已交付功能；旧信息没有被用作实施授权。

策划选题和初始数值是本次提案，不是人类批准或既成游戏事实。主控补齐了真实中文来源、免费目录守卫、三活动消费者及旧档保护；没有改写外部GDD。

只读GDD的武器、物品和核心循环章节支持合法安装位、整枪占格及搜集整备方向；消音器具体数值、出售途径和0.5倍率属于本次新提案。尚无新正式资源生产授权。

支持证据：`C:\Users\Administrator\Documents\美术资源生成\tmp\post-field-medical-next-function-planning-20261026`。包含原生请求/纠偏/完整有效回复、五工作目录事前指纹、572项源清单及文档检查。玩家数据、固定程序、桌面入口及其他未提交修改必须保持。

- [x] 原策划有效选题回复及真实代码核查。
- [x] 具体方案、影响、验收和保护边界。
- [x] 用户批准本版本。
- [x] 实施、构建、自动化及冻结候选。
- [x] 用户正常游玩验收。
- [ ] 在单独授权范围内提交、推送和合并。

</details>

## 实施启动（2026-10-26）

用户明确批准本方案并要求执行开发。已复核main7ee8600和PR194成功依赖，保留全部原未提交修改，从该已验收依赖建立`codex/weapon-suppressor-acoustics-v1`。实施前五目录、玩家数据及固定入口已做指纹快照，旧程序与内容70已备份。后续以本段和当前字段为准；上文“拟修改、尚未检查”等属于批准前方案。完成后提供候选等待人工验收，暂不提交、推送或合并。

## 当前交付候选与实际检查

**已完成：`RL-WEAPON-SUPPRESSOR-ACOUSTICS-v1-local-d0b4a091f1fa`。状态：`awaiting_user_acceptance`。** 未提交、推送或合并；开发代理未启动游戏，不安排下一任务。

实际完成两条获取途径、原子改装、三活动枪声范围、单发轻响占位、五槽界面及中文提示。正常物资表只增加一个权重10条目，原资源点数量、容量和抽取次数不变。附近听声者仍可唤醒同组成员；没有降低视线、追击、波次或高危压力。

内容71保持schema57，旧内容70/69及更早原升级兼容、不可覆盖主档/备份保护均保留。正常新建档、旧目录补领和真实档案开发目录均不免费补发消音器；独立测试场仍为临时物资。没有新图片、音频文件或Manifest变化。

| 实际检查 | 结果与边界 |
| --- | --- |
| Windows Debug | 按仓库preset配置、全目标重建及后续必要增量构建通过。公共头文件的全部消费者已编译/链接。 |
| 针对性回归 | 181项组件相关与27项供应相关，共208/208通过，0失败、0跳过；含全部新增组件、存档、真实击发、音频和改装界面回归。 |
| 完整CTest | 2765/2765通过，0失败、0跳过；原2734项保留，新增31项。 |
| 原五项3ms | 原负载、计时、断言、阈值及串行隔离不变，全部实际运行通过。 |
| 静态界面 | 1280×720、1600×900、1920×1080各中英文，共6组已查看；五槽及操控/降噪预览正常。仅软件渲染检查，不能代替正常游玩。 |
| 保全 | 五个相关目录其他原未提交文件逐字节保持；13份当前玩家数据和固定快捷方式未变。旧程序及内容70已备份。 |
| 远端与人工 | 本候选未提交、未推送，无本片远端CI；正常手感、听感和整体玩法仍待用户验收。 |

首次编译发现新测试遗漏可见性fixture头，随后发现旧内容构造中的JSON/string_view比较在MSVC有歧义；现仅补头和显式字符串比较。首轮针对性检查的11处失败为新定义总数预期遗漏，以及截图环境变量中文路径无法转换；物品总数已随新增定义改为60，原免费赠送59件/ID60断言不变；截图改用ASCII输出路径。

首轮完整回归的11处失败来自供应目录旧计数27（新增消音器后为28），以及将截图环境变量设为空字符串后仍被测试读取。现保留供应遍历和浏览不交易断言，新增明确检查消音器在目录中，并将精确计数更新为28；仅在全量测试进程移除截图开关，再完整重跑。没有删除断言、skip、放宽3ms或更改玩法；初次失败原日志保留。

实际测试文件比原拟定增加一份独立运行时目标`test_weapon_suppressor_runtime.cpp`，便于直接验证GameSession真实射击、外围、防守、恢复与同组传播；这是同一获批范围的测试组织，没有新增玩法。

### 候选身份与复查

- baseline / HEAD：`7ee8600dc54d89195d44b3de6caa09133c9192b1`；分支`codex/weapon-suppressor-acoustics-v1`，当前功能为未提交修改。
- 源清单：575文件；SHA256：`d0b4a091f1fad5de669028c83ffae1ed0789fef91112b8c6bddedf762bb30ed0`。本片31个产品/测试文件变化；自动化后重新核对源清单未变。
- 固定入口继续指向实际目录的`build/windows-debug/Project_Raidline.exe`；程序SHA256：`e7aa373779a307701caf7dd30418d7889a52394e4da5758e196cac98766ded2c`；运行目录内容JSON与源逐字节一致。
- 旧程序、内容70位于证据目录`preimplementation-program`；不直接用旧程序读取内容71档案，回退须配套升级前存档。
- 证据目录：`C:\Users\Administrator\Documents\美术资源生成\tmp\weapon-suppressor-acoustics-v1-20261026`。包含事前快照、源清单、实际构建与失败日志、focused/full的日志和JUnit、原五项门槛结果、六组PNG及保全检查。

实际修改文件如下；其余旧文档修改不属于本候选的代码交付范围：

- `CMakeLists.txt`
- `assets/content/v1/core.json`
- `src/app.cpp`
- `src/app_ui_weapon_components.cpp`
- `src/base_defense_runtime.cpp`
- `src/base_world.cpp`
- `src/base_world.h`
- `src/content_registry.cpp`
- `src/game_audio.cpp`
- `src/game_audio.h`
- `src/game_session.cpp`
- `src/gameplay_world.cpp`
- `src/gameplay_world.h`
- `src/item_definition.h`
- `src/profile_state.cpp`
- `src/save_repository.cpp`
- `src/ui_item_context_presentation.cpp`
- `src/ui_localization.cpp`
- `src/ui_weapon_components_presentation.cpp`
- `src/ui_weapon_components_presentation.h`
- `src/weapon_component_domain.cpp`
- `src/world_shooting_runtime.cpp`
- `src/world_shooting_runtime.h`
- `tests/test_content_registry.cpp`
- `tests/test_game_audio.cpp`
- `tests/test_profile_state.cpp`
- `tests/test_ui_base_supply.cpp`
- `tests/test_ui_weapon_components.cpp`
- `tests/test_weapon_suppressor_acoustics.cpp`
- `tests/test_weapon_suppressor_runtime.cpp`
- `tests/test_weapon_suppressor_save.cpp`

## 已验收交付（当前）

用户已明确验收本候选，并授权提交、推送和合并。本轮不开始下一任务。

人类授权：2026-10-26主控线程用户：“验收通过，请提交、推送和合并”。accepted_candidate_id：`RL-WEAPON-SUPPRESSOR-ACOUSTICS-v1-local-d0b4a091f1fa`。

提交前核对575项源文件与程序SHA均与验收候选一致。origin/main仍为7ee8600，PR194及其main CI已成功；本地完整2765项与原五项3ms适用于同一候选，无须因仅更新交付记录重复构建。

准确交付范围为31产品/测试文件、本片计划与验收清单，加CURRENT_STATE的本片新增入口，共34文件。CURRENT_STATE发布版本以HEAD原文件完整字节作历史后缀，不夹带本地其他历史UI记录。其他修改和工具不提交。

验收后的玩家数据、程序、快捷方式，以及五个相关目录当前脏文件已重新做交付前快照；保全以本片证据目录`delivery/before.json`为准。提交钩子如重写无关文档，将按原字节备份恢复，不绕过钩子或权限。

当前state：`accepted_delivering`；尚无feature提交、PR或新远端CI。下方后续实际交付记录优先于前面的开发/待验收历史，不新增仅CI状态提交。
