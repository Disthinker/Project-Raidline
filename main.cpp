#include <iostream>
#include <string_view>

// 使用 C++17 的 std::string_view 来替代传统的const char*，提升只读字符串的性能
void initializeEngine(std::string_view engineName)
{
    std::cout << "[SYSTEM] " << engineName << " Engine Initialized. Ready to hunt zombies.\n";
}

int main()
{
    initializeEngine("PixelZombie-2D");
    return 0;
}