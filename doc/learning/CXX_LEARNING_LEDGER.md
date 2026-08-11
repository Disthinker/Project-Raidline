# Project Raidline C++ 学习台账

本台账记录仓库中已经真实使用过的知识和仍需强化的学习债。详细教学保留在 DevLog 与每次 `doc/handoffs/completed/` 报告；这里不复制教程。

## 已在项目中使用

| 阶段 | 真实使用的 C++/工程主题 |
| --- | --- |
| Week 1–2 | target-based CMake、vcpkg/GTest/CTest、`enum class`、`std::optional`、`std::unordered_set`、SDL 事件与 held/just-pressed |
| Week 3–5 | 值类型 Vec2/Rect、deltaTime、归一化、`std::vector`、`emplace_back`、erase-remove、AABB、延迟删除 |
| Week 6–9 | 所有权边界、const getter、结果对象、lambda、短生命周期对象、跨帧状态与 cooldown |
| Week 10–12 | RAII、`std::unique_ptr` custom deleter、move-only、对象组合、`size_t`、`while` 跨帧、`std::mt19937`、distribution、`std::erase_if` |
| Week 13–14 | 类不变量、`[[nodiscard]]`、组合、状态转换、`std::array`、`std::string_view`、`std::uint64_t` 稳定 ID、所有权转移 |
| Week 15–16 | 扁平网格、有符号边界、`optional<move-only>`、事务式放置/移动、self-overlap、查询/提交分离、enum 状态机、vector 失效规避 |
| Week 17 | float 屏幕坐标到格子坐标、左上含/右下不含边界、`Idle/Pressed/Dragging` 指针状态机、像素拖动阈值、多格 grab offset、keyboard focus 与 mouse hover 分离、值类型 pointer event/move request、帧级输入优先级、预览/提交事务边界 |
| 背包 UX 稳定化（#27） | 用两个独立 `std::optional` 表达连续像素位移与吸附格候选、由原 placement 加 press→current delta 保留抓取点、有限浮点输入防护、纯渲染预览与模型事务继续分离 |
| Week 18 | 两个 move-only 所有者间的事务式转移、目标预留后提交、失败回滚、`std::variant` 请求、容器 ID 值状态、显式丢弃所有权转移、`vector::max_size` 溢出防护、`StorageCabinet` 组合所有权、Closed/PlayerOnly/Container 视图状态、有限浮点交互边界 |
| Week 19 | 四向 orientation 与 footprint transform、连续/离散抓取锚点同步、精确数量拆分/合并计划、世界级稳定 ID 分配、`std::optional<std::uint32_t>` 拖拽意图、同/跨容器指定格数量事务、部分 GroundItem 丢弃、预留后无抛出提交、MSVC `/showIncludes` 与 Ninja 依赖追踪 |
| Week 20 | 纯虚随机源接口与运行时多态、`final`、`std::mt19937` 注入边界、加权半开区间选择、结果堆叠规范化、显式 Unsearched/Searched 状态、临时 move-only GridInventory 原子提交、默认 move 特殊成员与 `noexcept`、首次搜索稳定 ID 事务 |
| Week 21 | 六态有限状态机、sticky 终局、连续占用取消语义、同一 deltaTime 内竞争终止事件、有限浮点配置/派生边界验证、半开矩形点包含、领域状态与 SDL 只读渲染分离、终局帧 mutation 截断 |
| Week 22 | 组合式 Stash 所有权、整容器预规划事务、占用位图模拟、批量 reserve 后无分配提交、完整堆叠身份保留、Blocked 可重试状态、显式批量销毁、Raid 终局到结算终局的幂等映射 |
| Week 23 | `std::unique_ptr<GameplayWorld>` 单局所有权、候选对象先构造后 `swap` 的强失败语义、外部 Stash 注入、跨局稳定 ID 高水位、InRaid/SettlementBlocked/BetweenRaids 会话状态、just-pressed 重开边沿、只读局外视图 |
| Week 24 | Player 组合拥有 Health、受控伤害命令统一生命与 Raid 终局、碰撞 cooldown 跨帧状态、致死帧早返回、成功/失败垂直切片集成测试、类布局依赖复验 |
| Week 25 | SDL 无关顶层四态状态机、组合根再封装、非拥有引用成员与构造/析构顺序、删除复制/移动保护别名、屏幕输入边沿消费、状态路由与非活动子系统冻结 |
| Week 26 | SDL 无关武器时间状态、值类型 `ShotSpec`、`std::optional<Vec2>` 瞄准快照、确定性整数伪随机偏移、向量旋转/归一化、held 与 edge 合并、UI pointer 抑制到物理释放、代码绘制准星反馈 |
| Week 27 | 组合式确定性 AI 与攻击状态机、`std::array` 配置表、条件武装/消费、Grab 前摇追踪与 Active 锁向、Grab→Bite 原子状态转换、`OffBalance` 分支、三级速度 enum、跨结束点的分段时间倍率、控制/受击计时唯一所有权、有界子步与逻辑快照驱动代码预警 |
| Week 28 | 感知状态机与滞回、冻结最后已知位置、有限角速度、值类型 `EnemyAiInput/EnemyTacticalDirective`、无所有权协调器、不可变 vector 快照后统一提交、稳定下标平局、有界邻域分离、有限浮点派生值溢出防护 |
| RL-INV-003 | 普通整堆放置的查询/命令统一、同容器 erase 后按稳定 ID 重查、部分填满与精确数量两种不同事务合同、目标 ID 保留与源余量原地保留 |
| 工程接管 | Agent TOML、仓库级 Skill、ExecPlan、证据式 DoD、构建/CI 环境与代码故障分层 |

