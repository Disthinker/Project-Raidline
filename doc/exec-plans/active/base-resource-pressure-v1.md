# Base 资源分配与基础需求 v1 ExecPlan

## 产品结果与范围

成功撤离后，本局新取得的物品不再直接混入个人长期仓库，而是进入 Base 的“待分配区”。玩家在新的资源分配设施中逐件决定：保留到个人 Stash，或不可逆地捐献给基地并转化为食物、卫生、士气、安全四类资源。每次完成一次 Raid 结算，基地消耗一轮小额活动需求；资源不足只形成清晰短缺提示，不阻止出击、不损坏库存、不导致死档。

本切片使用 SDL 代码绘制的文字、色条、边框、方块与简单图标占位。不得生成或接入正式美术，不修改美术 manifest，也不扩张 P0 音效包。

## 基线、依赖与排除

- 分支：`codex/base-resource-pressure-v1`，基线 `origin/main@d106193`。
- PR #77 已通过 exact-head CI、用户正常游玩验收并以普通 merge 进入 main，提供版本化物品克重与高危轻装撤离。
- 原 Core Extraction Alpha 文件继续作为历史范围合同；当前切片属于 Alpha 后的 Base Growth 最小闭环。
- 明确排除：人口、设施等级、建设队列、WorldClock、五日周期、愿望、任务、NPC、自动消耗、随机事件、库存损坏、资源惩罚、派系与正式题材包装。
- Raid 退出/异常恢复合同本轮不变；失败记录与寻回另立切片。

## 权威状态与命令合同

- `ProfileContainerKind::BaseIntake` 是待分配资产的独立长期位置；它不是 Stash，也不计入随身重量。
- `BaseResourceState` 保存四类 0～100 资源、最近一次需求短缺和已结算 Raid 数量，并进入 Profile 指纹与 schema v7 存档。
- `ItemDefinition::baseContribution` 是版本化内容能力。只有明确声明贡献的物品能被捐献；显示名称、颜色或物品大类不决定合法性。
- 成功 Settlement 根据 pending Raid 的冻结 Loot 记录，把实际带回的新物品原子迁入 BaseIntake；失败只清理随身/地面资产。无论成功失败，同一 SettlementId 只应用一次需求消耗。
- `queryBaseResourceContribution` 与 `executeBaseResourceContribution` 负责捐献预览与提交。捐献完整消耗所选待分配资产；若任一贡献会溢出上限、资产不在 Intake、容器非空、revision 过期或保存失败，Profile、资源、ID 高水位全部不变。
- 保留使用既有 InventoryCommand，把待分配物品移动到 Stash；Stash 无合法空间时零修改。
- 待分配区非空时禁止开始下一次 Raid，避免未处理物品跨局混淆。

## 玩家交互与占位表现

- Base 新增 `ALLOCATION & NEEDS` 交互设施。
- 页面左侧以四条色条显示资源、状态档位、下一次活动消耗与最近短缺；右侧列出待分配物品及其贡献。
- 玩家点击物品后选择 `KEEP IN STASH` 或 `CONTRIBUTE TO BASE`。按钮是正式占位交互，不是测试/验收脚手架；没有合法贡献时捐献按钮明确显示不可用原因。
- Base 常驻 HUD 显示四项资源的紧凑文字值；资源区所有视觉均为代码 fallback。

### 用户验收加固修订

- Base 停止移动时保留最后一次水平朝向；只进行上下移动不会把已确认的左右朝向重置为默认左向。静止右向复用已批准水平移动图集的首帧，不新增角色资源。
- Storage、Supply、Allocation 与 Raid Gate 都是 Base 的实体障碍。玩家矩形按 X/Y 两轴独立解算，四向与斜向都不能穿越，同时允许被阻挡轴之外的贴墙滑动。
- 玩家可见文本统一经过 SDL client 地化边界；首次运行默认简体中文，设置页可切换 English/简体中文。语言选择保存在独立 `settings.json`，不进入 Profile schema、revision、领域指纹或 Raid 结算。
- Windows 首发客户端使用系统自带微软雅黑生成缓存 SDL 文字纹理，不提交字体或新美术资产；Linux 继续只承担编译和领域测试兼容性，不增加同步发行承诺。

## 自动化、人工门槛与回滚

- Domain：合法捐献、非法位置、无贡献、非空容器、容量溢出、重复事务、过期 revision、拒绝前后指纹不变。
- Settlement：成功 Loot 进入 Intake、失败不产生 Intake、需求消耗一次、重复 Settlement 不重复消耗、待分配区阻止 Deploy。
- Persistence：schema v7 往返、schema v6 默认迁移、BaseIntake 位置、资源短缺、损坏/未知容器拒绝。
- Client/flow：第四设施可交互；保留/捐献路径与资源条投影；Esc 返回 Base；原 Storage/Supply/Raid Gate 无回归。
- 完成 focused tests、Windows Debug 全目标、完整 CTest 与 exact-head Windows/Ubuntu CI 后，最后交给用户进行一次正常游玩验收；开发代理不启动游戏。
- 回滚使用普通 revert；不得改写历史。旧 schema v6 档案迁移为默认 Base 资源状态，不删除用户资产。

## 进度

- [x] 2026-08-24：PR #77 用户验收后普通合入；从 `origin/main@d106193` 创建独立分支并冻结范围。
- [x] 2026-08-24：完成 BaseIntake、四资源状态、冻结 Loot 来源、Settlement 需求消耗、schema v7、贡献命令与服务保存边界。
- [x] 2026-08-24：完成第四 Base 设施、待分配选择、保留/捐献、资源色条与常驻紧凑投影；只使用文字和代码图形。
- [x] 2026-08-24：Windows Debug 全目标构建成功，完整 CTest 834/834 通过；开发代理未启动游戏。
- [x] 2026-08-24：修复静止朝向和 Base 二维设施碰撞，接入独立中英文设置与统一玩家文本渲染；Windows Debug 全目标构建成功，focused 11/11、完整 CTest 841/841 通过，开发代理未启动游戏。
- [ ] 完成 PR、exact-head CI 与用户正常游玩验收。
