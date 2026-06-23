cmake_minimum_required(VERSION 3.15)
project(Project_Raidline CXX)

# 1. 寻找 Vcpkg 安装的第三方依赖
find_package(fmt CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)

# 2. 编译游戏主可执行文件
add_executable(Project_Raidline
    src/main.cpp
    src/app.cpp
    src/app.h
    src/input_system.h
    src/input_system.cpp
    src/player.h
    src/player.cpp
    src/projectile.h
    src/projectile.cpp
    src/vec2.h
    src/rect.h
    src/collision.cpp
    src/collision.h
)
target_compile_features(Project_Raidline PRIVATE cxx_std_20)
set_target_properties(Project_Raidline PROPERTIES CXX_EXTENSIONS OFF)
# 链接 第三方依赖的 库
target_link_libraries(Project_Raidline PRIVATE fmt::fmt SDL3::SDL3 SDL3_image::SDL3_image)
target_include_directories(Project_Raidline PRIVATE src)

add_custom_command(TARGET Project_Raidline POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:Project_Raidline>/assets
)

# 3. 开启测试功能并配置 GTest 可执行文件, test health_system
enable_testing()
add_executable(HealthTest
    tests/test_health.cpp
)

target_compile_features(HealthTest PRIVATE cxx_std_20)
set_target_properties(HealthTest PROPERTIES CXX_EXTENSIONS OFF)

# 将 GTest 主程序与 fmt 库链接到我们的测试目标上
target_link_libraries(HealthTest PRIVATE GTest::gtest_main fmt::fmt)
target_include_directories(HealthTest PRIVATE src)

# 向 CMake 的 CTest 工具注册所有的 Google Test 用例
include(GoogleTest)
gtest_discover_tests(HealthTest)

# test input_system
add_executable(InputSystemTest
    tests/test_input_system.cpp
    src/input_system.cpp
    src/input_system.h
)
target_compile_features(InputSystemTest PRIVATE cxx_std_20)
set_target_properties(InputSystemTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(InputSystemTest PRIVATE GTest::gtest_main SDL3::SDL3)
target_include_directories(InputSystemTest PRIVATE src)

gtest_discover_tests(InputSystemTest)

# test player
add_executable(PlayerTest
    tests/test_player.cpp
    src/player.cpp
    src/player.h
    src/input_system.cpp
    src/input_system.h
)

target_compile_features(PlayerTest PRIVATE cxx_std_20)
set_target_properties(PlayerTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(PlayerTest PRIVATE GTest::gtest_main SDL3::SDL3)
target_include_directories(PlayerTest PRIVATE src)

gtest_discover_tests(PlayerTest)

# test projectile
add_executable(ProjectileTest
    tests/test_projectile.cpp
    src/projectile.cpp
    src/projectile.h
    src/vec2.h
)

target_compile_features(ProjectileTest PRIVATE cxx_std_20)
set_target_properties(ProjectileTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(ProjectileTest PRIVATE GTest::gtest_main)
target_include_directories(ProjectileTest PRIVATE src)

gtest_discover_tests(ProjectileTest)

# test collision
add_executable(CollisionTest
    tests/test_collision.cpp
    src/collision.cpp
    src/collision.h
    src/rect.h
)

target_compile_features(CollisionTest PRIVATE cxx_std_20)
set_target_properties(CollisionTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(CollisionTest PRIVATE GTest::gtest_main)
target_include_directories(CollisionTest PRIVATE src)

gtest_discover_tests(CollisionTest)

# utf-8
if(MSVC)
    target_compile_options(Project_Raidline PRIVATE /utf-8)
    target_compile_options(HealthTest PRIVATE /utf-8)
    target_compile_options(InputSystemTest PRIVATE /utf-8)
    target_compile_options(PlayerTest PRIVATE /utf-8)
    target_compile_options(ProjectileTest PRIVATE /utf-8)
endif()