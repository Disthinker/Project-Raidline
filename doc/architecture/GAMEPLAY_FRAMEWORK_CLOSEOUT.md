# 玩法框架整合收尾评审

核对日期：2026-09-09。问题复现基线 `d43bab0`；已获用户正常游玩验收的集成代码为
`f16544dd40bd919863befa4b24dab8176b955fd1`（PR #156）。
本报告完成 `active/combat-runtime-boundaries-v1.md` 约定的下一门禁，不启动新重构阶段。

## 裁决与交付边界

- Enemy Lifecycle 及逐项授权的后续窄整合可以结束**代码重构阶段**：本次复查没有发现
  需要继续实现的生命周期、接触、反馈或切换边界缺陷。此结论不是全项目零缺陷保证。
- **尚未完成主线交付**：`origin/main` 仍为 `2ae898a`，#149–#156 全部为未合并 Draft。
  当前验收针对集成 tip，不追溯宣称每个旧 head 都通过独立人工验收，尤其不能把
  #149 的已知问题基线单独作为合格发布版本。
- 不创建 `CombatSpaceRuntime` 总管；不继续整体 Navigation / Combat / Session /
  Persistence 重写。没有当前消费者或故障证据的抽象不进入代码。
- 本轮只改代码仓库文档，保持 src/tests/CMake/内容/存档格式完全不变，不启动游戏。
  GDD、美术、音频、Manifest 和真实玩家存档不变。

## 实际 Before / After 调用链

问题基线：

```text
Daily / Defense / Raid -> shooting 接收可变 enemy vector
  -> resolver 删除 vector，并输出移除下标
  -> Daily 丢掉结果 -> Session 仅同步存活 ID
     -> 下一次 configure 数量不符 -> 重建整个外围 -> 回跳/复活
  -> Defense 以集合差推断击杀，再独立删除突破者
  -> Raid 依下标逐个删除并行 navigation / encounter 数组
```

当前共享调用合同（不是一个新的中央调度类）：

```text
活动策略选择出生 / 目标 / 波次 / 路径刷新条件
  -> EnemyRoster<State> 持有 EnemyLifecycle 与 ID 绑定的附属状态
  -> Enemy::updateTowardsTarget + 当前活动碰撞/安全策略
  -> resolveEnemyAttackContact -> PlayerDamageObservation
  -> WorldShootingRuntime::advanceShots
     -> resolveShotHits -> HitResult + EnemyLifecycle::removeDead
        -> 删除 ID 附属状态 + 压缩私有集合 + 失效攻击预约
        -> EnemyRemovalFact{id, position, reason}
  -> 活动消费死亡/突破事实；Session 只消费 Profile/活动/结算相关事实
  -> HitFeedbackPresentationState -> App 只读显示
```

Defense 突破通过同一 `removeForObjective` 入口，先于射击，不得同 ID 既突破又击杀。
这里统一的是敌人推进/接触先于射击的**顺序约束**，不是所有活动与 Profile 命令均已合成
一个原子 combat frame。既有弹药提交、玩家伤势事务和失败恢复仍由各服务消费者编排。

代码入口：`src/enemy_lifecycle.h`、`src/enemy_attack_contact.h`、
`src/world_shooting_runtime.cpp`、`src/hit_resolution.cpp`；三个真实调用方是
`src/base_world.cpp`、`src/base_defense_runtime.cpp`、`src/gameplay_world.cpp`。

## Mechanism / Policy 对照

| 责任 | 共享机制 | 必须保留的活动策略 |
| --- | --- | --- |
| 身份、死亡、移除 | 原有 CombatTargetId；EnemyLifecycle 私有集合、退休 ID、单次移除事实 | Daily 周期；Defense 波次 ID；Raid 每空间部署/高危生成 |
| 附属状态清理 | EnemyRoster 按 ID 同步删除；不跨 erase 保存引用 | Daily 出生位置；Defense waypoint/contact；Raid navigation/encounter |
| 导航刷新 | Raid/Defense 共用 selectNavigationRefresh，有界轮询、返回稳定 ID | Raid 扫描待刷新者；Defense 每次轮转一名；Daily 保留直接转向 |
| 敌人配置 | 新普通感染者共用 enemy.infected.basic / 12 HP | 旧冻结快照保留血量；出生数、时机与目标不合并 |
| 射击与特殊反馈 | 同一实际准星/弹道/命中解析、HitResult 与短时反馈 | 不由 App 或环境名推断爆头；普通命中无 X |
| 攻击接触 | 活体/目标资格/攻击窗口/碰撞/LOS；Grab 同次转 Bite；0.25 秒保护 | 目标资格、安全区和保护计时的推进/保存仍由活动持有 |
| 伤害结果 | PlayerDamageObservation，包含来源 ID、类型、部位、护甲及伤势信息 | Daily 救回；Defense 公共软失败；Raid 伤势/失物/Settlement |
| 帧顺序 | 三环境敌人推进/接触先于射击/死亡收束 | 子步频率、终局、感知/枪声处理与 Profile 提交点不强并 |
| 状态切换 | runtime 清理瞬态，Flow 通知成功边界；失败操作不先清理 | 同场 Defense 恢复完整检查点；Raid 进出室内保留各自 roster |
| 持久化 | 已有唯一 Profile、AssetRegistry、显式事实、原子保存/幂等 | Daily 检查点；Defense 一致后台检查点；Raid 出击前回滚 |

