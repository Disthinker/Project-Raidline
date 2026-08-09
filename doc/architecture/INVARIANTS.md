# Project Raidline 不变量

这些规则是实现、测试和审查的行为底线。修改规则需要明确产品决策、测试变化，并在必要时更新 ExecPlan；不能通过实现细节或 include 顺序绕开。

## 生命、命中与容器遍历

- Health 的最大值大于 0，当前值始终在 `[0, max]`；非正伤害不生效。
- `takeDamage` 只在 alive→dead 的那次调用报告死亡；已经死亡的对象不重复结算。
- Player 唯一拥有默认 3 HP 的 Health；GameplayWorld、GameSession 与 App 不保存第二份玩家生命。
- 只有活动 Raid 接受玩家伤害；任何 GameplayWorld 级死亡入口都必须同时得到 Player HP 0 与 sticky `PlayerDead`，终局后伤害无副作用。
- 敌人与玩家单纯重叠不再产生被动接触伤害；普通伤害来自 Scratch Active，特殊高伤害来自 Grab 接触后原子转换得到的 Bite Active。Grab 本身伤害必须为 0，AI 不得独立选择 Bite。
- Scratch/Bite 严格经历 `Windup → Active → Recovery → Idle`；未命中的 Grab 经 `Windup → Active → OffBalance → Idle`。一次动作链最多消费一个伤害命中，Windup、Recovery、OffBalance、Idle 和已消费 Active 均不能重复伤害。
- Grab Windup 可按最新有限目标方向追踪并使用 Normal 速度；进入 Active 后方向锁定，按实际消费时间累计固定突进距离并夹在世界边界。接触后立即停止位移并转换为 Bite；空冲必须保持有限的长失衡。攻击致死发生在玩家射击、投射物推进、命中和计分之前，并立即停止本帧这些后续 mutation。
- 咬命中存活玩家时施加有限短暂控制；Player 唯一拥有控制剩余时间，重复控制取较大剩余值而不累加。受控帧抑制移动、射击和世界交互，到期后的下一帧自然恢复；瞄准和 UI 生命周期不由控制状态复制持有。
- Enemy 唯一拥有确定性的 `EnemyAiState` 与 `EnemyAttackState`。首次接敌和普通近战默认 Scratch；Grab 只有在至少启动一次 Scratch 后、目标连续处于特殊中距离带达到阈值且冷却完成时才能请求，启动后消耗本次武装。除 Grab Windup 允许追踪外，动作阶段不重新选招。
- 感知严格使用 `Unaware → Alerted → Searching`：获取半径小于丢失半径；`Searching` 只能追踪进入搜索前冻结的最后已知世界位置，到达或 2 秒记忆耗尽后清除记忆并回到 `Unaware`。只有 `Alerted + Engage` 可请求新攻击。
- 每个敌人子步先捕获全体 Enemy 值快照并计算全部协调指令，再开始任何 Enemy mutation；同一快照至多一个 Engage。等距按稳定 vector 槽位选取，活动 Windup/Active/Recovery 保留攻击权，死亡、Searching、Unaware 与 OffBalance 不获得新攻击许可。
- 邻居分离只读取有限、存活的快照位置，输出必须有限且有界；协调层不保存 `Enemy&`、iterator、指针或跨帧状态，默认部署完成后不因协调过程增删 `enemies_`。
- AI 驱动移动只有 `Stationary=0`、`Normal=72`、`Attack=135 px/s` 三档；普通追击与 Grab Windup 使用 Normal，Grab Active 使用 Attack，其余攻击阶段和 OffBalance 使用 Stationary。
- Player 与 Enemy 各自唯一拥有 0.18 秒受击减速计时，按真实时间衰减并把受影响区间的移动/动作时间乘以 0.28；非致死命中刷新计时，死亡不保留减速。Bite 控制优先抑制 Player 输入。
- AI 的零、NaN 或 Inf 目标向量不能产生移动或攻击；冷却、位置、方向和阶段时间始终保持有限。死亡、终局和新 Raid 分别停止、冻结和重建 AI/攻击/控制状态。
- AABB 必须有正面积重叠；仅边缘接触不算命中。
- 一发 Projectile 一次最多命中一个 Enemy；死亡 Enemy 不再被命中或重复计分。
- 遍历期间收集结果，完成遍历后再删除容器元素，避免迭代器/引用失效。

