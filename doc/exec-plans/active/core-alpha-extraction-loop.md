# Core Extraction Alpha：Extraction Loop ExecPlan

## 1. 产品结果与完成定义

本宏切片把已接受的 Persistent Base 与固定 Raid 连接成第一个真实资产闭环：玩家在 Base 装填弹匣、安装主武器/胸挂/背包和医疗物资，持久化 Deploy 后进入一张无硬时限的固定地图；射击消耗枪膛/弹匣，换弹只访问胸挂，治疗消耗真实 Medkit，Loot 进入真实随身容器；撤离保留全部合法随身资产，死亡、主动退出或异常退出全损，结算只发生一次并可继续下一局。

完成门槛：

- 同一个 `ProfileState::AssetRegistry` 在 Base、Deploy、Raid、Loot 和 Settlement 全程唯一拥有资产；禁止复制到旧 `ItemInstance` 库存。
- 弹药在散装堆、弹匣有序序列、枪膛和已消耗计数之间守恒；失败命令不改变资产、高水位或弹药。
- 玩家为 100 HP；Medkit 每件 3 次、每次 30 HP、Raid 内 5 秒，完成提交一次，合法中断不消耗。
- 一张固定几何地图每局冻结一组出生/撤离、一组 4～6 敌人部署和 6～9 个有效 Loot 插槽；同一快照重载不重抽。
- Raid 无时间失败；成功、死亡、主动退出和加载未结算 Raid 均通过唯一 Settlement ID 幂等落档。
- 自动化、Windows Debug 全量 CTest 与 exact-head Windows/Ubuntu CI 通过；最后由用户执行一次集中真实窗口验收。

## 2. 基线、依赖与排除

- 基线：`origin/main@b1ea3c3`，PR #58 已通过自动化与用户 6/6 验收并合入。
- 分支：`codex/core-alpha-extraction-loop`。
- 唯一范围合同：外部只读 GDD `05_Core_Extraction_Alpha_首阶段功能规格.md`。
- 不实现特殊弹、混装弹 UI、完整逻辑弹道替换、防具/部位/复杂伤势、耐久/故障、多地图、高危、特殊撤离、增援、尸体搜索、任务或正式美术/音频。
- 当前 `Projectile` 仅继续作为短期空间表现适配器；武器、弹药、伤害、存档和 UI 不依赖该类型。

## 3. 权威状态与生命周期

- `AssetRecord` 增加当前消费者需要的实例状态：弹匣有序 `AmmoDefinitionId` 序列、武器可选枪膛弹药；已安装弹匣通过封闭 `InstalledMagazineLocation{weaponId}` 表达，不复制所有权。
- `AssetLocation` 增加 Raid 地面位置；Stash 在 Raid 不可访问，装备根、其容器子资产和武器弹匣构成随身资产树。
- `ProfileState` 增加当前 HP、可选 `PendingRaidSnapshot`、已提交 Settlement ID 和最近一次 RaidResult。存档升级为 schema v2，schema v1 显式迁移到 HP=100、无 pending Raid。
- `PendingRaidSnapshot` 保存 Raid/Settlement ID、规则版本、Map ID、随机种子、冻结配置 ID、出生/撤离、敌人列表、Loot 槽与实例 ID、带入根和开始 HP；禁止保存 UI 格位或指针。
- Deploy 先在候选 Profile 中生成快照与地面 Loot、验证所有权并保存，成功后才创建运行时并进入 Raid。
- 成功结算删除未拾取地面 Loot，保留装备树和已拾取物，保留 HP；失败删除全部随身树和本局 Loot，清空三槽并恢复 100 HP。
- 加载带 pending Raid 的存档直接用同一 Settlement ID 幂等失败结算；不恢复局内进度。

## 4. 领域命令与动作

- `WeaponAmmoDomain`：Base 压弹、Base 卸弹、安装/卸下弹匣、尝试上膛/击发、查询出击状态。压卸弹和击发在候选状态原子提交。
- 普通击发：空膛但有供弹时先上膛且不产生伤害；成功击发消耗枪膛一发，并从已安装弹匣自动补入下一发。
- `RaidAction` 使用类型安全变体，只注册 Reload、Heal、Extract。换弹 2 秒、治疗 5 秒、撤离 3 秒；动作完成是唯一提交点，中断前不修改资产。
- R 只选择装备胸挂弹匣袋内的兼容弹匣，按剩余弹量降序、稳定实例 ID 升序；完成时原子交换已安装弹匣与候选原口袋。
- 快捷医疗只选择胸挂通用袋中的 Medkit；`5` 打开单类轮盘壳层，释放后开始治疗。库存物品菜单可选择胸挂或背包 Medkit，并调用相同命令。
- 随身库存只显示装备槽、胸挂与背包；打开时允许普通移动，但禁止冲刺、射击、换弹和开始治疗。

## 5. 地图、随机与模拟适配

