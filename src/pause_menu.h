#pragma once

enum class PauseMenuCommand
{
    Continue,
    Settings,
    MainMenu,
    ExitDesktop
};

// SDL-independent modal state. App owns hit testing and rendering while this
// type fixes Escape/back semantics for both Base and Raid screens.
class PauseMenuState
{
public:
    void open() noexcept
    {
        open_ = true;
        settingsOpen_ = false;
    }

    void close() noexcept
    {
        open_ = false;
        settingsOpen_ = false;
    }

    void showSettings() noexcept
    {
        if (open_)
        {
            settingsOpen_ = true;
        }
    }

    [[nodiscard]] bool handleEscape() noexcept
    {
        if (!open_)
        {
            return false;
        }
        if (settingsOpen_)
        {
            settingsOpen_ = false;
        }
        else
        {
            close();
        }
        return true;
    }

    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] bool settingsOpen() const noexcept
    {
        return settingsOpen_;
    }

private:
    bool open_{};
    bool settingsOpen_{};
};