## 瞄准、射击与投射物

- `GameplayInput` 只携带 SDL 无关的可选世界瞄准点与 fire held/edge 快照；SDL 坐标采集、屏幕/库存输入所有权和准星绘制留在 App/InputSystem 边界。
- WASD 只决定移动；Player 移动完成后，有效非零有限 aim direction 才能覆盖 facing。中心、NaN 或 Inf 瞄准保留上次有效 facing。
- `GameplayWorld` 唯一拥有 `WeaponFireState`；App 只能读取扩散与可视后坐力，不保存第二份 cooldown、扩散或后坐状态。新 GameplayWorld 自动得到干净状态。
- 第一发精确；持续射击使用项目自有整数序列产生有界确定性角度偏移。cooldown、扩散和后坐始终有限并 clamp 在配置范围内；停止射击经过恢复延迟后回到零。
- 非有限或负 deltaTime 不推进武器状态且不生成 shot；零 deltaTime 可观察一次有效输入边沿。每次 update 最多生成一发，不按大 deltaTime 补发历史子弹。
- Projectile 只在 `WeaponFireState` 返回有效 `ShotSpec` 后创建；方向归一化，生成中心位于玩家逻辑碰撞体沿最终方向的外缘，逻辑 footprint 为方向无关的 8×8。
- 左键被屏幕或库存层消费后，在对应物理释放前不得重新武装 pointer fire；该抑制不影响独立的 Space fire。失焦清除键盘和 pointer held/edge。
- 世界层同时接受 `firePressed` 与 `fireJustPressed`，确保同帧 down+up 的极短有效点击不丢失；UI 消费路径必须在进入世界前清除 pointer edge。
- App 只在活动 Raid、库存关闭且代码准星可用时隐藏系统鼠标；菜单、Base、库存、终局和 shutdown 必须恢复 SDL cursor，不能让进程级光标状态泄漏到 UI 或退出后。
- 投射物拖尾只读 `Projectile::velocity()` 计算反方向视觉采样；弹头、辉光和拖尾尺寸不改变 Projectile 的 8×8 逻辑 AABB、伤害或命中规则。
- 高速 Projectile 在一次世界更新内按有限距离子步进执行“推进→命中→出界删除”；每个子步命中结果先按值累计，不能跨 `vector` 删除保留 Projectile/Enemy 引用或迭代器。子步数量必须有上限，避免异常大 deltaTime 形成无界循环。
- 命中火花只表现既有 `HitResolutionResult::hitPositions`；颜色、长度、数量和寿命调优不得产生第二次伤害、重复得分或改变碰撞位置。

## 资源与动画

- Texture 唯一拥有 SDL_Texture：不可复制、可移动，并在 Renderer 销毁前释放。
- AnimationClip 帧表非空、每帧 duration 大于 0、索引有界。
- Animator 对大 deltaTime 使用跨帧推进；Loop 与 Once 保持各自结束规则。
- Particle duration 与 size 大于 0；非正 deltaTime 不推进；remaining clamp 到 0，normalized lifetime 在 `[0,1]`。
- 固定随机种子只保证相同调用序列下的逻辑可重复；测试不假设不同标准库实现给出相同具体浮点序列。

## 物品身份与所有权

- `ItemId::Count` 是哨兵，不是物品。
- ItemDefinition 是共享静态数据，不拥有 Texture 或运行时实例。
- ItemDefinition 的 `maxStackSize` 至少为 1；未发布视觉资源的逻辑定义不得进入正式世界生成，App 也不得尝试加载空路径或临时占位贴图。
- ItemInstance ID `0` 无效；实例不可复制、可移动，移动后源显式无效。
- 有效 ItemInstance 的 quantity 始终位于 `[1, maxStackSize]`；一个稳定 ID 标识一个完整堆叠 placement，而不是堆内单个单位。
- ItemInstance orientation 只能是 0/90/180/270；不可旋转定义只能使用 0°，移动所有权必须保留方向。
- 稳定 ID 不等于 vector 下标；容器 mutation 后不得使用旧引用、迭代器或下标识别实例。
- GroundItem 和 GridInventory::PlacedItem 各自独占 ItemInstance；拾取、跨容器转移和丢弃不会复制实例。

