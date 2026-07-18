# Project Raidline Week 11 开发日志

## 一、本周主题

Week 11 最终主题为：

```text
通用帧动画系统与 Player / Enemy 水平移动动画
```

本周最初曾考虑将动画系统接入 HitEffect，但在评估命中特效的长期表现方案后，开发路线调整为：

```text
保留 Week 9 已有 HitEffect 黄色 impact marker；
停止继续扩展 HitEffect 帧动画；
优先建立可复用的通用帧动画模型；
将动画系统接入 Player 和 Enemy 的水平移动表现。
```

HitEffect 原有的命中位置、生命周期、黄色方块渲染和到期清理逻辑均被保留，没有删除，也没有推翻。真正的粒子命中特效留到后续独立任务中实现。

本周最终完成的游戏功能是：

```text
Player 向左 / 向右移动时播放 6 帧循环动画；
Player 停止水平移动后保持最后朝向，并显示对应方向的第 0 帧；
Enemy 水平移动时播放 6 帧循环动画；
Enemy 碰到左右边界反弹后立即切换视觉方向；
Player 和 Enemy 均通过 SDL source rect 从合并 sprite sheet 中切换当前帧。
```

---

## 二、本周开发前的项目状态

Week 10 完成后，项目已经具备：

```text
Player 移动与边界限制
Player facingDirection
Enemy 水平移动与左右反弹
Projectile 多方向射击
Fire cooldown
Projectile / Enemy 碰撞
HitEffect 黄色命中标记
Texture RAII wrapper
SDL3 / SDL3_image 渲染
GTest 与 Windows / Ubuntu CI 基础
```

但此时仍然没有通用动画模型：

```text
没有 AnimationFrame
没有 AnimationClip
没有 Animator
没有帧索引推进
没有 Loop / Once 播放规则
Player 使用静态图片
Enemy 使用红色矩形
```

---

## 三、通用 Animation 模型

本周新增：

```text
src/animation.h
src/animation.cpp
tests/test_animation.cpp
```

### 1. AnimationFrame

`AnimationFrame` 使用简单值类型表示一帧动画的数据：

```cpp
struct AnimationFrame
{
    float durationSeconds{};
};
```

当前每帧只保存持续时间，不保存 Texture、SDL_Rect 或角色方向。

这样设计的原因是：

```text
AnimationFrame 负责帧数据；
Animator 负责时间推进；
App 负责根据 frame index 计算 source rect 并渲染。
```

`AnimationFrame` 不拥有资源，也不需要智能指针。

### 2. AnimationPlayMode

本周定义：

```cpp
enum class AnimationPlayMode
{
    Loop,
    Once
};
```

语义为：

```text
Loop：播放到最后一帧后回到第 0 帧；
Once：播放完最后一帧后停在最后一帧，并进入 finished 状态。
```

使用 `enum class` 的原因是：

```text
枚举值具有作用域；
使用时必须写 AnimationPlayMode::Loop；
不会把 Loop / Once 污染到外部命名空间；
不会轻易与 int 隐式混用。
```

### 3. AnimationClip

`AnimationClip` 按值保存：

```cpp
std::vector<AnimationFrame> frames_;
```

其职责包括：

```text
保存有序帧表；
返回 frameCount()；
通过只读 frame(index) 访问指定帧；
拒绝空帧表；
拒绝 durationSeconds <= 0；
使用 vector::at() 提供索引边界检查。
```

构造函数使用成员初始化列表和 `std::move`，将传入帧表的内部存储转移给成员，避免不必要的复制。

### 4. Animator

`Animator` 按值持有 `AnimationClip`，并保存：

```text
AnimationPlayMode playMode_
std::size_t currentFrameIndex_
float timeInCurrentFrame_
bool finished_
```

职责包括：

```text
累计 deltaTime；
根据当前帧 duration 推进 frame index；
支持一次 update 跨过多个帧；
支持 Loop 回绕；
支持 Once 播放完成；
支持 reset；
提供只读 currentFrameIndex() 和 isFinished()。
```

Animator 不依赖：

