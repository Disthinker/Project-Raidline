# Frontier Exchange 物资身份与 Loot 内容包 v1

## 产品结果与范围

本切片让 `Frontier Exchange` 的六类资源点产生可辨认的主题物资，而不是继续共用两张全局 Loot 表。玩家可以根据基地当前缺少的食物、卫生、士气、安全或建设材料选择公路服务区、维护缓存、货运区、制造厂或高价值货箱，并把成功带回的物品用于现有自动供给、建设材料加工或低价回收闭环。

基线为 `origin/main@7be91e8`，分支为 `codex/frontier-loot-identity-v1`。本轮只使用代码图形和中英文文字占位；不生成或接入新美术，不修改 Manifest。

明确排除：新地图主题、新武器/弹种、商人重做、动态价格、正式资源、程序化室内、AI 小队、实时守城和高危危机扩展。

## 领域合同与兼容

- 新增九种稳定物品定义，覆盖食物、卫生、娱乐、工业材料和高价值零件；每种物品声明重量、格子尺寸、堆叠、回收价及现有 Base 消费能力。
- 六类 `Frontier Exchange` 资源点各引用独立 Loot 表。普通资源点稳定提供生活与基础工业物资，高价值与地点专属资源点提供更重、更稀缺且价值更高的物资；不使用全局价格倍率。
- 新 Deploy 使用 `procedural-frontier-loot-identity-27`，并把最终资源点表和物品实例冻结进 pending Raid。旧规则 23～26 继续按原资源点—Loot 表映射验证，既有 Raid 不重抽、不吞失物品。
- content 升级为 v50，schema 保持 v37；v49、v48 与 v47 显式兼容。新物品沿用唯一 `AssetRegistry` 所有权及现有撤离、失败、寻回、自动供给、建设和回收事务。

## 验证、交付与回滚

- 自动化覆盖稳定 ID、六张专属 Loot 表、物资用途与数值边界、同 seed 冻结一致性、不同资源点产出集合差异、旧 rules 26 pending Raid 读取及新 rules 27 篡改拒绝。
- 完成 Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI 后，由用户通过正常游玩验收；开发代理不启动游戏。
- 回滚顺序为 UI 翻译、内容表、rules 27 兼容分支与文档；使用普通 revert，不重写历史、不强推。

## 进度

- [x] PR #114 经用户验收并以 `7be91e8` 合入 main。
- [x] 审计现有资源点、Loot 表、Base 贡献与 pending Raid 验证边界。
- [x] 实现九种主题物资、六张专属 Loot 表和双语占位。
- [x] 完成 rules 27、content v50 与旧 pending Raid 兼容。
- [x] 完成 focused tests、Windows Debug 全目标与 1287/1287 完整 CTest。
- [ ] 提交、推送 Draft PR 并完成 exact-head Windows/Ubuntu CI。
- [ ] 用户正常游玩验收。
