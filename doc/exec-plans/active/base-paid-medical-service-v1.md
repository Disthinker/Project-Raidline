# Base 付费医疗服务 v1 ExecPlan

最后核对：2026-08-25。

## 产品结果与范围来源

玩家可在 Base 的独立医疗设施支付普通货币，立即恢复至 100 HP 并清除当前流血。服务不读取、不锁定、不移动也不消耗 Stash、胸挂、背包或装备中的医疗物；玩家仍可在个人物品栏右键使用明确医疗物实例自疗。

唯一产品事实来源为只读 GDD 的 `systems/13_生命状态与医疗.md`、`03_系统设计完成度盘点.md` 与 `04_阶段式开发与减法规划.md`。本切片不实现骨折、内伤、NPC 治疗、公共医疗库存、人口、治疗队列、世界时间消耗、新美术或新音频。

## Git 与依赖基线

- PR #83 已通过 exact-head Windows/Ubuntu CI 和用户正常游玩验收，以普通 merge commit `20d9f48` 进入 `origin/main`。
- 当前分支 `codex/base-paid-medical-service-v1` 从干净的 `origin/main@20d9f48` 创建。
- content 从 v18 升至 v19；存档 schema 保持 v11，并兼容读取 v16、v17、v18 内容版本。
- 外部 GDD 保持只读。其枪匠文档仍残留“等待完成”的旧描述，与已接受的即时枪械维护冲突；本仓库只记录冲突，不静默改写策划库。

## 状态所有权与领域合同

- `ProfileState` 继续唯一拥有 HP、流血、止痛状态、货币、revision 与已提交事务。
- ContentRegistry 新增玩家基地医疗定价：每点缺失 HP、轻度流血、重度流血三项价格。
- `queryBaseMedicalService` 只读返回当前 revision、费用拆分及能否提交。
- `executeBaseMedicalService` 在候选 Profile 上原子扣费、恢复 HP、清除流血来源并推进 revision；保留止痛药剩余时间。
- 健康、货币不足、Raid pending、空事务、过期 revision、revision 溢出和存档失败都保证内存 Profile 与资产不变。
- 世界时钟、Base 资源、资产 ID 高水位、物品次数、Base 服务任务和任务 ID 高水位均不参与交易。

开发期定价为：缺失 HP 每点 3、轻度流血 30、重度流血 60。它相对现有 Medkit、绷带与止血带提供即时便利溢价，但不替代玩家物品自疗。

## 实施步骤与退出条件

1. 同步 PR #83 的接受证据与路线状态；建立本计划。
2. 增加 content v19 定价定义、领域 query/execute、GameSession 持久化包装；领域和保存失败测试通过。
3. 在 Base 右下区域加入文字与几何占位医疗设施；E 打开服务页，正常点击支付治疗；中英文投影完整。
4. 补齐 ContentRegistry、BaseWorld、持久化和本地化回归；所有拒绝路径验证 Profile 指纹不变。
5. Windows Debug 全目标、全量 CTest 与 exact-head Windows/Ubuntu CI 通过后，交给用户进行正常游玩验收。

## 自动化与人工验收

自动化覆盖：HP-only 报价、轻/重流血、即时治疗、止痛状态保留、货币和资产守恒、健康/余额不足/Raid pending/过期 revision/重复事务、保存失败不提交、跨进程重载、内容校验、设施碰撞/交互和中文文本。

人工验收只在自动化与 CI 后进行：在 Base 受伤状态进入医疗设施，确认报价、支付和即时恢复；确认货币减少、个人医疗物数量/次数不变；健康时不能重复付费；重启后状态保持。开发代理不启动游戏。

## 提交、回滚与风险

- 本切片只在一个分支和一个 PR 内交付，不合入 NPC 医疗或伤势扩张。
- 回滚可整体撤销 content v19、医疗领域、设施/UI 与测试；schema 未升级，旧 v18 存档保持可读。
- 主要风险是把玩家付费医疗与物品自疗混用。通过独立命令、资产指纹测试和界面明确文案防止退化。

## 进度

- [x] PR #83 通过用户验收并普通合入 main。
- [x] 从最新干净 main 建立独立开发分支。
- [x] content 与领域事务完成。
- [x] Base 设施与双语界面完成。
- [x] 自动化、Windows 构建和全量 CTest 913/913 完成。
- [ ] exact-head CI 完成。
- [ ] 用户正常游玩验收完成。
