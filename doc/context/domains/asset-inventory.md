# 领域上下文：资产与库存

## 权威与入口

- `ProfileState` 的 `AssetRegistry` 唯一拥有所有长期物品实例；位置由 `AssetLocation` 表达。
- 主要入口：`src/profile_state.h`、`src/inventory_domain.h`、`src/weapon_ammo_domain.h`、`src/grid_inventory.h`。
- 定义来自不可变 `ContentRegistry`；UI 格位、显示名和贴图不是身份或合法性来源。

## 稳定执行路径

`query -> 带 revision 的计划 -> command -> 候选 Profile 验证 -> 原子提交 -> receipt/projection`。

格子移动、交换、堆叠、装备、压卸弹、装匣与枪膛都必须走领域查询和命令；客户端拖放只生成意图。

## 核心不变量

- 每个 `AssetInstanceId` 恰有一个权威位置，ID 单调且不复用。
- 拒绝操作保持 Profile 指纹、revision、货币、高水位和弹药总量不变。
- 容器内容跟随容器；非空容器禁止嵌套；分区规则不能由 UI 猜测。
- 散装、弹匣、枪膛、动作暂持、消耗之间弹药守恒。
- 不得跨 vector 结构修改保留引用、指针、迭代器或下标。

## 主要消费者与测试

- 消费者：`GameSession`、Raid lifecycle/settlement、Profile inventory interaction、SaveRepository。
- CTest：`ctest -L area-inventory`、`ctest -L area-assets`、`ctest -L sentinel`。
- 关键测试文件：`test_profile_state.cpp`、`test_inventory_domain.cpp`、`test_weapon_ammo_domain.cpp`、`test_grid_inventory.cpp`。

## 跨域风险与禁止事项

修改 AssetRecord、Location、装备槽或稳定 ID 时，必须额外加载 `session-persistence` 与 `content`。不得复制资产到场景/V0 模型，不得让 App 直接改 Registry，不得用显示名称分支。