```text
SDL_Renderer
SDL_Texture
SDL_FRect
Player
Enemy
具体图片尺寸
具体资源路径
```

因此它可以被多个游戏对象复用。

---

## 四、时间推进算法

### 1. 时间累计

Animator 每次更新：

```text
timeInCurrentFrame += deltaTime
```

当累计时间达到当前帧时长时：

```text
减去当前帧 duration；
推进到下一帧；
继续检查剩余时间。
```

### 2. 为什么使用 while

如果只使用 `if`，一次较大的 `deltaTime` 最多只能推进一帧。

本周使用 `while`，因此：

```text
deltaTime = 0.35 秒
frame duration = 0.10 秒
```

可以在一次 update 中跨过 3 个帧边界，并保留剩余时间。

### 3. Loop

Loop 规则：

```text
0 → 1 → 2 → 3 → 4 → 5 → 0
```

回到第 0 帧后不会清空尚未消耗的剩余时间。

### 4. Once

Once 规则：

```text
进入最后一帧时还不算完成；
最后一帧也必须完整播放一个 duration；
播放完成后停在最后一帧；
isFinished() 返回 true；
后续 update 不再改变状态。
```

### 5. reset

`reset()` 会恢复：

```text
currentFrameIndex = 0
timeInCurrentFrame = 0
finished = false
```

---

## 五、Player 移动动画

### 1. 对象组合

Player 使用对象组合持有：

```cpp
Animator movementAnimator_;
```

Player 不是 Animator，因此没有使用继承。

Player 新增只读接口：

```text
bool isMoving() const
std::size_t currentAnimationFrameIndex() const
```

没有暴露可修改的 Animator 引用。

### 2. Player 移动状态

Player 根据合成后的移动方向长度判断是否 Moving：

```text
非零方向 → Moving
无输入 → Idle
左 + 右抵消 → Idle
上 + 下抵消 → Idle
```

Player 顶着边界继续按移动键时，位置会被边界限制，但仍然存在移动意图，因此移动动画继续推进。

### 3. Player 动画规则

Player 移动 Clip：

```text
6 帧
每帧 0.09 秒
Loop
```

Moving 时：

```text
更新位置；
更新 facingDirection；
更新 movementAnimator。
```

从 Moving 进入 Idle 时：

```text
movementAnimator.reset()
```

### 4. Player 资源

正式运行资源：

```text
assets/characters/player/default/
player_default_move_horizontal_6f_1536x640.png
```

规格：

```text
总尺寸：1536×640
布局：6 列 × 2 行
单帧：256×320
第 0 行：Left
第 1 行：Right
```

实际显示尺寸仍为：

```text
64×80
```

Player 的逻辑碰撞尺寸保持原有 `32×32`，没有因资源尺寸变化而扩大。

### 5. Player Idle / Moving 渲染

渲染规则：

```text
具有水平朝向且正在移动：
    使用 Animator 当前帧。

具有水平朝向但已经停止：
    使用对应方向行的 frame 0。

初始朝上或纯上 / 下移动：
    暂时使用原静态 Player 图片作为 fallback。
```

这样修复了：

```text
Player 向右移动后松开按键，突然切回固定左向静态图
```

的问题。

---

## 六、Enemy 移动动画

### 1. EnemyFacingDirection

新增：

```cpp
enum class EnemyFacingDirection
{
    Left,
    Right
};
```

方向规则：

```text
velocity.x < 0 → Left
velocity.x > 0 → Right
velocity.x == 0 → 默认保留确定性的 Right 初始方向
```

### 2. Enemy 对象组合

Enemy 新增：

```text
EnemyFacingDirection facingDirection_
Animator movementAnimator_
```

并提供只读接口：

```text
facingDirection()
isMoving()
currentAnimationFrameIndex()
```

Enemy 不持有 Texture，也不依赖 SDL。

### 3. Enemy 动画规则

Enemy 移动 Clip：

```text
6 帧
每帧 0.125 秒
Loop
```

更新顺序：

```text
1. position.x += velocity.x × deltaTime
2. 处理右边界反弹
3. 处理左边界反弹
4. 根据反弹后的最终 velocity.x 更新视觉方向
5. 移动时推进 Animator，静止时 reset
```