## 地面拾取

- GroundItem 位置是世界中心，pickup bounds 由玩法数据计算，不从 PNG 透明边界推断。
- 最近物品距离相等时选择 vector 中较早元素，保持确定性。
- 一次 just-pressed 最多拾取一件。
- 拾取先按稳定 placement 顺序合并同定义未满栈，再为余量使用 row-major first-fit；整个地面栈必须一次性进入背包，否则 GroundItem、quantity、ItemInstance ID 和 inventory 均完全不变。

## GridInventory

- 网格宽高为正，`cells_.size() == width * height`，使用 row-major 索引。
- 每个 PlacedItem 的完整 footprint 在界内且不与其他实例重叠。
- 每个占用格 ID 对应唯一 PlacedItem；实例 ID 在 inventory 中唯一。
- `findFirstFit` 按 y 后 x 的 row-major 顺序返回第一个合法位置。
- `canPlace`/`canMove` 是无副作用查询。
- placement 的有效 footprint 由定义基础尺寸与实例 orientation 共同决定；90°/270°交换宽高。
- `tryPlace` 在完全验证成功后才移动输入 ItemInstance；失败不消费输入、不修改 cells 或 placements。
- `canMove`/`tryMove` 允许与自身旧 footprint 重叠；目标为原 origin 是成功 no-op。
- `tryMove` 失败时所有 cells、origin、实例和 ID 不变；成功时先清旧 footprint、写新 footprint、最后更新 origin。
- `canTransform` 是无副作用查询；`tryTransform` 将 origin、orientation 和完整 cells 作为同一事务提交，允许与自身旧 footprint 重叠，失败时三者完全不变。

## 跨容器转移与丢弃

- 跨容器查询无副作用；同一个 `GridInventory` 不能作为转移的源和目标。
- 转移前检查源 ID、目标重复 ID、边界和占用，并在移出源物品前完成目标容量预留。
- 转移成功保持稳定 ID 与 ItemDefinition；失败时两个容器的 placement、cells、顺序和所有权不变。
- first-fit 转移使用目标容器的 row-major 第一个合法位置，结果必须确定。
- 堆叠转移先按目标 placement 顺序填充同定义未满栈，再为余量使用 row-major first-fit；请求数量不能完整容纳时两个容器完全不变。
- 拆分保留源栈 ID 并为新 placement 分配新 ID；合并保留目标栈 ID。新 ID 只由 GameplayWorld 分配，并且只在成功提交实际新 placement 后消耗。
- 普通与快捷 first-fit 转移保留现有 orientation，不自动旋转找空位；显式 transform 转移只有在目标 footprint 合法时才提交新方向。
- 只有玩家背包物品可丢弃；成功后玩家 footprint 清除，世界新增拥有同一实例的 GroundItem。
- 丢弃位置位于玩家逻辑碰撞体的脚底中心，与当前朝向无关，并按物品世界渲染半尺寸限制在世界边界内；失败不改变背包或地面物品。

## 柜体搜索与 Loot

- 柜体是否已搜索是独立生命周期状态，不能由库存是否为空推断；已搜索空柜不得重新生成 Loot。
- LootTable 条目必须引用有效且已发布视觉资源的 ItemDefinition，权重大于 0，数量范围位于 `[1, maxStackSize]`。
- 随机选择使用 `[0, totalWeight)`；测试通过可控 `LootRandomSource` 验证权重和数量边界，不断言不同标准库的具体分布序列。
- LootTable 只生成定义与数量值；相同定义先按最大堆叠规范化，最终 placement 才由 GameplayWorld 分配稳定 ID。
- 首次搜索先在同尺寸临时 GridInventory 完成全部 row-major placement、容量分配和 ID 冲突检查；正式柜体只接受一次完整 move-commit。
- 范围外、随机源非法、ID 冲突、尺寸不匹配或 placement 失败时，柜体库存、搜索状态、世界 ID 序列和其他物品所有权均不变。
- 已搜索重开是成功 no-op，不消费随机值；Tab 打开的 PlayerOnly 背包不得触发搜索。

