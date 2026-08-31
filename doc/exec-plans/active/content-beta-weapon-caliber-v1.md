# Content Beta 代表性枪械与口径梯度 v1

## 产品结果、基线与范围

本切片让玩家能够在 9×19 轻型、5.45×39 通用和 7.62×51 远距/重型供弹体系之间做出可理解的配装选择。六种职责武器必须完整消费现有购买/拾取、装备、弹匣、枪膛、击发、换弹、耐久、故障、维修、撤离、失物和两种寻回合同；差异来自射速、射程、操控、后坐力、稳定性、精准度、可靠性、重量和供弹，不使用武器品质颜色。

唯一范围来源为用户确认的 Content Beta 规划与 2026-08-30 策划主控交接。依赖基线为 `origin/main@e285a2b`，分支为 `codex/content-beta-weapon-caliber-v1`。PR #116 已验收合并，首张可玩随机大地图阶段关闭。

明确排除：组件化改枪、附件树、瞄具消费者、霰弹枪逐发装填、栓动/左轮、扩张/燃烧等特殊弹、多目标贯穿、击退/断肢、高倍镜、新射击手感模型、正式武器美术和额外音频生产。七装备槽与当前 `1/2/3` 热键保持阶段兼容。

## 差距、复用与停止扩展

- 复用：唯一 `AssetRegistry`、稳定 `ItemDefinitionId`、有序弹匣弹药、枪膛、真实消耗、武器属性、耐久/故障/维修、装备事务、Loot/Settlement/失物/寻回和版本化 JSON。
- 重构：现有弹匣只引用一个 `compatible_ammunition`；迁移为弹药与弹匣均引用稳定 `CaliberDefinitionId`，装填查询按口径判断。旧字段继续读取并在 ContentRegistry 内归一化。
- 新建：三个口径定义、六档普通弹定义、四件新武器、三种新弹匣、内容兼容图与只读 UI 投影。
- 停止扩展：新内容不增加 `ItemId` 枚举，不按显示名、贴图或品质色判断兼容，不建立无消费者的附件/弹种框架。

## 稳定 ID、定义与实例

口径定义：`caliber.9x19`、`caliber.5_45x39`、`caliber.7_62x51`。

保留现有定义且不改 ID：

- `item.weapon.pistol_basic`：9×19 轻型手枪职责。
- `item.weapon.rifle_basic`：保持现有 9mm 供弹，作为通用自动武器职责。
- `item.ammunition.9mm_basic`：9×19 标准普通弹。
- `item.magazine.9mm_15_pistol`、`item.magazine.9mm_30`。

新增定义：

- 武器：`item.weapon.carbine_5_45_compact`、`item.weapon.rifle_5_45_service`、`item.weapon.dmr_7_62x51_service`、`item.weapon.lmg_7_62x51_service`。
- 弹药：`item.ammunition.9mm_enhanced`、`item.ammunition.5_45x39_standard`、`item.ammunition.5_45x39_enhanced`、`item.ammunition.7_62x51_standard`、`item.ammunition.7_62x51_enhanced`。
- 弹匣：`item.magazine.5_45x39_30`、`item.magazine.7_62x51_20`、`item.magazine.7_62x51_100_box`。

`ContentRegistry` 唯一拥有不可变定义。Profile/Raid 中所有武器、弹匣和弹药实例继续由 `AssetRegistry` 唯一拥有；弹匣保存有序 AmmoDefinitionId，枪膛保存可选 AmmoDefinitionId，不为每发弹分配实例 ID。UI 只消费兼容查询及定义投影。

## 领域合同与持久化