局部遍历下标和 squad 本轮临时下标仍存在；它们不是持久身份，也不作为跨模块移除结果。
死亡时 `Enemy` 立即清空攻击/移动，命中查询忽略死者；批次末统一压缩集合。
显式 Profile 载入和新周期可以导入 roster，普通每帧同步不能借数量差异重建世界。

## 明确的行为变化，不冒充纯重构

1. #150 修复已提交死亡回跳/复活与附属状态遗留；保存失败仍允许一致恢复旧检查点。
2. #151 仅新生成普通感染者统一 12 HP；旧事件不加载改血。Base/Defense 接入真实特殊反馈。
3. #153 Daily 不再用旧 1/2 HP 和猜测 Scratch；Scratch 12 躯干、Bite 18 头部（护甲前）。
4. #154 有效 Grab 同次消费 Bite；保护中消费接触但不追加伤害/控制/延时补发；Daily 接触检查 LOS。
5. #155 Daily/Defense 从先射击改为先敌人推进/接触，后到的致死射击不能撤销已接受接触。
6. #156 新档/读档明确失效旧 Base 缓存，成功出击/回营/换周期清除旧空间弹道和帧事实。

Daily 真实路径规划**未实施**；不把新增 LOS 或共享轮询描述成已经统一寻路行为。

## 累计范围与测试证据

以 `git diff --numstat d43bab0..f16544d` 为准，排除 #149 原功能与本报告：

| 范围 | 文件 | 增行 | 删行 |
| --- | ---: | ---: | ---: |
| src 生产代码 | 29 | 713 | 533 |
| tests | 10 | 1691 | 60 |
| 文档 | 13 | 781 | 6 |
| CMake / 内容 | 2 | 91 | 83 |
| 总计 | 54 | 3276 | 682 |

生产代码净增加 180 行，不声称所有删行都是重复算法，也不以行数证明正确性。
未新增通用管理器；EnemyLifecycle 154 行，接触 52 行、导航选择 33 行、受伤事实 32 行。
本轮不新增测试或放宽阈值，基线 1606 项累积为 1697 项（增加 91 项）。

- 本次 Windows Debug 全目标构建：`ninja: no work to do`，与已验收代码匹配。
- 本次全量 CTest：**1697/1697，0 失败，66.16 秒**，含串行防守性能门禁。
- `tests/test_enemy_lifecycle_contract.cpp` 中 17 个参数化合同由真实
  Daily/Defense/Raid 适配器运行，共 **51 个注册实例**；另有所有权、Session 与切换回归。
  Fixture 只安排确定性敌人/弹道，调用生产 update，不实现第二套模拟。
- 已覆盖 non-lethal、lethal、同帧多击/一次死亡、首/中/尾删除、存活者身份与位置、
  死者不攻击/导航/成为目标、无意重建、头部意图与实际命中双重匹配、接触保护及帧顺序。
- #156 增加 10 项：同进程/新进程载入、新档、接受/拒绝 Deploy、周期替换、定位清理、
  同场 Defense 保留真实弹道与武器/消耗、无效恢复不修改，以及多次室内进出。
