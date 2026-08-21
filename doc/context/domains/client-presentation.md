# 领域上下文：SDL 客户端与表现

## 权威与入口

- SDL 只属于 `raidline_sdl_client`。App/Input/Renderer 把设备事件翻译为意图，并渲染只读 projection。
- 主要入口：`src/app.h`、`src/input_system.h`、`src/profile_inventory_interaction.h`、`src/game_audio.h`。
- GameFlow/GameSession/Simulation 是行为来源；客户端不拥有长期资产或结算状态。

## 稳定执行路径

`SDL event -> InputSystem -> device-independent input/command -> GameSession/Simulation -> projection/domain fact -> App render/audio`。

## 核心不变量

- UI 预览不授予提交资格；revision 过期或失败时刷新，不做局部提交。
- 模态 UI、失焦和终局必须正确释放输入捕获，不能向世界泄漏同帧操作。
- 命中、伤害、库存合法性、结算和 Sound Event 触发语义来自领域事实。
- 音频/贴图缺失应可控降级，不能驱动领域状态。

## 主要消费者与测试

- 消费者：最终可执行文件与用户真实窗口验收。
- CTest：`ctest -L area-client`、`ctest -L area-inventory-ui`、`ctest -L area-audio`。
- 关键测试：`test_input_system.cpp`、`test_profile_inventory_interaction.cpp`、`test_game_audio.cpp`。

## 跨域风险与禁止事项

App 目前是高频热点，修改前必须通过符号搜索定位真实领域入口。涉及命令/所有权时加载对应领域；涉及暂停/退出/保存时加载 `session-persistence`。不得在 app.cpp 中复制领域公式或按颜色、贴图、名称推断事实。