### 4. 反弹后的视觉方向

Enemy 撞到右边界后：

```text
velocity.x 变为负数；
facingDirection 立即变为 Left。
```

Enemy 撞到左边界后：

```text
velocity.x 变为正数；
facingDirection 立即变为 Right。
```

反弹只改变方向行，不重置当前动画帧，因此步态连续：

```text
Right / frame 3
→ 反弹
→ Left / frame 3
```

### 5. Enemy 资源

正式运行资源：

```text
assets/characters/enemy/default/
enemy_default_move_horizontal_6f_1536x640.png
```

规格与 Player 相同：

```text
总尺寸：1536×640
布局：6 列 × 2 行
单帧：256×320
第 0 行：Left
第 1 行：Right
```

Enemy 实际显示尺寸为：

```text
64×80
```

逻辑碰撞尺寸仍保持 GameplayWorld 中原有的 `50×50`。

---

## 七、SDL source rect 渲染

App 根据当前帧和方向计算 source rect：

```text
sourceX = frameIndex × 256
sourceY = directionRow × 320
sourceWidth = 256
sourceHeight = 320
```

例如：

```text
frameIndex = 3
Right row = 1

sourceX = 3 × 256 = 768
sourceY = 1 × 320 = 320
```

然后将 `256×320` 的源帧绘制为 `64×80` 的目标矩形。

为了保持像素边缘清晰，Player 和 Enemy 动画 Texture 都设置：

```cpp
SDL_SCALEMODE_NEAREST
```

Animator 只输出 frame index，不参与 SDL source rect 计算。

---

## 八、Texture RAII 与资源生命周期

Player 和 Enemy 动画 Texture 均由 App 使用 Week 10 的 `Texture` wrapper 管理。

App 当前持有：

```text
backgroundTexture_
playerTexture_
playerMoveHorizontalTexture_
enemyMoveHorizontalTexture_
```

加载流程：

```text
先加载到局部 Texture；
检查 valid()；
设置 nearest scale mode；
全部成功后 std::move 到 App 成员。
```

失败时，局部 Texture 会自动析构，避免资源泄漏。

关闭流程：

```text
Enemy animation Texture reset
Player animation Texture reset
Player static Texture reset
Background Texture reset
Renderer destroy
Window destroy
SDL_Quit
```

本周曾发现 `SDL_DestroyRenderer(renderer_)` 被连续调用两次，已经修复为只销毁一次，并随后将指针设为 `nullptr`。

---

## 九、动画资产处理

本周使用 GPT 生成 Player 和 Enemy 水平移动动画资源。

生成过程中遇到：

```text
帧数错误
角色细节扭曲
动作图数量不稳定
棋盘格被画入图片而不是真透明
单帧锚点轻微漂移
首尾循环姿态不自然
```

最终采用的处理策略是：

```text
按单方向生成 6 帧；
将左右条带整理为 6×2 合并 sprite sheet；
统一每帧为 256×320；
统一脚底锚点；
清理为真正透明背景；
程序只加载最终合并图。
```

本周收口时删除了不再使用的逐帧图、中间条带和部分旧静态资源，只保留正式运行资产。

---

## 十、自动化测试

### 1. AnimationTest

新增：

```text
tests/test_animation.cpp
```

覆盖：

```text
AnimationClip 保存帧顺序
空 Clip 抛出 invalid_argument
duration == 0 被拒绝
负 duration 被拒绝
非法索引抛出 out_of_range
Animator 初始 frame 0
非正 deltaTime 不推进
小 deltaTime 不推进
跨 update 时间累计
刚好达到 duration 推进
不同帧独立 duration
大 deltaTime 跨多帧
Loop 回绕
Loop 保留剩余时间
跨多轮 Loop
单帧 Loop
Once 进入末帧但未完成
Once 播放完末帧后 finished
Once 完成后忽略后续 update
单帧 Once
reset 清除索引、累计时间和 finished
```

### 2. PlayerTest

新增覆盖：