- Content Registry 扩展一个固定 Alpha MapDefinition：3 个经过配对的出生/撤离配置、3 个不同压力形态的 4～6 敌人部署、8～12 个固定 Loot 插槽及路线标签。
- 使用仓库内跨编译器稳定 PCG32；Map、Deployment、Loot 使用命名流。最终抽取结果全部写入快照，运行时不再随机。
- `GameplayWorld` 只消费冻结的出生、撤离、敌人与 100 HP，继续复用移动、AI、攻击、射击解析和代码表现；禁用 V0 柜体/地面物所有权和 Timeout 结算。
- Profile Raid Loot 以代码 fallback 渲染；E 拾取到装备胸挂/背包的首个合法位置。正式资源生产仍暂停。

## 6. 实施与回滚边界

1. **领域提交**：扩展 Asset/Profile、WeaponAmmo、Action、RaidSnapshot、Settlement、schema v2 与 focused tests。
2. **模拟/服务提交**：冻结地图配置、Deploy、无时限 GameplayWorld 适配、拾取、动作推进、成功/失败/异常恢复。
3. **客户端提交**：Base 压卸弹/安装弹匣、出击摘要/二次确认、Raid HUD/换弹/医疗/随身库存、主动退出、RaidResult。
4. **证据提交**：项目状态、总计划、构建证据和集中验收清单。

每个提交保持可构建。若客户端需回滚，领域和服务测试仍可独立保留；若新 Settlement 不可靠，整分支不合入，不能回退为双资产复制路径。

## 7. 自动化门槛

- 弹药守恒：随机压弹/卸弹/安装/上膛/击发序列后，散装+弹匣+枪膛+消耗恒定；拒绝前后指纹一致。
- 换弹：候选排序、胸挂限制、空/满/不兼容、容量失败、完成与任意中断；每个实例始终恰有一个位置。
- Medkit：满 HP 拒绝、30 HP 上限、3 次、完成一次、中断不消耗、死亡中断、Base 即时使用。
- 快照：同 seed 同结果，三个配置均可达，6～9 Loot、三路线有回报、部署 4～6 且无重抽。
- 生命周期：Deploy 保存失败不进入；成功保留；死亡/主动/异常全损；Settlement 重放与重复加载只提交一次。
- 模拟：100 HP 伤害、无硬时限、3 秒撤离中断、库存打开时输入限制；现有射击/AI/攻击回归。
- 存档：schema v1→v2、v2 往返、pending Raid、坏主档恢复后异常失败结算、未知定义/重复 ID/重复 Settlement 拒绝。
- 全量 CTest、Windows/Ubuntu exact-head CI，且生产领域/模拟继续 SDL-free。

## 8. 集中人工验收（自动化及 CI 后由用户执行）

1. 新档在 Base 装备三槽，对弹匣压弹/卸弹并安装弹匣；出击摘要正确区分可击发、需上膛、无弹，空手/无弹需二次确认但可进入。
2. Raid 内完成上膛/击发、至少一次 R 胸挂换弹；弹量显示与实际射击一致，背包弹匣不会被 R 选择。
3. 受伤后用 `5` 胸挂 Medkit 完成一次治疗，并验证移动/攻击中断及次数变化；打开随身库存时只能普通移动和整理。
4. 体验三类路线并拾取 Loot；同局搜索点不刷新，清敌后可安全搜索且没有 Timeout。
5. 成功撤离，确认装备、容器位置、拾取物和 HP 保留；出售 Loot/补弹后再次出击。
6. 分别验证死亡、主动退出、强制关闭后重开：本局资产全损且只结算一次，玩家 100 HP 在医疗点恢复控制。
7. 至少完成第二次 Raid，确认前一局结果、Loot、货币、救济和 Settlement 不重复。

## 9. 进度记录

- 2026-08-15：用户以“下一步”授权执行已公布的合并步骤；PR #58 以 merge commit `b1ea3c3` 进入 main。
- 2026-08-15：从干净的 `origin/main@b1ea3c3` 创建 `codex/core-alpha-extraction-loop`，重新读取唯一 Alpha 范围合同并固定本计划。
- 2026-08-15：领域合同提交 `2d9b96d` 完成 content v2、稳定 PCG32、WeaponAmmo、RaidAction、RaidLifecycle、schema v2 与 focused tests。
- 2026-08-15：端到端提交 `66f3120` 接通 Base 弹药/医疗、持久 Deploy、Alpha GameplayWorld、真实射击消耗、换弹/治疗/Loot/随身库存、四类结算与 RaidResult；生产 Alpha 不再使用 V0 柜体、无限弹和 Timeout 结算。
- 2026-08-15：补齐普通/救济弹堆隔离，以及 Raid Loot 合并或装填后历史快照仍合法的所有权规则。Windows Debug 构建成功，focused 17/17、全量 CTest 620/620 通过；开发代理未启动游戏。
- 待完成：Draft PR exact-head 范围检测、Windows/Ubuntu CI，以及用户执行第 8 节集中真实窗口验收。
