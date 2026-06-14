cmake_minimum_required(VERSION 3.15)
project(Project_Raidline CXX)

# 1. 寻找 Vcpkg 安装的第三方依赖
find_package(fmt CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

# 2. 编译游戏主可执行文件
add_executable(Project_Raidline
    main.cpp
    app.cpp
    app.h
    input_system.h
    input_system.cpp
)
target_compile_features(Project_Raidline PRIVATE cxx_std_20)
set_target_properties(Project_Raidline PROPERTIES CXX_EXTENSIONS OFF)
# 链接 fmt 和 SDL3 库
target_link_libraries(Project_Raidline PRIVATE fmt::fmt SDL3::SDL3)


# 3. 开启测试功能并配置 GTest 可执行文件
enable_testing()
add_executable(HealthTest
    test_health.cpp
)

target_compile_features(HealthTest PRIVATE cxx_std_20)
set_target_properties(HealthTest PROPERTIES CXX_EXTENSIONS OFF)

# 将 GTest 主程序与 fmt 库链接到我们的测试目标上
target_link_libraries(HealthTest PRIVATE GTest::gtest_main fmt::fmt)

# 向 CMake 的 CTest 工具注册所有的 Google Test 用例
include(GoogleTest)
gtest_discover_tests(HealthTest)

add_executable(InputSystemTest
    test_input_system.cpp
    input_system.cpp
    input_system.h
)
target_compile_features(InputSystemTest PRIVATE cxx_std_20)
set_target_properties(InputSystemTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(InputSystemTest PRIVATE GTest::gtest_main SDL3::SDL3)
gtest_discover_tests(InputSystemTest)