# Project Raidline 不变量

这些规则是实现、测试和审查的行为底线。修改规则需要明确产品决策、测试变化，并在必要时更新 ExecPlan；不能通过实现细节或 include 顺序绕开。

## 生命、命中与容器遍历

- Health 的最大值大于 0，当前值始终在 `[0, max]`；非正伤害不生效。
- `takeDamage` 只在 alive→dead 的那次调用报告死亡；已经死亡的对象不重复结算。
- AABB 必须有正面积重叠；仅边缘接触不算命中。
- 一发 Projectile 一次最多命中一个 Enemy；死亡 Enemy 不再被命中或重复计分。
- 遍历期间收集结果，完成遍历后再删除容器元素，避免迭代器/引用失效。

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

## 背包交互

- 预览和按 R 旋转候选都不修改 GridInventory；同容器旋转提交只能通过 `tryTransform`，跨容器旋转提交只能通过 transform 转移服务。
- 背包是纯鼠标交互：方向键、主 Enter 和数字键盘 Enter 不具有库存语义，也不存在键盘焦点或键盘放置模式。
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
