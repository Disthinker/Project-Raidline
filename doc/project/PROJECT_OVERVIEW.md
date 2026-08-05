# Project Raidline 项目概况

## 定位

Project Raidline 是一个使用 C++20、SDL3 和像素资源构建的 2D 游戏原型。产品方向由用户定义为“2D 斜俯视角像素风末日搜打撤”，当前代码仍处于垂直切片集成阶段；该方向不等于完整搜打撤循环已经实现。

## 技术基线

- 语言与运行时：C++20、SDL3、SDL3_image。
- 构建与依赖：CMake、Ninja、vcpkg manifest。
- 自动测试：GoogleTest、CTest；艺术管线另有 pytest。
- 持续集成：GitHub Actions 在 Windows 2022 与 Ubuntu 22.04 上构建全部 C++ target 并运行完整 CTest。
- 主程序 target：`Project_Raidline`。

## 当前可运行切片

- 玩家：WASD 移动、朝向保持、斜向归一化、Space 连续射击和冷却。
- 敌人与战斗：水平移动和边界反弹、Health、AABB 命中、伤害、死亡、调试分数。
- 表现：玩家/敌人水平帧动画、命中粒子、背景与物品贴图。
- 世界物品：Cola、Medkit、Pistol、Rifle 的共享定义与唯一运行时实例；地面物品由 F 键拾取。
- 背包：10×6 格子、确定性 first-fit、footprint 占用、稳定实例 ID、事务式放置与移动。
- 背包 UI：Tab 开关；方向键浏览或移动预览；Enter 选择/确认；Esc 取消；合法性使用绿/红代码绘制反馈。

## 当前产品边界

尚未实现：完整 Raid 生命周期、撤离、结算、Stash、地图搜索容器、Loot table、装备栏、多容器转移、物品旋转、快捷转移、上下文菜单、堆叠/重量/耐久、保存读取、音频、复杂 AI、程序地图、尸体搜索和完整武器弹药系统。

这些内容只能作为候选路线，不能在当前任务中被当作现有能力或默认承诺。详见 [ROADMAP.md](ROADMAP.md)。

## 工程原则

- 以小步、可运行、可测试的闭环推进。
- 先建立核心模型和不变量，再接 SDL 输入与渲染。
- 区分共享定义与唯一实例，明确每次所有权转移。
- 区分纯查询与事务式提交，失败不改变状态。
- 使用稳定 ID，避免把容器下标、引用或迭代器当作跨帧身份。
- 不为未排期系统提前引入 ECS、SceneManager 或通用服务层。

架构职责见 [ARCHITECTURE.md](../architecture/ARCHITECTURE.md)，行为底线见 [INVARIANTS.md](../architecture/INVARIANTS.md)。