- 最新代码 exact-head CI [34320466698](https://github.com/Disthinker/Project-Raidline/actions/runs/34320466698)：
  Windows **1697/1697 / 213.41 秒**，Ubuntu **1697/1697 / 72.35 秒**。
  本次只读重查两个 job 的 head SHA 与成功状态；本报告文档提交不是新的 C++ CI head。
- 用户本轮明确验收当前集成版本；开发代理未启动游戏。自动化性能不是可见 FPS 或发行硬件承诺。

## 依赖链审查与待授权的合并交付

各 head 的祖先关系及 Windows/Ubuntu check-runs 已在本轮核实；全部仍 OPEN / Draft。

| PR | head | 改动 | 精确 CI run |
| --- | --- | --- | --- |
| #149 | d43bab0 | 原玩家实时防守与问题复现基线 | 34126836998 |
| #150 | 2ff1a69 | Enemy Lifecycle | 34177559722 |
| #151 | 47c696b | 敌人配置/命中反馈 | 34186994633 |
| #152 | a98090a | 有界导航刷新选择 | 34188461419 |
| #153 | d70f861 | 完整受伤事实 | 34247787567 |
| #154 | b7937c3 | 接触与保护 | 34298809360 |
| #155 | c0b0664 | 敌人先行的帧顺序 | 34313046163 |
| #156 | f16544d | Profile/活动/空间切换隔离 | 34320466698 |

本报告在 `codex/gameplay-consolidation-closeout`，显式 stacked 依赖 #156，仅文档差异。
后续收到明确合并授权后：

1. 再 fetch 核实工作区、全部 head、main 与授权范围；任一漂移重新评估。
2. 按 #149 → #150 → #151 → #152 → #153 → #154 → #155 → #156 → 收尾文档的
   依赖顺序交付，保留 merge ancestry，不 squash/rebase，不提前删除被子 PR 引用的分支。
   逐项转向当时的 main 并核查差异，确认最终包含全部修复。不要把 #149 单独发布或结束交付。
3. 最终 main 必须包含验收 tip，生产树须与验收 tip 相同；若合并处理改变代码，重新构建并复验。
   等待最终合并 head 的完整双平台 CI，而不是把旧 head 成功冒充合并后成功。
4. 同步状态、关闭本次交付门禁，再做下一玩法产品评审。合并若中断，明确标记未完成，不称已交付。

以上为待授权的操作说明，本轮未改 PR base/Draft 状态、合并、删除分支或重写历史。
无关 Draft #70 与 Week29 保持不动。

## 剩余工程债及重新开启条件

| 项目 | 当前判断 | 何时开启 / 应有证据 |
| --- | --- | --- |
| Daily 直接转向 | 保留的玩法/规模差异，非生命周期缺陷 | 用户明确要求绕障调查或实际卡死复现；单独行为合同及真实 Daily 路径回归 |
| 全局 combat frame / Profile 提交顺序 | 本次未统一，不能宣称完全共享原子帧 | 可复现的同帧消耗/伤害/终局不一致；先 red test，再收窄事务边界 |
| Profile 已接受、runtime 构造失败 | 现有 blocked-recovery，不新增全局回滚保证 | 真实恢复缺口或失败注入用例；只修接受边界，不重写全部 Persistence |
| App / GameSession / GameplayWorld 偏大 | 按消费者拆分的维护债 | 修改热点或新消费者重复有证据；逐片迁移，不按文件大小盲拆 |
| 大规模寻路/射击宽阶段、固定步长 | 当前性能门禁通过，未作无限规模承诺 | 超预算的目标内容与可重复 P95/P99 数据；不能用假设启动整套框架 |
| 历史 V0 / 局部索引 | 停止新增消费者，现有回归仍依赖 | 明确替代与删除覆盖；稳定 ID 已用于跨模块生命结果，不要求抹去所有循环索引 |
| 新活动接入 | 当前没有第四个战斗消费者 | 复用 roster/contact/shooting；把真实适配器加入同一合同，再决定是否提取调度 |

没有新的必答产品级问题阻塞本次代码收尾。异常退出长期规则、武器热键和居民公共医疗库存
仍是既有产品议题，不在本轮修改。

## 下一步与回滚

下一项具体工作是**授权后的依赖链合并交付**：让防守功能与生命周期、伤害、时序和切换修复
一起进入 main，完成最终 CI 和状态同步。新玩法仍暂停。之后只提交一份产品评审：是否继续
原候选“固定防御位与少量基础工事”，先明确位置合法性、碰撞/导航、费用/回收、安全核心和
防守冻结快照合同；该候选不是本次实施授权，不启动 NPC 守军或更广内容。

本报告可独立 revert，不影响程序。#150–#156 没有提升 schema，仍复用 #149 的 v46；
#151 的 content v60 新增对 v59 的向后兼容，不保证旧程序读新内容。若撤销整条依赖，按逆序普通
revert 并核查存档兼容；回到 #149 前的 main/v45 必须使用升级前另存的备份，不能把已滚动
升级的安全备份当作 v45 档。不得靠回滚让已知缺陷版本成为长期发布基线。
