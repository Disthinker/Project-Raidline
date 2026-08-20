# Combat：输入捕获、后坐力曲线与临时音频 v1 ExecPlan

状态：本地实现、Windows Debug 全目标与 751/751 CTest 已完成；待提交、Draft PR、exact-head CI 和用户正常游玩验收

基线：`origin/main@881c034`（PR #67 已通过用户验收并合入）

分支：`codex/combat-input-capture-audio-v1`

## 1. 玩家结果

- 默认准星操控进入用户已验证的高响应区间；最大速度不再掩盖控制加速度，F10 可分别调整两者。
- 横向后坐力先产生明确的径向初速，再在短弯曲时间内平滑转向随机角度；高偏转比例形成连续弧线而不产生单帧瞬移。鼠标静止仍不自动回正。
- Active Raid 使用 SDL 相对鼠标模式，光标不能离开窗口，玩家可以持续压枪。打开库存、医疗轮盘、开发面板或失去焦点时立即释放捕获并恢复系统光标。
- 成功击发与 Enemy/Obstacle/Ground 命中播放短促临时程序化音效。音频只消费领域事实，不参与射击、碰撞或伤害。

## 2. 状态与边界

- `WeaponAimState` 继续位于 SDL-free simulation；新增每帧相对位移输入、即时径向后坐力初速和短方向弯曲阶段。随机角度使用现有 PCG32，击发刷新运动而不叠加。
- `maximumReticleSpeed`、`reticleControlAcceleration` 和 `recoilBendDurationSeconds` 都是可测试运行参数；人机工效仍派生控制加速度和后坐力减速度。
- 相对鼠标启停与焦点/UI 仲裁只属于 `raidline_sdl_client`。App 把 `xrel/yrel` 翻译成 `GameplayInput::aimMotionDelta`，simulation 不依赖 SDL 或 OS 光标位置。
- `CombatAudioOutput` 属于 SDL client。当前 cue 由代码生成并在回调中混音，不新增文件资产、不修改资源 manifest，也不宣称正式音效完成。

## 3. 实施与退出条件

1. 调整默认武器人机工效、最大准星速度与控制加速度映射；F10 增加控制加速度参数。退出条件：高速度/高人机工效不再被旧加速度上限拖慢。
2. 将后坐力侧向速度叠加迁移为“即时径向初速 + 有界角度弯曲”；F10 增加弯曲时间。退出条件：总速度有界、连续开火刷新不叠加、极端横向参数形成连续弧线且无单帧跳点。
3. 建立 Raid 相对鼠标捕获和 UI/焦点仲裁。退出条件：Active Raid 持续消费相对位移；所有模态 UI 与失焦释放捕获；切换不造成隐藏准星跳变。
4. 加入程序化枪声与三类落点 cue。退出条件：无音频设备时游戏仍能运行；音频仅消费 `shotFiredLastUpdate/HitResult`。
5. focused、Windows Debug 全目标、全量 CTest、exact-head Windows/Ubuntu CI 后交由用户正常游玩验收。开发代理不自行启动游戏。

## 4. 自动化、风险与回滚

- 自动化覆盖相对位移、速度/加速度上限、后坐力径向初速与连续弯曲、随机角度总速度、刷新不叠加、鼠标捕获决策和 cue 的确定性/有限幅值。
- 风险：过高默认响应削弱武器重量感；通过独立加速度参数调节。后坐力过于平滑会缺少冲击；径向初速即时生效，弯曲时间只控制横向轨迹。相对模式与 UI 冲突通过单一纯决策函数收束。临时音色粗糙是已知边界，正式音频需独立授权与资产流程。
- 整个 PR 可回滚到 `origin/main@881c034`，不改变 schema、Profile、内容 ID 或 manifest。

## 5. 进度

- 2026-08-20：从干净 `origin/main@881c034` 创建分支。
- 2026-08-20：完成默认操控、相对位移、径向初速后连续弯曲的后坐力、相对鼠标捕获、F10 新参数与临时程序化音效实现。
- 2026-08-20：按用户二次调参反馈把默认最大准星速度提高到面板上限 5000、人机工效提高到 100；横向后坐力改为即时径向初速后连续弯向随机角度。
- 2026-08-20：Windows Debug 全目标构建和 751/751 CTest 通过；`Project_Raidline.exe` 已生成但开发代理未启动。提交、Draft PR 与 exact-head CI 待完成。
