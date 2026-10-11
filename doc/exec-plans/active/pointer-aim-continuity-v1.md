# 鼠标与准星连续性修复：窗口锚定修正方案 v2

- task_id：`pointer-aim-continuity-v1`（同一未交付修复，不启动其他开发）
- state：用户已验收；执行已授权提交与合并。本计划提交在 `main` 可达时为已交付，交付事实以真实 Git/PR 核对；v1 失败历史保留。
- plan_version：`RL-POINTER-WINDOW-ANCHOR-v2`
- baseline：原工作区 `codex/pointer-aim-continuity-v1` / HEAD `d391278a844f45c41cd624bdd4a17f797fc832f9`，保留完整 v1 未提交候选。已验收 main `772fba0d3068a69bb207566d21a01913eb224451` 仅增加配置；main CI `38138542850` 已实查 success。实施前未切换分支；本轮 v2 在该分支继续完成，HEAD 与 index 保持原样。
- approval_source：2026-10-11 本主控线程真实用户明确“同意 RL-POINTER-WINDOW-ANCHOR-v2，请按方案修复，保留已通过验收的菜单行为，完成后提供复验清单，暂不提交或合并。”；授权本版本实施与验证，不授权 Git 交付。之前2026-10-11 本主控线程真实用户指出验收未通过，确认菜单开关通过、建筑切换不通过，并要求准星与窗口锚定、不能超出窗口；此为验收反馈和修正目标，不当作新方案实施批准。
- candidate_id：`RL-POINTER-WINDOW-ANCHOR-v2-local-cfff4c1d7de2`；完整 15 路径源码 SHA256 清单见支持工作区 `tmp/pointer-window-anchor-v2-20261011/candidate-v2.json`。清单摘要 `cfff4c1d7de2f1ad93fabd0e7a4913a04f39667c1907db481df7aa98c256c2eb`；Debug 程序 SHA256 `581cf63191aff1c4618fa9c7b01f7b96486f01758ba5d52c98ebdb71bdf6c20c`。v1 原字节与实施前快照分别见 `before.json`、`implementation-before.json`。
- accepted_candidate_id：`RL-POINTER-WINDOW-ANCHOR-v2-local-cfff4c1d7de2` / manifest `cfff4c1d7de2f1ad93fabd0e7a4913a04f39667c1907db481df7aa98c256c2eb`。
- acceptance_source：2026-10-11 本主控线程真实用户明确“窗口锚定 v2 验收通过，请完成提交与合并，启动下一项开发。”；授权当前未变候选提交、推送/PR 和普通合并，沿用既有 origin 策略。下一任务仅启动策划与真实代码调查，具体版本方案仍需用户批准。
- delivery_baseline：已验收 main `772fba0d3068a69bb207566d21a01913eb224451` / 配置 PR #168，main CI `38138542850` success；与候选原 HEAD 的差异仅配置/文档。隔离交付分支 `codex/pointer-window-anchor-v2`，复用干净交付 checkout；不将原工作区其他脏文件纳入。
- pending_request：无；此为用户直接反馈的 Bug，不另派策划选题或美术生产。

## 本地完成证据与冻结候选

2026-10-11 按真实用户批准实施。切换与移动后使用同一纯相机投影，在瞄准/击发前将实际准星、控制目标及输入基准转换至新相机。生产路径均提供瞬时视口；无视口的兼容调用仍保留旧行为。旧空间样本完整转换后立即消费，不丢弃转移后的小幅输入。准星中心严格限制在窗口像素范围（1280×720 时为 0..1279、0..719），包括小地图旁的窗口空白；弹道终点与角色活动仍遵守地图边界。

`raid_camera` 唯一编译归属移至 simulation，配置检查确认 simulation 不依赖 SDL。基地跟随镜头同步传播了同一窗口合同；建造镜头另用原路径。菜单捕获函数与相邻尾部逐字节保持原 v1，原独立世界输入基准保留；无需修改菜单交互。`src/app.h` 仅包含原 v1 变更，本轮未再修改。后坐力逐步限位且无积累越界位移、无自动回正，参数未变。

