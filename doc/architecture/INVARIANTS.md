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
- ItemInstance ID `0` 无效；实例不可复制、可移动，移动后源显式无效。
- 稳定 ID 不等于 vector 下标；容器 mutation 后不得使用旧引用、迭代器或下标识别实例。
- GroundItem 和 GridInventory::PlacedItem 各自独占 ItemInstance；拾取、跨容器转移和丢弃不会复制实例。

## 地面拾取

- GroundItem 位置是世界中心，pickup bounds 由玩法数据计算，不从 PNG 透明边界推断。
- 最近物品距离相等时选择 vector 中较早元素，保持确定性。
- 一次 just-pressed 最多拾取一件。
- 背包无空间时，GroundItem、ItemInstance ID 和 inventory 均完全不变。

## GridInventory

- 网格宽高为正，`cells_.size() == width * height`，使用 row-major 索引。
- 每个 PlacedItem 的完整 footprint 在界内且不与其他实例重叠。
- 每个占用格 ID 对应唯一 PlacedItem；实例 ID 在 inventory 中唯一。
- `findFirstFit` 按 y 后 x 的 row-major 顺序返回第一个合法位置。
- `canPlace`/`canMove` 是无副作用查询。
- `tryPlace` 在完全验证成功后才移动输入 ItemInstance；失败不消费输入、不修改 cells 或 placements。
- `canMove`/`tryMove` 允许与自身旧 footprint 重叠；目标为原 origin 是成功 no-op。
- `tryMove` 失败时所有 cells、origin、实例和 ID 不变；成功时先清旧 footprint、写新 footprint、最后更新 origin。

## 跨容器转移与丢弃

- 跨容器查询无副作用；同一个 `GridInventory` 不能作为转移的源和目标。
- 转移前检查源 ID、目标重复 ID、边界和占用，并在移出源物品前完成目标容量预留。
- 转移成功保持稳定 ID 与 ItemDefinition；失败时两个容器的 placement、cells、顺序和所有权不变。
- first-fit 转移使用目标容器的 row-major 第一个合法位置，结果必须确定。
- 只有玩家背包物品可丢弃；成功后玩家 footprint 清除，世界新增拥有同一实例的 GroundItem。
- 丢弃位置位于玩家逻辑碰撞体的脚底中心，与当前朝向无关，并按物品世界渲染半尺寸限制在世界边界内；失败不改变背包或地面物品。

## 背包交互

- 预览不修改 GridInventory；同容器成功提交只能通过 `tryMove`，跨容器成功提交只能通过转移服务。
- 背包是纯鼠标交互：方向键、主 Enter 和数字键盘 Enter 不具有库存语义，也不存在键盘焦点或键盘放置模式。
- 鼠标状态保存源容器、稳定 ID、抓取偏移与值坐标，不保存可能因容器 mutation 失效的业务引用或迭代器。
- 多格拖拽的候选左上角由实际 placement origin 与 grab offset 计算，不能把任意被点击覆盖格当作 origin。
- 网格外没有合法 hover/preview；只有显式丢弃区可产生玩家物品丢弃请求，其他空白或窗口外释放均取消。
- cell size 必须在除法前验证为正；屏幕、面板局部和 GridPosition 坐标必须显式区分。
- 单帧输入优先级固定为 Tab 关闭、Esc 取消、pointer 事件；Tab/Esc 触发时必须丢弃本帧待处理的 mouse-up，不能先提交移动、转移或丢弃再取消。

## 构建与证据

- 新源码未进入 CMake target 就不属于已构建行为；绿色旧测试不能证明它正确。
- 新行为至少覆盖成功、边界、无效输入和失败状态不变；目标测试后仍需完整 CTest。
- 未执行的 CI、人工操作和视觉验收不能标为通过。