```text
初始 Idle 和 frame 0
有效移动进入 Moving
移动时间累计推进 frame
无输入保持 Idle
停止移动后 reset
相反水平输入不启动动画
垂直输入属于 Moving
6 帧移动动画 Loop
边界处移动意图仍推进动画
停止向右移动后保持 Right facing 并回到 frame 0
```

### 3. EnemyTest

新增覆盖：

```text
正速度初始朝右
负速度初始朝左
静止 Enemy 不推进
移动时间累计推进 frame
右边界反弹后朝左
左边界反弹后朝右
反弹保持动画进度
6 帧移动动画 Loop
```

### 4. GameplayWorldTest

新增覆盖：

```text
GameplayWorld update 推进 Enemy 动画
Enemy 反弹后暴露正确的 Left 视觉方向
Enemy frame index 保持在合法范围内
```

### 5. CMake

新增 `AnimationTest` target，并将 `animation.cpp` 加入：

```text
Project_Raidline
AnimationTest
PlayerTest
EnemyTest
HitResolutionTest
GameplayWorldTest
```

纯逻辑测试没有额外链接 SDL。

---

## 十一、人工运行验收

本周已人工确认：

```text
背景正常显示
Player 静止显示正常
Player 向左移动动画正常
Player 向右移动动画正常
Player 停止后保持最后水平朝向
Player 动画能够循环
Enemy 移动动画正常
Enemy 到右边界后切换为 Left
Enemy 到左边界后切换为 Right
Enemy 反弹时动画帧没有明显重置
Projectile 正常
Projectile 能命中 Enemy
HitEffect 保持黄色方块
HitEffect 自动消失
像素贴图没有棋盘格背景
像素缩放清晰
程序正常退出且不崩溃
```

---

## 十二、本周典型 Bug 与修正

### Bug 1：Loop 回绕到 frame 1

错误实现：

```cpp
currentFrameIndex_ = 1;
```

正确实现：

```cpp
currentFrameIndex_ = 0;
```

该错误会导致 Loop 跳过第 0 帧，并让单帧 Loop 访问非法索引。

### Bug 2：Renderer 重复销毁

错误：

```cpp
SDL_DestroyRenderer(renderer_);
SDL_DestroyRenderer(renderer_);
```

修正：

```cpp
SDL_DestroyRenderer(renderer_);
renderer_ = nullptr;
```

### Bug 3：Player 向右停止后跳回左向图

原因：

```text
Idle 时无条件使用固定左向静态图片。
```

修正：

```text
如果具有水平朝向，Idle 使用对应方向行的 frame 0。
```

### Bug 4：动画资源不是真透明

生成图片中的棋盘格是实际 RGB 内容，而不是 Alpha 通道。

修正：

```text
清除棋盘格；
输出 RGBA PNG；
统一固定网格和脚底锚点。
```

### Bug 5：新增 Animator 后测试目标出现潜在链接缺口

`PlayerTest`、`EnemyTest`、`HitResolutionTest` 和 `GameplayWorldTest` 会直接编译依赖 Animator 的源码，因此必须把 `animation.cpp` 加入对应 target，否则会出现：

```text
LNK2019
undefined reference
```

---

## 十三、本周 C++ 学习内容

本周重点学习并实践了：

```text
struct 与 class 的职责区别
enum class
std::vector
std::size_t
索引边界
成员初始化列表
std::move
对象组合
const getter
const 引用返回
异常与错误输入保护
时间累计成员变量
while 跨多帧
状态转换
编译错误与链接错误判断
RAII 资源生命周期
逻辑层与渲染层分离
```

### 已进一步掌握

```text
frame index 必须小于 frameCount
空 vector 不能访问 frame 0
size_t 适合容器索引
累计时间必须保存为成员变量
大 deltaTime 需要 while，而不是单次 if
Loop 和 Once 的边界语义不同
声明存在但实现未进入 target 会产生链接错误
Texture 必须在 Renderer 前释放
```

### 仍需继续强化

```text
复杂对象初始化顺序
多动画状态切换
资源描述数据与渲染配置分离
浮点边界测试设计
更大规模 CMake target 复用
```

---