- 修正前新屏幕坐标/四角回归 **0/3**，如预期检出建筑跳点和 48 像素越界；`build/windows-debug/pointer-v2-baseline.log`。该结果是基线缺陷证据。
- Windows Debug 重新配置及全目标构建成功（148 步）；补充基地镜头回归后仅重建对应测试。日志 `pointer-v2-configure.log`、`pointer-v2-build.log`、`pointer-v2-build-final.log`，均位于 `build/windows-debug/`。
- 定向 **57/57**（20.44 秒）及补充基地镜头用例 **1/1**（0.12 秒），合计 **58**，分别见 `pointer-v2-focused.log`、`pointer-v2-base-focused.log`。v2 净新增 **9** 项，另改正 3 项原有相对位置断言；覆盖相机四边/中心、连续进出、下一帧移动、首个鼠标位移、同帧射击方向、小空间、窗口角落反向输入和后坐力，以及已通过的菜单/焦点/关闭点击行为。
- 最终源码及构建完整 CTest **1898/1898**（127.25 秒），`build/windows-debug/pointer-v2-full.log`。本片 whitespace 检查通过；候选 15 路径及可执行文件 SHA256 与冻结清单一致。
- 分支仍为 `codex/pointer-aim-continuity-v1`，HEAD 仍为 `d391278a844f45c41cd624bdd4a17f797fc832f9`，index 未变；其他脏文件、v1 计划及 CURRENT_STATE 历史正文逐字节保留。测试使用隐藏 dummy SDL 窗口，未运行 App.run、未启动游戏、未操作用户存档。没有本片 commit/push/PR/merge 或远端 CI，也未开始其他任务。

复验程序为 `E:\WorkPlace\Projects\C\Project Raidline\build\windows-debug\Project_Raidline.exe`；对应上述候选，不以旧构建或主线配置 CI 代替。上述冻结候选现已通过用户正常游玩验收；没有再改候选代码。旧未提交/无远端 CI 条目是本地完成时证据，当前交付以 Git/PR 状态为准。

## 现象、原因与证据

用户实际游玩复现：开关菜单正常；进出建筑时，玩家在窗口中的位置变化，准星随之突然跳动。预期在无鼠标位移时，切换前后准星保持同一窗口坐标，且准星中心不能越出窗口。审批前诊断只读确认调用与坐标关系，未启动游戏；以下四项描述修正前实现，不是再次实机复现。批准后的自动化结果见上方完成证据。

1. `GameplayWorld::tryTransitionRaidSpace` 将旧/新玩家中心交给 `WorldShootingRuntime::relocate`，后者调用 `WeaponAimState::relocate`，实际准星、控制目标和输入基准均加上玩家中心差。屏幕投影为 `world - camera`，所以切换后屏幕差等于“玩家位移减相机位移”；玩家不在同一屏幕位置时，这个差不为零。
2. `WorldShootingRuntime::updateAim` 的新空间首样本只 `rebaseInput`，不会纠正上一步错误的准星平移。
3. `App::makeGameplayInput` / `makeBaseGameplayInput` 使用 `kReticleViewportOutsideMargin = 48`，允许准星中心越出窗口；仅在释放 SDL 鼠标时限位，不能保证战斗准星始终在窗口内。
4. `tests/test_pointer_continuity_ui.cpp` 的建筑回归断言是“新玩家中心 + 旧相对偏移”，模拟测试也名为 `EnterExitAndFirstFrameKeepPlayerRelativeAim`。它们证明旧合同，未验证窗口坐标连续性。此前 1889/1889 不能证明本次新目标。

尚未验证：v2 在真实 SDL 鼠标设备上的手感与所有镜头边缘组合。新候选的自动化结果不能替代用户正常游玩验收。

## 最小修正及范围

采用局部跨 App/输入/瞄准运行时的修正，因为窗口投影由客户端提供，而真实射击方向必须消费 simulation 的实际准星；只移动绘制坐标会使显示与击发方向分离。

