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

## Week 17 预期学习主题（尚未完成）

- 屏幕、面板局部与网格三种坐标系。
- 点击、阈值拖拽、释放与取消的显式状态机。
- 多格物品真实 origin 与 grab offset。
- keyboard focus 与 mouse hover 两条输入状态的兼容。
- `canMove` 查询与 `tryMove` 提交之间的预览/事务边界。
- 引用成员 `GridInventory&` 的生命周期约束与替代架构选择。

只有在 Week 17 实际实现、测试和复盘后，才把这些主题移动到“已使用”。