## 持续学习债

- `std::move` 只是转换，不保证移动发生；需结合构造/赋值调用点解释。
- `std::vector` 扩容、erase 与重排造成的引用/迭代器/下标失效。
- move-only 对象放入 `std::optional`、`std::vector` 和跨所有者事务时的状态。
- 强异常保证与“先验证后无抛出提交”的真实边界。
- `const` 接口、值/引用/指针返回及可变状态暴露。
- 浮点边界与跨标准库随机 distribution 测试的可移植性。
- CMake target 源码复用、compile/link/test discovery 的不同失败层。
- App 初始化/销毁顺序与 SDL 资源依赖。
- 两容器事务中“查询成功”到“最终提交”之间的前置条件，以及为什么预留容量必须早于源物品移出。
- 数量拆分时“源 ID、目标 ID、新 ID”三种身份规则，以及为什么失败不能推进世界 ID 序列。
- 同一 `GridInventory` 内拆分/合并与两个不同 `GridInventory` 间转移在引用失效和提交顺序上的差异。
- 为什么普通整堆拖拽可把目标补满并保留源余量，而 Ctrl/Shift 数量拖拽必须精确提交；以及两者为何不能复用一个被放宽的布尔合同。
- 随机源内部状态无法回滚，但为什么领域状态仍能通过“先完整生成、后一次提交”获得强事务保证。
- 抽取结果值、最终 ItemInstance 和柜体 GridInventory 三层之间的职责与所有权边界。
- 大 deltaTime 同时跨过两个 deadline 时，为什么需要比较“事件还剩多久”而不是固定检查顺序。
- 为什么撤离占用读取玩家逻辑中心而不是渲染 sprite，以及半开边界如何避免相邻区域双重命中。
- 状态机终局 sticky、同帧提前 return 与 App 只读反馈如何共同防止终局后的额外玩法 mutation。
- 为什么整背包转移不能逐件“试试看”，以及如何用目标占用副本先证明所有 placement 都能提交。
- RaidSession 的终局与 RaidSettlement 的完成态为什么是两个职责不同、但都必须 sticky 的状态边界。
- `clear()`、逐件 move 到 Stash 和未来跨 Raid ID 分配分别代表销毁、所有权转移与身份生成，不能混成同一规则。
- 为什么跨局 ID 必须传递“下一未使用值”而不是扫描存活对象最大值，以及已销毁 ID 仍不能复用。
- 为什么重开要先构造完整 `std::unique_ptr<GameplayWorld>` 候选再 `swap`，而不能先销毁旧终局后尝试构造。
- RaidSettlement 为何只保存单局 sticky 结果，而长期 Stash 必须由更外层 GameSession 拥有并显式注入。
- 为什么 Player 应唯一拥有 Health，而 GameplayWorld 只编排伤害与 RaidSession 转换，避免 HP 与终局双事实源。
- 为什么旧的被动接触 cooldown 必须被显式 Active 命中窗口替代，以及致死攻击为何仍必须位于射击/命中之前。
- 为什么顶层流程必须唯一拥有 GameSession，并由状态路由决定是否调用 update，而不能只在渲染层“隐藏”仍运行的世界。
- 引用成员为什么不会延长对象生命周期，以及成员声明顺序、初始化顺序和删除 App 复制/移动如何共同保护 `gameSession_` 非拥有别名。
- 为什么屏幕转换帧需要消费 Enter/点击并立即返回，避免一个输入边沿跨越 MainMenu、Base 与新 Raid。
- 为什么射击时间状态必须由 GameplayWorld 内的领域对象唯一拥有，而 App 只读反馈，避免 cooldown/扩散双事实源。
- 为什么只读取 mouse held 会丢失同帧 down+up 的极短点击，以及 `fireJustPressed || firePressed` 如何同时覆盖单击与连射。
- 为什么 UI 消费左键后必须抑制到物理 release，而不能只清除当前帧 edge；以及该设备级抑制为何不应清除 Space fire。
- 为什么任意方向弹丸要同时改变速度、枪口生成点和方向无关 AABB，而不能只旋转 velocity。
- 为什么 SDL cursor 可见性是 App 必须成对恢复的进程级状态，以及 shutdown 为何仍要执行兜底 `SDL_ShowCursor()`。
- 为什么拖尾渲染应只读取 const velocity 快照，并让视觉弹头尺寸与 8×8 逻辑碰撞体保持解耦。
- 为什么提高 Projectile 速度后需要有限子步进防止离散 AABB 穿透，以及每个子步删除容器元素后只能累计值结果、不能保留实体引用。
- 为什么命中火花可以复用只读 Particle 值并在 App 中改变颜色/线条，而伤害与得分仍只能来自一次 HitResolutionResult。
- 为什么 Enemy 应唯一拥有 AI 与攻击阶段，而 GameplayWorld 只提供目标值并编排碰撞，避免 App/World/Enemy 三份 cooldown 与阶段状态。
- 为什么攻击启动时必须锁定归一化方向，并让 Grab 位移按 Active 实际消费时间累计，才能同时保证可躲避性与帧切分一致性。
- 为什么“查询存在命中机会”后仍要先消费窗口再修改 Player/Raid，避免大帧、多子步或致死早退产生重复伤害。
- 为什么 AI 的攻击冷却只在动作真正启动后提交，而不是在“想攻击”的查询阶段扣除。
- 为什么 Player 应唯一拥有控制剩余时间，重复控制取 `max` 而不是相加，以及到期帧为何保持整帧抑制以避免输入时序歧义。
- 有界敌人子步解决了跨过短 Active 窗口的问题，但 2048 上限对异常超大非终局 deltaTime 仍是明确的精度/工作量权衡。
- 为什么多实体 steering 必须先生成全体不可变值快照和全体指令，再修改任一 Enemy；即使 vector 本轮不扩容，边算边移仍会产生顺序依赖。
- 为什么 `EnemySquadCoordinator` 应返回值类型 directive 而不保存 `Enemy&`、指针或 iterator，以及稳定槽位如何同时服务等距平局与支援侧向。
- 感知获取/丢失为什么需要两个半径形成滞回，Searching 为什么只能读取冻结的最后已知位置而不能持续更新远处玩家坐标。
- 为什么有限输入经过减法或平方后仍可能溢出为 Inf，以及距离函数把派生溢出解释为“不可获取”而不是“零距离”的安全退化。

## Week 17–18 已落地、仍应复习的主题

- 屏幕、面板局部与网格三种坐标系。
- 点击、阈值拖拽、释放与取消的显式状态机。
- 多格物品真实 origin 与 grab offset。
- keyboard focus 与 mouse hover 两条输入状态的兼容。
- `canMove` 查询与 `tryMove` 提交之间的预览/事务边界。
- 为什么 interaction 不保存 `GridInventory&`，而由 App 查询模型并提交值类型请求。
- 为什么 SDL poll 中不能直接提交 mouse-up，而要等 Tab/Esc 完成帧级仲裁后再消费值类型 pointer event。
- 为什么类布局变化后混用新旧增量对象会表现为 MSVC 栈损坏，而不是普通断言失败。
- 为什么跨容器移动不能复用单容器 `tryMove`，以及失败时如何保证两个容器都不变。
- 为什么丢弃区必须产生显式请求，而不能把任意网格外 release 都解释为丢弃。
