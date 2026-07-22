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
    src/item_definition.h
    src/item_definition.cpp
    src/item_instance.h
    src/item_instance.cpp
    src/ground_item.h
    src/ground_item.cpp
    src/rect.h
    src/collision.cpp
    src/collision.h
    src/enemy.h
    src/enemy.cpp
    src/hit_resolution.h
    src/hit_resolution.cpp
    src/gameplay_input.h
    src/gameplay_world.h
    src/gameplay_world.cpp
    src/texture.h
    src/texture.cpp
    src/animation.h
    src/animation.cpp
    src/particle.h
    src/particle.cpp
    src/particle_system.h
    src/particle_system.cpp
    src/grid_inventory.h
    src/grid_inventory.cpp
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
target_link_libraries(HealthTest PRIVATE GTest::gtest_main)
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
    src/gameplay_input.h
    src/animation.h
    src/animation.cpp
)

target_compile_features(PlayerTest PRIVATE cxx_std_20)
set_target_properties(PlayerTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(PlayerTest PRIVATE GTest::gtest_main)
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

# test enemy
add_executable(EnemyTest
    tests/test_enemy.cpp
    src/enemy.cpp
    src/enemy.h
    src/animation.h
    src/animation.cpp
)

target_compile_features(EnemyTest PRIVATE cxx_std_20)
set_target_properties(EnemyTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(EnemyTest PRIVATE GTest::gtest_main)
target_include_directories(EnemyTest PRIVATE src)

gtest_discover_tests(EnemyTest)

# test hit_resolution
add_executable(HitResolutionTest
    tests/test_hit_resolution.cpp
    src/hit_resolution.h
    src/hit_resolution.cpp
    src/collision.cpp
    src/collision.h
    src/enemy.cpp
    src/enemy.h
    src/projectile.cpp
    src/projectile.h
    src/rect.h
    src/vec2.h
    src/animation.h
    src/animation.cpp
)

target_compile_features(HitResolutionTest PRIVATE cxx_std_20)
set_target_properties(HitResolutionTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(HitResolutionTest PRIVATE GTest::gtest_main)
target_include_directories(HitResolutionTest PRIVATE src)

gtest_discover_tests(HitResolutionTest)

# test gameplay_world
add_executable(GameplayWorldTest
    tests/test_gameplay_world.cpp
    src/gameplay_world.cpp
    src/gameplay_world.h
    src/gameplay_input.h

    src/player.cpp
    src/player.h

    src/projectile.cpp
    src/projectile.h

    src/enemy.cpp
    src/enemy.h

    src/rect.h
    src/vec2.h

    src/hit_resolution.cpp
    src/hit_resolution.h

    src/collision.cpp
    src/collision.h

    src/animation.h
    src/animation.cpp

    src/particle.h
    src/particle.cpp
    src/particle_system.h
    src/particle_system.cpp

    src/item_definition.h
    src/item_definition.cpp
    src/item_instance.h
    src/item_instance.cpp
    src/ground_item.h
    src/ground_item.cpp
)

target_compile_features(GameplayWorldTest PRIVATE cxx_std_20)
set_target_properties(GameplayWorldTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(GameplayWorldTest PRIVATE GTest::gtest_main)
target_include_directories(GameplayWorldTest PRIVATE src)

gtest_discover_tests(GameplayWorldTest)

# test texture
add_executable(TextureTest
    tests/test_texture.cpp
    src/texture.h
    src/texture.cpp
)

target_compile_features(TextureTest PRIVATE cxx_std_20)
set_target_properties(TextureTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(TextureTest PRIVATE GTest::gtest_main SDL3::SDL3)
target_include_directories(TextureTest PRIVATE src)

gtest_discover_tests(TextureTest)

# test animation
add_executable(AnimationTest
    tests/test_animation.cpp
    src/animation.h
    src/animation.cpp
)

target_compile_features(AnimationTest PRIVATE cxx_std_20)
set_target_properties(AnimationTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(AnimationTest PRIVATE GTest::gtest_main)
target_include_directories(AnimationTest PRIVATE src)

gtest_discover_tests(AnimationTest)

# test particle
add_executable(ParticleTest
    tests/test_particle.cpp
    src/particle.h
    src/particle.cpp
)

target_compile_features(ParticleTest PRIVATE cxx_std_20)
set_target_properties(ParticleTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(ParticleTest PRIVATE GTest::gtest_main)
target_include_directories(ParticleTest PRIVATE src)

gtest_discover_tests(ParticleTest)

# test particle_system
add_executable(ParticleSystemTest
    tests/test_particle_system.cpp
    src/particle_system.h
    src/particle_system.cpp
    src/particle.h
    src/particle.cpp
)

target_compile_features(ParticleSystemTest PRIVATE cxx_std_20)
set_target_properties(ParticleSystemTest PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(ParticleSystemTest PRIVATE GTest::gtest_main)
target_include_directories(ParticleSystemTest PRIVATE src)

gtest_discover_tests(ParticleSystemTest)

# test item_definition
add_executable(ItemDefinitionTest
    tests/test_item_definition.cpp
    src/item_definition.h
    src/item_definition.cpp
    src/vec2.h
)

target_compile_features(
    ItemDefinitionTest
    PRIVATE
    cxx_std_20
)

set_target_properties(
    ItemDefinitionTest
    PROPERTIES
    CXX_EXTENSIONS OFF
)

target_link_libraries(
    ItemDefinitionTest
    PRIVATE
    GTest::gtest_main
)

target_include_directories(
    ItemDefinitionTest
    PRIVATE
    src
)

gtest_discover_tests(ItemDefinitionTest)

# test item_instance
add_executable(ItemInstanceTest
    tests/test_item_instance.cpp
    src/item_instance.h
    src/item_instance.cpp
    src/item_definition.h
    src/item_definition.cpp
    src/vec2.h
)

target_compile_features(
    ItemInstanceTest
    PRIVATE
    cxx_std_20
)

set_target_properties(
    ItemInstanceTest
    PROPERTIES
    CXX_EXTENSIONS OFF
)

target_link_libraries(
    ItemInstanceTest
    PRIVATE
    GTest::gtest_main
)

target_include_directories(
    ItemInstanceTest
    PRIVATE
    src
)

gtest_discover_tests(ItemInstanceTest)

# test ground_item
add_executable(GroundItemTest
    tests/test_ground_item.cpp
    src/ground_item.h
    src/ground_item.cpp
    src/item_instance.h
    src/item_instance.cpp
    src/item_definition.h
    src/item_definition.cpp
    src/rect.h
    src/vec2.h
)

target_compile_features(
    GroundItemTest
    PRIVATE
    cxx_std_20
)

set_target_properties(
    GroundItemTest
    PROPERTIES
    CXX_EXTENSIONS OFF
)

target_link_libraries(
    GroundItemTest
    PRIVATE
    GTest::gtest_main
)

target_include_directories(
    GroundItemTest
    PRIVATE
    src
)

gtest_discover_tests(GroundItemTest)

# test grid_inventory
add_executable(GridInventoryTest
    tests/test_grid_inventory.cpp
    src/grid_inventory.h
    src/grid_inventory.cpp
    src/item_instance.h
    src/item_instance.cpp
    src/item_definition.h
    src/item_definition.cpp
    src/vec2.h
)

target_compile_features(
    GridInventoryTest
    PRIVATE
    cxx_std_20
)

set_target_properties(
    GridInventoryTest
    PROPERTIES
    CXX_EXTENSIONS OFF
)

target_link_libraries(
    GridInventoryTest
    PRIVATE
    GTest::gtest_main
)

target_include_directories(
    GridInventoryTest
    PRIVATE
    src
)

gtest_discover_tests(GridInventoryTest)

# utf-8
if(MSVC)
    target_compile_options(Project_Raidline PRIVATE /utf-8)
    target_compile_options(HealthTest PRIVATE /utf-8)
    target_compile_options(InputSystemTest PRIVATE /utf-8)
    target_compile_options(PlayerTest PRIVATE /utf-8)
    target_compile_options(ProjectileTest PRIVATE /utf-8)
    target_compile_options(CollisionTest PRIVATE /utf-8)
    target_compile_options(EnemyTest PRIVATE /utf-8)
    target_compile_options(HitResolutionTest PRIVATE /utf-8)
    target_compile_options(GameplayWorldTest PRIVATE /utf-8)
    target_compile_options(TextureTest PRIVATE /utf-8)
    target_compile_options(AnimationTest PRIVATE /utf-8)
    target_compile_options(ParticleTest PRIVATE /utf-8)
    target_compile_options(ParticleSystemTest PRIVATE /utf-8)
    target_compile_options(
        ItemDefinitionTest
        PRIVATE
        /utf-8
    )
    target_compile_options(
        ItemInstanceTest
        PRIVATE
        /utf-8
    )

    target_compile_options(
        GroundItemTest
        PRIVATE
        /utf-8
    )
    target_compile_options(
    GridInventoryTest
    PRIVATE
    /utf-8
    )
endif()