## Raid 生命周期与撤离

- Raid 配置中的总时长和撤离时长必须为有限正数；Raid 剩余时间与撤离已用时间始终 clamp 在各自合法范围。
- RaidSession 构造后为 Preparing，只有一次成功 start 可进入 InRaid；Extracted、PlayerDead 和 RaidEnded 都是不可逆的 sticky 终局。
- 只有 InRaid 与 Extracting 接受局内计时、玩家死亡命令和 GameplayWorld 玩法更新。
- ExtractionPoint 的位置、尺寸及右/下派生边界必须有限，尺寸为正；contains 使用左/上包含、右/下排除的半开矩形。
- 撤离占用只读取玩家 32×32 逻辑碰撞体中心，不读取更大的渲染 sprite 或 PNG Alpha。
- 玩家中心进入撤离点时开始连续计时；任何一次离开都立即回到 InRaid 并把撤离进度清零，不能跨多次进入累计。
- 大 deltaTime 同时覆盖撤离与超时时，时间上先发生的事件决定唯一终局；完全同时由 RaidEnded 获胜。
- 非正或非有限 deltaTime 不推进 Raid/撤离时钟，但仍允许当前帧占用观察触发进入或离开撤离状态。
- GameplayWorld 在玩家移动后更新 RaidSession；若本帧形成终局，本帧不再提交拾取、射击、敌人、投射物或命中 mutation，后续帧保持冻结。
- App 只能读取并呈现 RaidSession，不得保存第二份状态、倒计时或撤离进度；库存 overlay 打开不会暂停 Raid 时钟，终局时必须关闭 overlay。

## Raid 结算与 Stash

- RaidSettlement 的 Pending/Blocked 不是完成态；Extracted、PlayerDead、RaidEnded 是 sticky 完成态，重复调用不得再次转移、销毁或统计物品。
- Extracted 必须把玩家背包中的每个完整堆叠移动到 Stash，保持稳定 ID、定义、数量、orientation 和源 placement 顺序；结算存入不自动合并堆叠。
- 整背包转移先复制目标占用、验证全部稳定 ID 并规划所有 row-major 目标，再一次性预留目标 placement 容量；容量、footprint 或 ID 冲突时不得移动第一件物品。
- PlayerDead/RaidEnded 必须先记录玩家携带的栈数与单位数，再显式销毁玩家背包中的全部 ItemInstance；Stash、柜体和地面物品不受影响。
- Blocked 必须保持玩家背包和 Stash 的 placement、cells、顺序、ID、数量与方向完全不变，并允许在外部条件改变后重试。
- Stash 仅在 App 进程内存中存在；不能把它描述为已保存到磁盘。Week23 只增加跨 Raid ID、第二局创建和只读 Stash 网格，配装交互与持久化仍属于后续里程碑。

## 跨 Raid GameSession

- GameSession 始终拥有一个有效的当前 GameplayWorld、一个跨局 Stash 和一个仅属于当前 Raid 的 RaidSettlement；GameFlow 是它的唯一所有者，App 不保存这些状态的副本。
- 只有结算进入 Extracted、PlayerDead 或 RaidEnded 完成态后才能开始下一局；活动 Raid、Pending 与 Blocked 均拒绝重开且无副作用。
- 下一局必须先使用旧世界的“下一未使用 ID”完整构造候选 GameplayWorld，再交换所有权；构造失败时旧终局、Stash、结算、Raid 编号和 ID 高水位不变。
- 跨 Raid ID 序列按分配历史单调前进，不能通过扫描当前存活实例最大值重建；已销毁或留在 Stash 的 ID 均不得复用。
- 下一 ID 高水位达到 `std::numeric_limits<ItemInstanceId>::max()` 表示没有可安全递增的新 ID；任何需要创建拆分实例的丢弃、转移或指定格放置必须原子失败，不能分配后回绕到 0。
- 新 Raid 创建新的玩家、敌人、地面物品、柜体搜索状态、RaidSession 和空玩家背包；Stash 中的 ItemInstance 不复制、不重建、不自动进入出战背包。
- Stash 只在当前 App 进程内跨局保留；只读网格不构成配装 UI、磁盘保存或跨进程持久化承诺。