- `CaliberDefinition` 提供稳定 ID 和双语显示投影。
- `AmmunitionUse` 提供 `caliberId`、普通弹档位和穿甲值；高级普通弹只提高穿甲与稀有/经济成本。
- `MagazineDefinition` 以 `caliberId` 接受同口径任一已发布弹药；WeaponDefinition 仍显式列出合法弹匣，避免只凭口径安装错误弹匣。
- 现有 `compatible_ammunition`/`compatible_magazine` JSON 继续可读；加载时归一到新兼容图。新定义不进入旧 `ItemId` 枚举。
- 弹药装填、安装、换弹和击发继续使用既有查询/命令；拒绝操作保持 Profile 指纹、revision、ID 高水位和弹药总量不变。
- content v52；schema 保持 v38，因实例保存结构不变；rules 保持 v28，因本切片不重排 Raid Loot。content v51 与更早受支持版本继续加载。
- rules v28 及更早 pending Raid 使用已冻结 DefinitionId，不替换武器、不重抽 Loot、不改变弹匣顺序。

## 实施步骤与退出条件

1. 建立口径/弹药定义、兼容查询及旧字段适配；内容校验覆盖重复/缺失 ID、未知口径和非法兼容。退出条件：旧内容原样加载，错误矩阵拒绝。
2. 迁移弹匣装填、枪膛、击发伤害/穿甲消费及 UI 投影；不改准星、后坐力或逻辑弹道运动。退出条件：同口径两档弹可混装，错口径零修改。
3. 发布四件新武器、三种新弹匣和五件新弹药定义，使用代码/文字/现有 fallback；参数形成六种职责而非线性升级。退出条件：六种武器可完成完整武器生命周期。
4. 接入 Base 供应与受控测试来源：标准 9mm 保持最低供应，其余内容先通过有限供应/现有 Loot 测试入口形成可验收闭环；正式风险来源重排留给 Macro 3。退出条件：高级内容不进入无限基础救济。
5. 更新双语检视/配装警告，完成自动化、Windows Debug、完整 CTest、exact-head Windows/Ubuntu CI，再交给用户正常游玩验收。

## 自动化与正常游玩门禁

- Weapon–Magazine–Caliber–Ammo 全兼容矩阵，错口径、错弹匣、空膛与容量失败零修改。
- 两档同口径混装顺序、卸弹、枪膛、换弹中断、实际击发消耗及保存往返。
- 六种武器的射击模式、耐久、故障、维修、撤离、失败、失物老化、NPC/自力寻回。
- content v51/schema v38 旧档、旧 pending Raid、未知定义和损坏存档回归。
- 中英文显示与兼容信息必须来自同一领域查询；颜色关闭后仍可理解。
- 全量 CTest、Windows/Ubuntu exact-head CI；开发代理不启动游戏。

用户使用正常游戏流程完成：三种口径配装；正确/错误装填；同口径两档混装；装匣、上膛、射击、换弹、故障和维修；一次成功撤离及一次死亡后寻回。验收目标是至少区分轻型、通用和远距/重型三类职责，而不是逐项点击调试按钮。

## 提交、PR、风险与回滚

本分支只交付 Macro 1。内部提交依次为计划/内容合同、领域兼容迁移、六职责内容与 UI、验证/状态收尾；形成一个 Draft PR。任何后续防具/容器、Loot 经济和成长长序列使用独立分支。

主要风险：旧代码仍按 `ItemId` 或显示名分支；高级普通弹成为无条件上位；旧 Rifle 的 9mm 身份与新职责混淆；100 发弹匣放大 UI/重量与持续火力问题。通过 DefinitionId 查询、成本/穿甲单轴差异、明确职责文本和容量/重量测试控制。

回滚使用普通 revert。已经写入存档的新定义即使停止投放也不得删除；可从供应/Loot 中撤下但继续解析。不得重写历史或强推。

## 进度

- [x] PR #116 经用户验收并以 `e285a2b` 合入 main。
- [x] 从最新 `origin/main@e285a2b` 创建独立 Macro 1 分支。
- [x] 冻结本 ExecPlan、稳定 ID、兼容与迁移策略。
- [ ] 实现口径/弹药定义与兼容查询。
- [ ] 迁移装填、击发穿甲消费和只读 UI 投影。
- [ ] 发布六职责武器、三口径两档普通弹与弹匣。
- [ ] 完成 focused tests、Windows Debug 和完整 CTest。
- [ ] 推送 Draft PR 并通过 exact-head Windows/Ubuntu CI。
- [ ] 用户正常游玩验收。
