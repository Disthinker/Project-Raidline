# Project Raidline Week 5 开发日志

## 一、本周主题

最小命中闭环：

Enemy 静态逻辑对象
→ AABB 碰撞检测
→ Projectile 命中 Enemy
→ Projectile 和 Enemy 被清理
→ 自动化测试保护碰撞规则

## 二、本周最终完成的功能

### 1. Rect 逻辑矩形

说明：
- position 表示左上角
- size 表示逻辑宽高
- 不依赖 SDL

### 2. AABB 碰撞检测

说明：
- 两个矩形必须 X/Y 两轴都有面积重叠
- 边缘相接不算碰撞
- `isCollision()` 只返回 bool，不负责删除对象

### 3. Enemy 最小逻辑对象

说明：
- Enemy 保存 position 和 size
- 提供 `bounds()`
- 不包含 AI、生命值、移动、贴图、攻击

### 4. Projectile bounds

说明：
- Projectile 新增 `bounds()`
- 由 position、width、height 生成 Rect

### 5. 命中处理 helper

说明：
- `resolveProjectileEnemyHits(projectiles, enemies)`
- 检测 Projectile 与 Enemy 的 AABB 命中
- 命中后双方被移除
- 一枚 Projectile 一次最多命中一个 Enemy
- 一个 Enemy 同一帧不会被重复处理
- 检测阶段与清理阶段分离

### 6. App 接入

说明：
- App 持有 `std::vector<Enemy>`
- 初始化静态 Enemy
- 渲染 Enemy 占位矩形
- 在 update 中接入命中处理
- 越界 Projectile 清理仍然保留

## 三、新增或修改的核心文件

列出：
- `src/rect.h`
- `src/collision.h`
- `src/collision.cpp`
- `src/enemy.h`
- `src/enemy.cpp`
- `src/projectile.h`
- `src/projectile.cpp`
- `src/hit_resolution.h`
- `src/hit_resolution.cpp`
- `tests/test_collision.cpp`
- `tests/test_enemy.cpp`
- `tests/test_hit_resolution.cpp`
- `tests/test_projectile.cpp`
- `src/app.h`
- `src/app.cpp`
- `CMakeLists.txt`

## 四、碰撞规则和边界语义

写清楚：
- position 是左上角
- left/right/top/bottom 如何计算
- X/Y 两轴都重叠才碰撞
- 边缘相接不算碰撞
- 逻辑碰撞尺寸不等于 sprite 显示尺寸

## 五、Projectile 与 Enemy 命中规则

写清楚：
- 命中后 Projectile 消失
- 命中后 Enemy 消失
- 不命中双方保留
- 一枚 Projectile 一次最多处理一个 Enemy
- 一个 Enemy 同一帧不重复处理

## 六、vector 删除策略

写清楚：
- 检测阶段不 erase
- 用 hit 标记记录待删除对象
- 检测结束后统一清理
- 避免遍历中 erase 导致引用、指针、迭代器或索引失效

## 七、本周测试覆盖

列出：
- CollisionTest 覆盖哪些 AABB 场景
- EnemyTest 覆盖哪些 Enemy 逻辑状态
- ProjectileTest 新增 bounds 测试
- HitResolutionTest 覆盖哪些命中处理规则
- 旧测试 Health/InputSystem/Player/Projectile 仍通过

## 八、本地验证结果

填写你实际结果：

- CMake build：通过 / 未通过
- CTest：通过 / 未通过
- 人工运行：通过 / 未通过

人工运行内容：
- Enemy 可见
- 玩家可移动
- Space 发射 Projectile
- Projectile 命中 Enemy 后双方消失
- 未命中 Projectile 继续移动并越界清理
- 背景、玩家、Enemy、Projectile、调试文字正常显示

## 九、遇到的问题与修复

建议记录：
- `static bool isCollision` 导致声明/定义链接问题
- `EXPECT_EQ(Vec2, Vec2)` 因没有 `operator==` 不适合，改字段比较
- CMake target 漏 `.cpp` 可能导致链接错误
- `renderEnemies()` 写了但忘记在 `render()` 中调用
- `HitResolutionTest` 依赖不完整
- 测试数据误把“不命中”写成了“命中”
- VSCode Testing 面板单项运行与全量 CTest 的区别

## 十、App 职责评估

写清楚：
- App 现在负责输入、玩家更新、Projectile 生成、Enemy 持有、命中处理调用、渲染调度
- 有黄色预警
- 但本周通过 `isCollision()` 与 `resolveProjectileEnemyHits()` 做了最小拆分
- 当前不引入 ECS / World / Scene，因为功能规模还不足以证明大型架构必要

## 十一、本周掌握的知识

列出：
- AABB
- 边缘相接规则
- 逻辑矩形
- 检测与响应分离
- 延迟删除 / 标记删除
- vector erase 风险
- CMake target 依赖
- 单元测试与人工渲染验证区别

## 十二、仍需巩固的知识

建议写：
- `std::erase_if` 与 lambda 捕获
- vector 删除后的引用/迭代器失效
- App 职责膨胀后的重构边界
- 测试数据设计的准确性