- 以实际准星的窗口坐标为切换锚点，不以玩家相对方向/距离或 SDL 相对模式的固定硬件锚点代替。切换前 `screen = actualAimWorld - oldCamera`；切换后 `actualAimWorld = screen + newCamera`。没有设备位移且不触及窗口边界时，窗口坐标严格不变。
- 在现有 `GameplayInput` 中补足设备无关的旧视口原点/尺寸；simulation 在空间转移时用同一纯相机数学取得新视口，并在本帧瞄准/射击前同步实际准星、控制目标及输入基准。原窗口锚点即使因相机变化对应另一世界点，也不能被下一帧的 rebase 或旧事件再加一次位移。
- 复用 `raid_camera` 纯坐标数学，必要时将其唯一编译归属从 SDL client 移至 simulation；不让 simulation 引用 SDL，不复制一套相机规则、不添加框架。
- 准星中心以窗口可视范围为严格边界，去掉生产路径的 48 像素越界。反向移动应立即恢复，不能先消耗被截掉的虚拟位移。地图小于窗口时也以窗口锚点为准：瞄准点可位于窗口内的地图外空白，玩家活动与弹道终点仍沿用地图边界，不扩大地图或射程。
- 保留菜单释放/重新捕获、子页往返、失焦恢复及关闭点击抑制已通过的行为。后坐力仍在既有规则下推动实际准星，只在窗口边缘执行限位，不引入自动回正。

## 核查路径与实际传播

| 路径 | 操作 | 原因 |
| --- | --- | --- |
| `src/app.cpp`、`src/gameplay_input.h` | 修改 | 明确窗口/视口数据与严格边界，保留菜单捕获入口 |
| `src/gameplay_world.cpp` | 修改 | 建筑转移消费视口锚点并于当前帧完成转换 |
| `src/world_shooting_runtime.cpp`、`src/world_shooting_runtime.h` | 修改 | 改正转移与首样本衔接，维护单一瞄准状态 |
| `src/weapon_aim.cpp`、`src/weapon_aim.h` | 修改 | 按视口差平移、统一窗口限位，分离地图弹道边界与准星边界 |
| `src/raid_camera.cpp`、`src/raid_camera.h`、`CMakeLists.txt` | 修改 | 复用纯相机投影与窗口边界，保持唯一编译归属及 SDL-free 检查 |
| `tests/test_pointer_continuity_ui.cpp`、`tests/test_raid_camera.cpp`、`tests/test_weapon_aim.cpp` | 修改 | 将建筑断言改为窗口坐标，补齐相机边缘、小空间和越界反向输入回归 |
| `src/base_world.cpp` | 修改 | 同一窗口合同的必要共享消费者传播，在移动后的当前相机中处理实际瞄准，避免基地移动时边缘越界；建造相机维持既有路径 |
| 本活动计划及 `doc/project/CURRENT_STATE.md` | 修改 | 记录真实批准、v2 候选、自动化证据与待人工验收状态 |

本轮未新增生产源码、资源或存档字段；`tests/test_pointer_continuity_ui.cpp` 为原 v1 未提交测试文件，本轮在其上扩展；不改门口/出生点、地图内容、伤害、后坐力/散布参数、资源与正式资产 Manifest。保持 schema/content/rules 51/64/29。其余脏文件保持原字节。

## 验证、人工复验与回滚

已执行 Windows Debug 全目标重建（输入/类接口消费者均重建），定向 App/SDL、相机、瞄准、建筑转移与射击方向回归及完整注册 CTest、本片 whitespace 检查。自动化须断言实际准星投影和真实击发方向一致，不能只验证图形被画回原点。没有新代码时不把旧构建/1889 项结果记为 v2 通过。

用户复验：

1. 把准星放在窗口固定标记处，在玩家不居中的建筑入口进出；无鼠标位移时准星仍在同一窗口点。覆盖相机滚动区域与地图四侧限位附近的入口。
2. 连续进出并继续移动，确认转移帧和下一帧不跳点，随后鼠标小幅移动立即生效。
3. 将准星推向窗口四边/四角，含连续射击后坐力；中心不能越出窗口，反向移动无死区。小于窗口的模拟室内也验证同一窗口锚点。
4. 重验背包、容器、暂停/设置、测试场、子页往返与窗口焦点；沿用已通过的菜单接续行为，不误开枪。

主要风险是相机位移被应用两次、转移帧使用旧视口边界、边缘截断后的输入积累，以及仅修绘制未同步射击。以上各有直接回归。实施前保存 v1 原字节与文档；失败只还原本轮准星修正，保留 v1 菜单修改和其他未提交成果，不用 git reset/clean。当前用户已明确验收 v2 并授权交付；只提交上述候选与本任务记录，等待准确提交的双平台 CI，通过后普通合并，不绕过门禁。

---

以下完整保留 v1 原方案与自动化历史。其“相对玩家方向/距离”要求及旧验收第 3 项已被 v2 窗口锚定目标替代；v1 完整候选已验收失败，旧通过记录不作为 v2 证据。

