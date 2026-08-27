# Regional Operations — 失物记录与老化 v1

## 产品结果

死亡或主动放弃 Raid 后，携带资产不再被无痕删除，而是以原实例、原容器树和原弹匣安装关系进入一条独立失物记录。Base 的 Raid Gate 提供独立记录页；每次后续 Raid 真正结算时记录老化，完成三次后续行动后进入最后机会，第四次结算时永久失效。出击前必须提示即将失效的记录。

## 基线与分支

- 基线：`origin/main@d7c231b`，PR #100 的 Raid World 可扩展性能基础已通过用户验收并普通合入。
- 分支：`codex/regional-loss-records-v1`。
- 当前 schema v23、content v33；本切片升级为 schema v24、content v34，并兼容迁移 v23/v33。

## 领域合同

- `AssetRegistry` 继续唯一拥有所有资产实例。失败结算只把当前携带树的装备根迁移到 `LostRaidAssetLocation`；容器内容与已安装弹匣保留父子位置。
- 每条 `LostRaidRecord` 冻结 Record/Settlement/Raid ID、地图 DefinitionId、难度、失败类型、创建世界分钟及后续结算次数。
- 死亡与主动退出创建记录；空装失败不制造空记录；异常退出继续恢复精确的 Raid 前存档，不创建记录、不老化记录。
- 老化只由未重复的终局 Settlement 触发。新建记录不计当前失败；已有记录在后续成功、死亡或主动退出结算时增加一次。计数 0～3 可恢复，下一次结算永久删除记录及其全部资产树。
- Settlement 重放、非法 Profile、revision 溢出或保存失败均不重复创建、不重复老化、不丢失资产。
- 本切片只建立记录和时间压力，不提供 NPC 委托或 Raid 内自力寻回。

## 实施与退出条件

1. 新增失物位置、记录、纯查询投影和状态指纹；Profile 校验拒绝孤儿记录、未知地图、非法失败类型、重复来源槽和悬空资产。
2. 失败 Settlement 原子迁移携带根，老化既有记录并删除到期树；成功与失败都驱动老化，异常回滚不驱动。
3. schema v24 保存记录、失物位置及最后结算关联；v23 正常迁移为空记录集合。
4. RaidResult 只提示已创建记录；Raid Gate 独立页显示记录、地图、难度、剩余行动窗口和资产摘要。出击前显示到期警告并要求再次确认。
5. focused tests、Windows Debug 全目标、完整 CTest 与 exact-head Windows/Ubuntu CI 全部通过后交给用户正常游玩验收。

## 明确排除

- 不实现 NPC 寻回任务、委托价格/时长、取消、多个任务槽、保险或 NPC 升级。
- 不实现 Raid 内尸体、标记、原地图自力寻回、敌对幸存者拾取装备或完整尸体生命周期。
- 不改变异常退出回滚合同，不增加路线图、哨所、载具、正式美术、音频或 manifest。

## 回滚与验收

- 交付一个 Draft PR；合入前关闭即可弃用，合入后普通 revert merge commit。schema v24 存档不能由旧构建读取，因此回滚测试使用自动安全备份。
- 自动化完成后，用户正常游玩一次死亡/主动退出、查看 Base 记录、连续完成后续 Raid 并观察警告与到期即可；开发代理不启动游戏。

## 进度

- [x] 基线、失败边界、老化窗口和排除项冻结。
- [x] 领域所有权与失败 Settlement；普通库存和经济命令不能越权使用失物。
- [x] schema v24、v23 迁移和损坏拒绝。
- [x] Base 记录页、RaidResult 提示与出击警告。
- [x] focused tests、Windows Debug 全目标、1106/1106 CTest 与文档。
- [ ] exact-head Windows/Ubuntu CI 与用户正常游玩验收。

## 自动化证据

- Windows Debug 全目标构建成功，包含 `Project_Raidline.exe`；开发代理未启动游戏。
- `AlphaHardeningTest`、`EconomyDomainTest` 与 `LostRaidDomainTest` 定向 13/13 通过。
- 完整 Windows Debug CTest 1106/1106 通过，覆盖失物树迁移、普通命令不可访问、0～3 次老化、第四次删除、Settlement 重放、异常退出回滚、schema v24 往返、v23 迁移和未知地图拒绝。
- exact-head Windows/Ubuntu CI 等待 Draft PR 创建后执行；人工验收仍由用户在最后进行。