## 顶层 GameFlow

- 顶层状态只能是 MainMenu、Base、Raid 或 RaidResult；App 不保存第二份屏幕状态，也不能直接写状态枚举。
- 只有 Raid 状态可以调用 `GameSession::update`；MainMenu、Base 和 RaidResult 中玩家、敌人、投射物、Raid 时钟与结算都必须冻结。
- MainMenu 只能通过 Start 进入 Base；Base 只能通过 Deploy 进入 Raid；完整结算只能先进入 RaidResult，再通过确认返回 Base。非法转换返回 false 且不修改会话或世界。
- 第一次 Deploy 激活构造时已准备的 Raid 1；以后只有完整结算返回 Base 后的 Deploy 才能调用 `startNextRaid()`，每次成功只增加一个 Raid 编号。
- SettlementBlocked 不得进入 RaidResult、Base 或下一 Raid；它保留当前世界与结算以供既有恢复路径处理。
- Enter/数字键盘 Enter 与屏幕主按钮点击使用单次边沿确认；转换成功的帧必须终止屏幕处理，不能把同一输入继续提交到新屏幕、GameplayWorld 或库存。
- `N` 不再映射为重开；下一局必须经过 RaidResult→Base→Deploy。

## 背包交互

- 预览和按 R 旋转候选都不修改 GridInventory；同容器旋转提交只能通过 `tryTransform`，跨容器旋转提交只能通过 transform 转移服务。
- 背包是纯鼠标交互：方向键、主 Enter 和数字键盘 Enter 不具有库存语义，也不存在键盘焦点或键盘放置模式；Enter 仅在非 Raid 屏幕作为顶层确认。
- 鼠标状态保存源容器、稳定 ID、抓取偏移与值坐标，不保存可能因容器 mutation 失效的业务引用或迭代器。
- 多格拖拽的候选左上角由实际 placement origin 与 grab offset 计算，不能把任意被点击覆盖格当作 origin。
- 拖拽旋转必须同时变换离散格锚点和连续像素锚点；四次顺时针旋转恢复原方向、footprint 与抓取关系。
- 网格外没有合法 hover/preview；只有显式丢弃区可产生玩家物品丢弃请求，其他空白或窗口外释放均取消。
- cell size 必须在除法前验证为正；屏幕、面板局部和 GridPosition 坐标必须显式区分。
- 单帧输入优先级固定为 Tab 关闭、Esc 取消、UI 事件队列；Tab/Esc 触发时必须丢弃本帧待处理的 mouse-up、快捷转移和旋转，不能先提交移动、转移或丢弃再取消。
- Idle 状态下对可堆叠物品按 Ctrl+左键立即拿起 1 个，按 Shift+左键立即拿起 `(quantity + 1) / 2` 个并进入数量拖拽；该行为在 PlayerOnly 与双容器界面都可用，预览阶段源栈不变。
- 数量拖拽释放到空格时创建新 placement，释放到同定义未满栈时精确合并，释放到玩家丢弃区时只把所选数量放到角色脚下；请求不能完整提交时，源/目标/地面与 ID 序列均不变。
- Ctrl+Shift+左键必须被消费为无操作，不能开始拖拽；F 与 Ctrl+右键继续表示双容器间的整栈 first-fit 快速转移。

## 构建与证据

- 新源码未进入 CMake target 就不属于已构建行为；绿色旧测试不能证明它正确。
- 新行为至少覆盖成功、边界、无效输入和失败状态不变；目标测试后仍需完整 CTest。
- 未执行的 CI、人工操作和视觉验收不能标为通过。
