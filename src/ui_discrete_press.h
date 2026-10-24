#pragma once
#include <string>
#include <string_view>

// A pending client gesture, never a command or an owning object reference.
// Re-evaluate the visible action and context on release before dispatching.
struct UiDiscretePress {
  int action{};
  std::string context;
  void cancel() noexcept { action = 0; context.clear(); }
  void begin(int nextAction, std::string_view nextContext) {
    action = nextAction;
    context = nextAction ? std::string(nextContext) : std::string{};
  }
  [[nodiscard]] bool release(int currentAction, std::string_view currentContext) {
    const bool accepted = action != 0 && action == currentAction && context == currentContext;
    cancel();
    return accepted;
  }
};
