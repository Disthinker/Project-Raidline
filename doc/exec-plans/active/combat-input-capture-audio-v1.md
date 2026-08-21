# Combat：输入捕获、后坐力曲线与 P0 音频 v1 ExecPlan

状态：已完成；PR #68 通过 exact-head Windows/Ubuntu CI 与用户正常游玩验收，以 merge commit `ba3375e` 进入 main

基线：`origin/main@881c034`（PR #67 已通过用户验收并合入）

分支：`codex/combat-input-capture-audio-v1`

## 1. 玩家结果

- 默认准星操控进入用户已验证的高响应区间；最大速度不再掩盖控制加速度，F10 可分别调整两者。
- 横向后坐力先产生明确的径向初速，再在短弯曲时间内平滑转向随机角度；高偏转比例形成连续弧线而不产生单帧瞬移。鼠标静止仍不自动回正。
- Active Raid 使用 SDL 相对鼠标模式，光标不能离开窗口，玩家可以持续压枪。打开库存、医疗轮盘、开发面板或失去焦点时立即释放捕获并恢复系统光标。
- 当前玩法闭环使用精选运行时音频：手枪/步枪击发、远尾、弹匣/枪膛/清障、库存/UI、医疗/受伤、感染者和 Base/Raid 低密度环境声。音频只消费既成玩法事实，不参与射击、碰撞、伤害、库存或结算。

## 2. 状态与边界

- `WeaponAimState` 继续位于 SDL-free simulation；新增每帧相对位移输入、即时径向后坐力初速和短方向弯曲阶段。随机角度使用现有 PCG32，击发刷新运动而不叠加。
- `maximumReticleSpeed`、`reticleControlAcceleration` 和 `recoilBendDurationSeconds` 都是可测试运行参数；人机工效仍派生控制加速度和后坐力减速度。
- 相对鼠标启停与焦点/UI 仲裁只属于 `raidline_sdl_client`。App 把 `xrel/yrel` 翻译成 `GameplayInput::aimMotionDelta`，simulation 不依赖 SDL 或 OS 光标位置。
- `GameAudioOutput` 与 `SoundEventId` 属于 SDL client；48 kHz mono WAV 在启动时由 SDL3 原生加载/转换，回调混音按事件限制并发和冷却。`GameSessionPresentationEvent` 只表达动作事实，不引用文件名。
- 播放设备打开前请求 512 sample-frame 缓冲，在 48 kHz 下对应约 10.7 ms 的游戏侧目标。SDL/平台可以调整该值，远程桌面音频重定向仍属于游戏之外的附加延迟。
- `assets/audio/v1/sound_events.json` 是音频表现 bank，不进入 Profile 内容版本或存档。`SOURCES.md` 和 `tools/audio_pipeline/build_p0_audio.ps1` 保存精确来源与离线处理配方；源 ZIP 不进入仓库。
- 本次是用户于 2026-08-21 对这一命名 P0 包的明确授权；PNG、美术 manifest、Grab/Scratch/Bite、P1 音频及整包导入仍在范围外。

## 3. 实施与退出条件

1. 调整默认武器人机工效、最大准星速度与控制加速度映射；F10 增加控制加速度参数。退出条件：高速度/高人机工效不再被旧加速度上限拖慢。
2. 将后坐力侧向速度叠加迁移为“即时径向初速 + 有界角度弯曲”；F10 增加弯曲时间。退出条件：总速度有界、连续开火刷新不叠加、极端横向参数形成连续弧线且无单帧跳点。
3. 建立 Raid 相对鼠标捕获和 UI/焦点仲裁。退出条件：Active Raid 持续消费相对位移；所有模态 UI 与失焦释放捕获；切换不造成隐藏准星跳变。
4. 用稳定 Sound Event bank 替换程序化 cue，接入枪械、库存/UI、医疗/受伤、感染者与两类环境循环。退出条件：资源和 bank 自动校验；无音频设备时游戏仍能运行；音频只消费语义事实。
5. focused、Windows Debug 全目标、全量 CTest、exact-head Windows/Ubuntu CI 后交由用户正常游玩验收。开发代理不自行启动游戏。

## 4. 自动化、风险与回滚

- 自动化覆盖相对位移、速度/加速度上限、后坐力径向初速与连续弯曲、随机角度总速度、刷新不叠加、鼠标捕获决策、稳定事件完整性、路径安全、WAV 格式/时长及关键会话事件。
- 音频自动化额外限制 Base 环境循环的过零率与事件增益，防止尖锐电流噪声重新成为安全区底噪；资源脚本重复运行必须得到相同 SHA-256。
- 风险：过高默认响应削弱武器重量感；通过独立加速度参数调节。后坐力过于平滑会缺少冲击；径向初速即时生效，弯曲时间只控制横向轨迹。相对模式与 UI 冲突通过单一纯决策函数收束。P0 音色与响度仍需用户正常游玩验收；事件并发、冷却和 master gain 可集中调整，不修改玩法领域。远程桌面可能忽略或掩盖较小的游戏侧缓冲收益，应分别比较本机与远程体验。
- 整个 PR 可回滚到 `origin/main@881c034`，不改变 schema、Profile、内容 ID 或 manifest。

## 5. 进度

- 2026-08-20：从干净 `origin/main@881c034` 创建分支。
- 2026-08-20：完成默认操控、相对位移、径向初速后连续弯曲的后坐力、相对鼠标捕获、F10 新参数与临时程序化音效实现。
- 2026-08-20：按用户二次调参反馈把默认最大准星速度提高到面板上限 5000、人机工效提高到 100；横向后坐力改为即时径向初速后连续弯向随机角度。
- 2026-08-20：Windows Debug 全目标构建和 751/751 CTest 通过；`Project_Raidline.exe` 已生成但开发代理未启动。提交、Draft PR 与 exact-head CI 待完成。
- 2026-08-20：提交 `f32653d` 推送至 Draft PR #68，exact-head Windows/Ubuntu CI 成功；等待用户正常游玩验收。
- 2026-08-21：用户授权从 ArtWorkbench 精选并接入 P0 音效。已生成统一 runtime WAV、稳定事件 bank、来源记录和可复现脚本；程序化音频实现退场。开发代理未启动游戏。
- 2026-08-21：Windows Debug 全目标构建与 755/755 CTest 通过，0 失败；专项覆盖 bank 完整性、WAV 格式、路径安全、敌人警觉、换弹与医疗事件。提交 `f779d31` 已推送，exact-head Windows/Ubuntu CI 全部成功；待用户正常游玩验收。
- 2026-08-21：用户报告 Base 出现强烈电流/滋滋声。确认不是循环叠加或格式错误，而是灯泡/线圈拾音素材本身具有高频噪声；替换为经过频段收束与低响度处理的 InMotionAudio 室内烟囱风声，Base 事件增益由 0.42 降至 0.28，过零率由约 0.344 降至 0.007。
- 2026-08-21：针对远程桌面听感延迟，在 SDL 设备打开前请求 512 帧缓冲；游戏侧目标约 10.7 ms，不能消除远程重定向的附加延迟。修订提交 `8efcd8d` 已完成 Windows Debug 全目标、GameAudio 6/6 与全量 CTest 756/756；开发代理未启动游戏，待新 exact-head CI 与用户正常游玩验收。
- 2026-08-21：修订 head 通过 exact-head Windows/Ubuntu CI；用户确认 P0 音效整体方向无误，小瑕疵和远程桌面附加延迟延期处理。PR #68 以 merge commit `ba3375e` 进入 main。
