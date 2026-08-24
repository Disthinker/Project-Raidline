# Raid 高危条件撤离 v1 ExecPlan

## 产品结果与范围

三张固定地图在持续高危阶段同时开放两条紧急撤离路线：现有信号撤离继续提供无资源门槛但较长的等待；新增轻装条件撤离提供更短读条，但只接受当前随身总重量不超过地图限制的玩家。玩家由此需要在高级 Loot 收益、装备重量和撤离路线之间做可见取舍。

本切片建立物品单位重量和权威随身重量查询。重量包含已装备根物品、胸挂/背包内容、安装弹匣、弹匣内子弹和枪膛弹；每个资产与每发弹药只计算一次。v1 只把重量作为条件撤离资格，不提前加入负重移动惩罚、体力、燃油、凭证、随机危机、情报或随机地图。

不新增正式美术/音频，不修改美术 manifest。条件撤离区使用代码表现，正式题材包装后置。

## 基线与依赖

- 分支：`codex/raid-conditional-extraction-v1`，基线 `origin/main@bc26337`。
- PR #76 已通过 exact-head CI 和用户正常游玩验收并普通合入，提供可主动触发的持续高危、高级资源区和互不重叠的关键区域。
- 外部 GDD 只读；已确认高危可以随机开放燃油或负重条件撤离，条件资源只能在撤离成功的原子事务中消耗。v1 选择不消耗资源的负重上限路线，避免在燃油内容与经济来源尚未冻结时引入占位消耗品。

## 权威状态与数据合同

- `ItemDefinition::unitWeightGrams` 是版本化内容事实；ContentRegistry 拒绝零重量和总重量溢出风险。
- `carriedWeightGrams(ProfileState, ContentRegistry)` 是唯一随身重量查询；App 不遍历资产或猜测容器归属。
- `HighRiskRaidDefinition` 保存轻装撤离区域、读条时长和重量上限；所有关键交互区在内容加载时验证边界、障碍与彼此重叠。
- `GameSession` 每个逻辑步在 Profile revision 最新状态上计算资格，并作为只读输入交给 `GameplayWorld`。掉落/拾取/库存变化会在下一逻辑步刷新资格。
- `RaidSession` 显式区分 `EmergencySignal` 与 `EmergencyConditional` 路线；离开区域、失去资格、死亡或控制状态中断会清空读条，终局仍只提交一次。

## 实施与回滚边界

1. 为内容定义增加单位克重、三图轻装撤离配置、content v12 与验证器。
2. 增加权威随身重量查询与资产/弹药去重回归测试。
3. 扩展 RaidSession、GameplayWorld 和 GameSession 的条件撤离资格与路线状态。
4. 接入路线区域、重量/门槛和读条的代码 UI 投影。
5. 更新 CURRENT_STATE、ROADMAP、KNOWN_ISSUES 与本 ExecPlan；focused tests、Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI 后交给用户正常游玩验收。

回滚使用普通 revert，不改写历史。旧 schema v6 存档不新增运行字段，继续兼容；content v11 只作为旧存档内容版本标记保留。

## 自动化与人工门槛

- ContentRegistry：所有物品重量为正；三图条件撤离区域合法且不与普通/信号/控制/高级资源/障碍重叠；时长和限制非法时拒绝。
- Profile：装备、容器内容、安装弹匣、弹匣子弹和枪膛弹全部计重且不重复；数量和弹药变化即时反映；溢出安全。
- RaidSession：高危前关闭；高危后轻装可开始并按独立时长完成；超重、离区和中途增重取消；信号路线无回归。
- GameplayWorld/GameSession：Profile 当前重量驱动资格；地图配置传递完整；轻装撤离成功仍走原有唯一 SettlementId 结算。
- 人工验收：在任一地图进入高危，比较信号与轻装撤离；轻装时完成快速撤离；拾取到超重后确认轻装区明确拒绝且信号仍可用；丢弃/整理回限重以下后重新读条并成功结算。

## 进度

- [x] 2026-08-24：PR #76 用户验收后普通合入，核对 GDD、主线、内容与撤离边界，冻结轻装条件撤离 v1。
- [x] 2026-08-24：完成 content v12 单位克重、三图轻装撤离、权威随身重量、独立路线状态、服务资格输入和重量/读条 UI。
- [x] 2026-08-24：完成 Windows Debug 全目标构建、定向 8/8 与完整 CTest 825/825；开发代理未启动游戏。
- [ ] 完成 exact-head CI 和用户正常游玩验收。
