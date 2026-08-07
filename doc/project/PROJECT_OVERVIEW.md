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
- 世界物品：Cola、Medkit、Pistol、Rifle 与可堆叠 9mm 弹药；共享定义和 move-only 稳定 ID 实例分离，地面物品由 F 键拾取并可原子合并。
- 背包：玩家 10×6 与柜体 6×6、确定性 first-fit、footprint、四向物品方向、数量堆叠、稳定 ID 和事务式放置/移动/跨容器转移。
- 背包 UI：Tab/柜体上下文视图；纯鼠标阈值拖拽；平滑虚影与吸附 footprint 分离；F/Ctrl+右键整栈快捷转移；R 拖拽旋转；Ctrl/Shift+左键按数量拿取；Esc/Tab 取消优先；方向键和 Enter 无背包语义。
- 物品丢弃：玩家整栈或选中数量可拖到明确丢弃区并在角色脚下生成地面物品；第二容器物品不能直接丢弃。
- 搜索与 Loot：世界柜体首次近距离 F 交互按加权 LootTable 生成一次战利品；关闭、转移、取空和重开均保留当前世界生命周期内的真实搜索状态。
- Raid 与撤离：自动开始 180 秒 Raid；玩家中心进入左下撤离区并连续停留 3 秒可撤离，离开会清零进度；撤离、死亡命令或超时形成不可逆终局并冻结世界玩法更新。

## 当前产品边界

尚未实现：Raid 结算、Stash、玩家 Health/战斗死亡接线、局内重开、多柜体/搜索计时、外部数据驱动 Loot、装备栏、上下文菜单、重量/耐久、保存读取、音频、复杂 AI、程序地图、尸体搜索和完整武器装填/弹匣系统。当前 Raid 与搜索状态只在单个 GameplayWorld 生命周期内存在，9mm 也只是数量物品，不代表完整武器弹药系统已经完成。

这些内容只能作为候选路线，不能在当前任务中被当作现有能力或默认承诺。详见 [ROADMAP.md](ROADMAP.md)。

## 工程原则

- 以小步、可运行、可测试的闭环推进。
- 先建立核心模型和不变量，再接 SDL 输入与渲染。
- 区分共享定义与唯一实例，明确每次所有权转移。
- 区分纯查询与事务式提交，失败不改变状态。
- 使用稳定 ID，避免把容器下标、引用或迭代器当作跨帧身份。
- 不为未排期系统提前引入 ECS、SceneManager 或通用服务层。

架构职责见 [ARCHITECTURE.md](../architecture/ARCHITECTURE.md)，行为底线见 [INVARIANTS.md](../architecture/INVARIANTS.md)。当前缺陷、待决产品行为和延期工程债见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)。