# 鼠标与准星位置连续性修复 v1

## 来源、结果与范围

2026-10-11 用户验收场景容器并授权合并后继续开发，明确追加建筑/菜单鼠标位置重置修复。策划线程将下一项限定为本修复。玩家打开背包、容器、暂停、设施或测试菜单时，鼠标从当前准星屏幕位置接续；子页与返回不重置鼠标；关闭菜单恢复世界瞄准，界面鼠标移动不作为战斗输入。进入/退出建筑保持相对玩家的瞄准方向和距离，允许合法边界限位，并避免切换首帧消费旧空间位移。

基线：已验收 [PR #167](https://github.com/Disthinker/Project-Raidline/pull/167)，main `d391278a844f45c41cd624bdd4a17f797fc832f9`，树与验收 head `393663b` 一致。修复分支 `codex/pointer-aim-continuity-v1`。原有维护及历史改动保留。

不改变角色出生点、射击/后坐力/散布参数、敌人、地图、容器规则、资源或存档字段；不启动游戏，不修改信任/权限。本片待用户复验，不自动合并。

## 调用链与所有权

App 保留独立于界面鼠标的世界输入屏幕基准。共用捕获切换在释放前将 SDL 指针定位至当前准星（窗口内限位），重新捕获清除暂存位移及已消费点击。SDL 的释放顺序依据 [SDL_SetWindowRelativeMouseMode](https://wiki.libsdl.org/SDL3/SDL_SetWindowRelativeMouseMode)。

GameplayWorld 门户转移只平移瞄准状态到新玩家原点；WorldShootingRuntime 忽略转移帧的旧空间输入，并在新空间第一样本重接输入基准。WeaponAimState 保有唯一瞄准状态与既有速度/后坐力规则，不保存 SDL/菜单坐标。

## 完成条件与回滚

- [x] 共用鼠标捕获与菜单输入连续性修复
- [x] 进出建筑及新空间首帧连续性修复
- [x] 实际 SDL/App 事件路径及瞄准边界回归（定向 35/35，新增 10 项）
- [x] Windows Debug 全目标构建、完整 CTest 与本片 whitespace 检查
- [ ] 用户正常游玩复验

本片尚未提交时只撤销本片路径差异回到上述 main，保留原维护文件；后续独立提交可单独回退。本片不改 schema/content/rules（51/64/29）。

## 正常游玩验收

1. 准星移至屏幕边侧，依次打开背包、场景容器、暂停/设置及测试场面板：鼠标接续准星，子页返回保持鼠标位置。
2. 在界面中移鼠标整理物品，再关闭：恢复此前瞄准，首帧不跳动、不误开枪，随后鼠标位移立即生效。
3. 斜向瞄准后连续进入/退出建筑：保持相对玩家的方向/距离，窗口及地图边界仅做必要限位；继续移动时镜头跟随不造成额外跳动。
4. 切出并返回窗口，重复菜单操作；确认瞄准与鼠标可用。

## 实际证据

修复前在独立临时程序中重现 Base 背包、暂停与 Raid 背包的鼠标/瞄准跳动，以及实际 GameplayWorld 门户转移及下一帧的相对瞄准变化。测试使用隐藏 dummy SDL 驱动，无游戏窗口、无 App.run；日志 `build/pointer-baseline-test.log`、`build/pointer-portal-baseline.log`。

Windows Debug 全目标重建成功；扩展测试后再增量构建验证新夹具，日志 `build/windows-debug/pointer-build.log`、`build/windows-debug/pointer-build-expanded.log`。定向 **35/35**（15.08 秒）通过，日志 `build/windows-debug/pointer-focused.log`；新增 **10** 项，直接验证 SDL 释放后的鼠标坐标、真实菜单及子页往返、窗口焦点、按住关闭点击、测试场 F8 面板、实际 App F 门户/场景容器、进出两空间及首样本/后续位移和小空间限位。

最终完整 CTest **1889/1889**（138.77 秒）通过，日志 `build/windows-debug/pointer-full.log`；本片 whitespace 检查通过。全部 C++ 消费者已重建；schema/content/rules 保持 **51/64/29**。本片尚未提交/推送/新建 PR，无本片远端 CI；未启动游戏，待用户按上述清单正常游玩复验。既有维护/历史文件逐字节校验保留，CURRENT_STATE 原历史正文保留。这些自动化不替代实机鼠标/手感验收。
