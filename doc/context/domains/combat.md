# 领域上下文：战斗

## 权威与入口

- simulation 拥有单局战斗瞬态；长期 HP、装备、弹药、伤势和耐久仍由 Profile 领域提交。
- 主要入口：`src/gameplay_world.h`、`src/weapon_aim.h`、`src/weapon_fire.h`、`src/shot_resolution.h`、`src/logical_ballistics.h`、`src/hit_resolution.h`。

## 稳定执行路径

`WeaponAim/WeaponFire/WeaponAmmo -> ShotCommand -> ShotResolution -> LogicalBallisticFlight -> HitResult -> GameSession 领域提交 -> App 投影`。

生产路径没有可渲染、可碰撞 Projectile 实体；曳光和音效只消费逻辑结果。

## 核心不变量

- 击发冻结方向、散布结果、距离和意图；之后鼠标/角色移动不能修改本发。
- 连续线段扫掠只选择最近合法目标/障碍；一次飞行最多一个权威命中。
- 命中部位、爆头、弱点、防具和伤害必须来自领域结果；普通命中不由 App 升格。
- 输入和帧分割不得改变确定性规则；PCG32 状态与命名随机流保持稳定。
- 音画表现缺失或延迟不能改变弹药、碰撞、伤害或结算。

## 主要消费者与测试

- 消费者：`GameSession`、Profile combat/medical、SDL client 投影。
- CTest：`ctest -L area-combat`、`ctest -L layer-integration`、`ctest -L sentinel`。
- 关键测试：`test_weapon_aim.cpp`、`test_weapon_fire.cpp`、`test_logical_ballistics.cpp`、`test_hit_resolution.cpp`、`test_gameplay_world.cpp`。

## 跨域风险与禁止事项

改弹药消耗/耐久/伤势时加载 `asset-inventory`；改保存或结算时加载 `session-persistence`；改表现只读投影时加载 `client-presentation`。不得重新引入 Projectile 权威、App 命中旁路或按武器显示名分支。