## 十四、本周未实现内容

Week 11 明确没有实现：

```text
Player 向上 / 向下移动动画
Enemy 向上 / 向下动画
多帧 Idle 动画
射击动画
受伤动画
死亡动画
复杂 AnimationStateMachine
AnimationController 大框架
粒子系统
换装系统
TextureManager
Sprite Atlas 管理器
骨骼动画
动画编辑器
音效
屏幕震动
Wave
Loot
撤离
ECS
SceneManager
对象池
```

这些内容不会在 Week 11 临时扩展。

---

## 十五、核心文件变化

### 新增

```text
src/animation.h
src/animation.cpp
tests/test_animation.cpp

assets/characters/player/default/
player_default_move_horizontal_6f_1536x640.png

assets/characters/enemy/default/
enemy_default_move_horizontal_6f_1536x640.png
```

### 修改

```text
src/player.h
src/player.cpp
src/enemy.h
src/enemy.cpp
src/app.h
src/app.cpp
tests/test_player.cpp
tests/test_enemy.cpp
tests/test_gameplay_world.cpp
CMakeLists.txt
```

### 继续保留

```text
HitEffect 黄色方块
HitEffect lifetime
HitEffect position
HitEffect cleanup
```

---

## 十六、本地验证与 Git 状态

当前开发分支：

```text
week11-basic-animation-system
```

截至 DevLog 生成时，分支相对 `main`：

```text
ahead 11
behind 0
```

人工运行：

```text
PASS
```

最终提交或创建 PR 前仍需再次记录：

```text
cmake --build --preset windows-debug
ctest --preset windows-debug -R Animation --output-on-failure
ctest --preset windows-debug -R PlayerTest --output-on-failure
ctest --preset windows-debug -R EnemyTest --output-on-failure
ctest --preset windows-debug -R GameplayWorldTest --output-on-failure
ctest --preset windows-debug --output-on-failure
git diff --check
git status
```

最终结果填写：

```text
Build：待最终收口确认
AnimationTest：待最终收口确认
PlayerTest：待最终收口确认
EnemyTest：待最终收口确认
GameplayWorldTest：待最终收口确认
全量 CTest：待最终收口确认
Windows Actions：待 PR 后确认
Ubuntu Actions：待 PR 后确认
PR 编号：待创建
main 合并状态：待完成
最终工作区：待合并后确认 clean
```

---

## 十七、提交与 PR 建议

Week 11 分支包含多个开发过程 commit，最终 PR 建议采用：

```text
Squash and merge
```

PR 标题建议：

```text
feat: add reusable animation system and movement sprites
```

PR 描述应说明：

```text
新增通用 AnimationClip / Animator；
支持 Loop / Once 和大 deltaTime；
Player 增加左右水平移动动画；
Enemy 增加移动动画和反弹换向；
Texture 继续使用 RAII；
HitEffect 保持 Week 9 静态黄色标记；
没有引入复杂动画状态机、粒子系统或 ECS。
```

---

## 十八、本周总结

Week 11 完成了一套可测试、可复用、与 SDL 解耦的基础帧动画模型，并将它实际接入 Player 和 Enemy 的水平移动表现。

本周形成的完整链路是：

```text
AnimationFrame / AnimationClip
        ↓
Animator 时间推进
        ↓
Player / Enemy 对象组合
        ↓
移动状态与视觉方向
        ↓
App 计算 SDL source rect
        ↓
Texture RAII 管理
        ↓
Player / Enemy 可见帧动画
```

本周的价值不仅是让角色“动起来”，还系统训练了：

```text
C++ 数据建模
vector 帧表
size_t 索引
时间累计算法
Loop / Once 边界
对象组合
逻辑与渲染分离
异常输入保护
自动化测试
SDL 资源生命周期
```

当前动画系统已经可以作为后续以下功能的基础：

```text
Player 上下方向动画
多帧 Idle
射击动画
Enemy 受伤 / 死亡动画
UI 动画
换装表现
其他短时帧动画
```

Week 11 到此进入最终 PR、CI 和合并收口阶段，不在本周继续扩展新的动画类型。
