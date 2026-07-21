#pragma once

struct GameplayInput
{
    bool moveUp{};
    bool moveDown{};
    bool moveLeft{};
    bool moveRight{};

    bool fireJustPressed{};
    bool firePressed{};

    // 只在 F 从未按下变为按下的这一帧为 true。
    bool interactJustPressed{};
};