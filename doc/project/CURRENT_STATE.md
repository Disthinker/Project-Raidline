# Project Raidline 已接受状态

本文只记录已经进入 `main` 并完成所需验收的稳定能力。当前分支、Draft PR、临时 CI、测试数量和待验收修订由 GitHub、自动快照与活动 ExecPlan 负责。

## 产品流程

- `MainMenu -> Base -> Raid -> RaidResult -> Base` 可跨进程反复游玩。
- Base 可行走且安全，提供仓储/配装、供应/回收和 Raid 出击入口；首次环境目标链不阻塞游玩。
- 一张固定 Alpha 地图在出击时冻结出生/撤离、有限敌人和一次性 Loot 快照；Raid 无硬时限。
- 成功撤离保留合法随身资产和状态；死亡/主动放弃全损；程序关闭或异常退出恢复精确出击前档案。
- Settlement 使用唯一幂等键；RaidResult 显示成功带回物与货币变化，失败不生成丢失物清单。

## 资产、库存与经济

- `ProfileState::AssetRegistry` 唯一拥有长期物品；Stash、装备、容器、武器安装点和 Raid 地面使用稳定实例 ID 与显式位置。
- Base 与 Raid 共用领域驱动拖放：移动、旋转、交换、堆叠、配装、卸装、压卸弹和指定弹匣换弹均由查询/命令原子提交。
- 非空容器禁止嵌套；内容跟随容器；失败操作保持 Profile、revision、高水位、货币和弹药不变。
- 当前装备包含两把长枪、手枪、头盔、护甲、胸挂和背包；武器定义声明兼容槽。
- 固定供应、低价回收、普通货币和条件式单份救济已持久化，连续失败不会形成不可恢复死档。

## 武器、战斗与生存

- 真实散装弹药、弹匣有序内容、枪膛、换弹和击发消耗闭环已接入；只有合法兼容弹匣可安装。
- 玩家 100 HP。基础防具、头/躯干/腿命中、流血、疼痛、Medkit/绷带/止血带/止痛药与 Raid 限时动作已接入。
- 武器与防具有耐久、资源化维护；武器支持 Stovepipe 故障与清障。Raid 治疗和维修可按合同缓慢移动并接受中断风险。
- `1/2/3` 在三把武器间限时切换；每把武器保留自己的弹药、耐久与故障状态。
- `ShotCommand -> ShotResolution -> LogicalBallisticFlight -> HitResult` 是生产射击权威；没有可渲染/可碰撞的 Projectile 场景实体。
- 准星中心、后坐力、随机散布和右键瞄准由 simulation 计算；玩家必须手动压枪。逻辑弹道连续扫掠最近目标/障碍，并在最大距离形成 Ground 结果。
- 普通命中不显示准星 X；命中部位、防具、伤害和反馈由领域结果驱动，App 不猜测。

## 输入、表现与音频

- Active Raid 使用 SDL 相对鼠标位移，模态 UI、失焦和终局会释放捕获，避免 OS 光标离窗阻断连续压枪。
- Base/Raid 显示现有批准主角资源；左右移动复用现有六帧，其他方向仍使用静态 fallback。
- `assets/audio/v1` P0 Sound Event 库提供枪械、库存、医疗、感染者和 Base/Raid 环境的最小闭环；音频失败静默降级且不反向驱动领域状态。
- 正式 Grab/Scratch/Bite 动画和未另行授权的美术/音频生产仍暂停。

## 架构、内容与存档

- 四个生产库为 `raidline_domain`、`raidline_simulation`、`raidline_services`、`raidline_sdl_client`；每个 production cpp 只有一个编译所有者，domain/simulation 禁止 SDL。
- ContentRegistry 从版本化 JSON 加载稳定命名 DefinitionId，并验证重复 ID、非法引用、数值、地图、价格与发布资源。
- Save schema 已逐步迁移到 v6，保存稳定 ID、关系、版本、高水位、医疗/装备/武器状态和结算凭证；主档使用校验、备份和原子替换。
- 旧 V0 ItemId、3 HP、180 秒 Timeout、无限弹和旧 RaidSettlement 只服务历史回归，不得增加新消费者。

## 已接受交付序列

- Core Extraction Alpha：PR #55～#60。
- Survival Loadout：PR #61～#65。
- Combat 逻辑弹道、准星/调参与输入捕获/P0 音频：PR #66～#68。

精确提交、构建数量、CI 与人工证据保存在对应 completed ExecPlan 和 PR，不在本文重复维护。
