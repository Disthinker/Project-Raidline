cmake_minimum_required(VERSION 3.15)
project(Project_Raidline CXX)

# CMake 3.31 can mis-decode the UTF-8 Chinese MSVC /showIncludes prefix
# through the system ANSI code page. Ninja then records zero header
# dependencies, so a class-layout change may leave ABI-incompatible objects.
# Correct only that known bad detection; English and other locales keep the
# prefix CMake detected normally.
if(
    MSVC
    AND CMAKE_GENERATOR MATCHES "Ninja"
    AND CMAKE_CL_SHOWINCLUDES_PREFIX STREQUAL
        "娉ㄦ剰: 鍖呭惈鏂囦欢:  ")
    set(
        CMAKE_CL_SHOWINCLUDES_PREFIX
        "注意: 包含文件:  ")
endif()

find_package(fmt CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)

set(RAIDLINE_PUBLISHED_CONTENT_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/content/v1/core.json")
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${RAIDLINE_PUBLISHED_CONTENT_FILE}")
file(READ
    "${RAIDLINE_PUBLISHED_CONTENT_FILE}"
    RAIDLINE_PUBLISHED_CONTENT_JSON)
# Keep the generated C++ literal below MSVC's per-literal size ceiling. JSON
# strings cannot contain raw newlines, so stripping line indentation preserves
# the exact parsed content while avoiding a second runtime file dependency.
string(
    REGEX REPLACE
    "[\r\n]+[ \t]*"
    ""
    RAIDLINE_PUBLISHED_CONTENT_JSON
    "${RAIDLINE_PUBLISHED_CONTENT_JSON}")
# MSVC limits one source string token to roughly 16 KiB. Keep each generated
# token comfortably below that ceiling while relying on standard adjacent
# string-literal concatenation to expose one immutable string_view.
string(LENGTH "${RAIDLINE_PUBLISHED_CONTENT_JSON}" RAIDLINE_CONTENT_LENGTH)
set(RAIDLINE_PUBLISHED_CONTENT_LITERALS "")
set(RAIDLINE_CONTENT_OFFSET 0)
while(RAIDLINE_CONTENT_OFFSET LESS RAIDLINE_CONTENT_LENGTH)
    math(EXPR RAIDLINE_CONTENT_REMAINING
        "${RAIDLINE_CONTENT_LENGTH} - ${RAIDLINE_CONTENT_OFFSET}")
    if(RAIDLINE_CONTENT_REMAINING GREATER 8000)
        set(RAIDLINE_CONTENT_CHUNK_LENGTH 8000)
    else()
        set(RAIDLINE_CONTENT_CHUNK_LENGTH ${RAIDLINE_CONTENT_REMAINING})
    endif()
    string(SUBSTRING
        "${RAIDLINE_PUBLISHED_CONTENT_JSON}"
        ${RAIDLINE_CONTENT_OFFSET}
        ${RAIDLINE_CONTENT_CHUNK_LENGTH}
        RAIDLINE_CONTENT_CHUNK)
    string(APPEND RAIDLINE_PUBLISHED_CONTENT_LITERALS
        "R\"raidline_content(${RAIDLINE_CONTENT_CHUNK})raidline_content\"\n    ")
    math(EXPR RAIDLINE_CONTENT_OFFSET
        "${RAIDLINE_CONTENT_OFFSET} + ${RAIDLINE_CONTENT_CHUNK_LENGTH}")
endwhile()
configure_file(
    src/published_content_json.h.in
    generated/published_content_json.h
    @ONLY)

function(raidline_configure_library target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(
        ${target}
        PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")

    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

function(raidline_configure_executable target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

# A production implementation belongs to exactly one module. Tests link the
# production modules and never compile a second copy of business source.
function(raidline_assert_unique_compiled_sources)
    set(seen_sources)
    set(seen_owners)

    foreach(target IN LISTS ARGN)
        get_target_property(target_sources ${target} SOURCES)

        foreach(source IN LISTS target_sources)
            if(NOT source MATCHES "\\.(c|cc|cpp|cxx)$")
                continue()
            endif()

            get_filename_component(
                absolute_source
                "${source}"
                ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            file(
                RELATIVE_PATH
                source_key
                "${CMAKE_CURRENT_SOURCE_DIR}"
                "${absolute_source}")
            string(REPLACE "\\" "/" source_key "${source_key}")
            list(FIND seen_sources "${source_key}" existing_index)

            if(NOT existing_index EQUAL -1)
                list(GET seen_owners ${existing_index} existing_owner)
                message(
                    FATAL_ERROR
                    "Production source ${source} belongs to both "
                    "${existing_owner} and ${target}.")
            endif()

            list(APPEND seen_sources "${source_key}")
            list(APPEND seen_owners "${target}")
        endforeach()
    endforeach()

    file(
        GLOB absolute_business_sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

    set(all_business_sources)
    foreach(source IN LISTS absolute_business_sources)
        file(
            RELATIVE_PATH
            source_key
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${source}")
        string(REPLACE "\\" "/" source_key "${source_key}")
        list(APPEND all_business_sources "${source_key}")
    endforeach()

    list(
        REMOVE_ITEM
        all_business_sources
        "src/main.cpp")

    set(owned_sources ${seen_sources})
    list(SORT all_business_sources)
    list(SORT owned_sources)

    if(NOT all_business_sources STREQUAL owned_sources)
        set(missing_sources ${all_business_sources})
        set(unexpected_sources ${owned_sources})

        foreach(source IN LISTS owned_sources)
            list(REMOVE_ITEM missing_sources "${source}")
        endforeach()
        foreach(source IN LISTS all_business_sources)
            list(REMOVE_ITEM unexpected_sources "${source}")
        endforeach()

        message(
            FATAL_ERROR
            "Every non-main src/*.cpp must belong to exactly one production "
            "module. Missing: ${missing_sources}; unexpected: "
            "${unexpected_sources}.")
    endif()
endfunction()

# Domain and simulation stay buildable without SDL. Listing public headers on
# their owning targets lets this guard catch direct SDL includes before build.
function(raidline_assert_no_sdl_includes target)
    get_target_property(target_sources ${target} SOURCES)

    foreach(source IN LISTS target_sources)
        if(NOT source MATCHES "\\.(c|cc|cpp|cxx|h|hpp)$")
            continue()
        endif()

        get_filename_component(
            absolute_source
            "${source}"
            ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        file(READ "${absolute_source}" source_contents)
        string(
            REGEX MATCH
            "#[ \t]*include[ \t]*[<\"]SDL"
            forbidden_include
            "${source_contents}")

        if(forbidden_include)
            message(
                FATAL_ERROR
                "${target} must remain SDL-free, but ${source} includes SDL.")
        endif()
    endforeach()
endfunction()

# Stable modular-monolith production targets. Persistent Profile/Inventory/
# Economy live in domain, Base/Raid runtime in simulation, orchestration and
# persistence in services, and SDL input/rendering in the client.
add_library(raidline_domain STATIC
    src/alpha_content_ids.h
    src/base_resource_domain.cpp
    src/base_resource_domain.h
    src/base_service_domain.cpp
    src/base_service_domain.h
    src/combat_damage_domain.cpp
    src/combat_damage_domain.h
    src/content_registry.cpp
    src/content_registry.h
    src/definition_id.h
    src/health_system.h
    src/economy_domain.cpp
    src/economy_domain.h
    src/inventory_domain.cpp
    src/inventory_domain.h
    src/item_definition.cpp
    src/item_definition.h
    src/item_instance.cpp
    src/item_instance.h
    src/grid_inventory.cpp
    src/grid_inventory.h
    src/inventory_transfer.cpp
    src/inventory_transfer.h
    src/loot_table.cpp
    src/loot_table.h
    src/maintenance_domain.cpp
    src/maintenance_domain.h
    src/medical_domain.cpp
    src/medical_domain.h
    src/medical_types.h
    src/profile_state.cpp
    src/profile_state.h
    src/profile_combat_domain.cpp
    src/profile_combat_domain.h
    src/raid_lifecycle.cpp
    src/raid_lifecycle.h
    src/raid_action.cpp
    src/raid_action.h
    src/raid_session.cpp
    src/raid_session.h
    src/raid_settlement.cpp
    src/raid_settlement.h
    src/shot_resolution.cpp
    src/shot_resolution.h
    src/stash.cpp
    src/stash.h
    src/stable_random.h
    src/weapon_ammo_domain.cpp
    src/weapon_ammo_domain.h
    src/world_clock.cpp
    src/world_clock.h
    src/rect.h
    src/vec2.h
)
raidline_configure_library(raidline_domain)
target_include_directories(
    raidline_domain
    PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(
    raidline_domain
    PRIVATE
    nlohmann_json::nlohmann_json)
add_library(raidline::domain ALIAS raidline_domain)

add_library(raidline_simulation STATIC
    src/animation.cpp
    src/animation.h
    src/base_world.cpp
    src/base_world.h
    src/collision.cpp
    src/collision.h
    src/enemy.cpp
    src/enemy.h
    src/enemy_ai.cpp
    src/enemy_ai.h
    src/enemy_attack.cpp
    src/enemy_attack.h
    src/enemy_squad.cpp
    src/enemy_squad.h
    src/extraction_point.cpp
    src/extraction_point.h
    src/gameplay_input.h
    src/gameplay_world.cpp
    src/gameplay_world.h
    src/ground_item.cpp
    src/ground_item.h
    src/hit_resolution.cpp
    src/hit_resolution.h
    src/logical_ballistics.cpp
    src/logical_ballistics.h
    src/particle.cpp
    src/particle.h
    src/particle_system.cpp
    src/particle_system.h
    src/player.cpp
    src/player.h
    src/shot_feedback_presentation.cpp
    src/shot_feedback_presentation.h
    src/storage_cabinet.cpp
    src/storage_cabinet.h
    src/weapon_fire.cpp
    src/weapon_fire.h
    src/weapon_aim.cpp
    src/weapon_aim.h
    src/weapon_clear_gesture.cpp
    src/weapon_clear_gesture.h
)
raidline_configure_library(raidline_simulation)
target_link_libraries(raidline_simulation PUBLIC raidline::domain)
add_library(raidline::simulation ALIAS raidline_simulation)

add_library(raidline_services STATIC
    src/game_flow.cpp
    src/game_flow.h
    src/game_session.cpp
    src/game_session.h
    src/save_repository.cpp
    src/save_repository.h
)
raidline_configure_library(raidline_services)
target_link_libraries(
    raidline_services
    PUBLIC
    raidline::domain
    raidline::simulation
    PRIVATE
    nlohmann_json::nlohmann_json)
add_library(raidline::services ALIAS raidline_services)

add_library(raidline_sdl_client STATIC
    src/app.cpp
    src/app.h
    src/game_audio.cpp
    src/game_audio.h
    src/input_system.cpp
    src/input_system.h
    src/inventory_interaction.cpp
    src/inventory_interaction.h
    src/pause_menu.h
    src/profile_inventory_interaction.cpp
    src/profile_inventory_interaction.h
    src/raid_pointer_capture.h
    src/sound_event.cpp
    src/sound_event.h
    src/texture.cpp
    src/texture.h
    src/ui_localization.cpp
    src/ui_localization.h
    src/ui_text_renderer.cpp
    src/ui_text_renderer.h
)
raidline_configure_library(raidline_sdl_client)
target_link_libraries(
    raidline_sdl_client
    PUBLIC
    raidline::services
    SDL3::SDL3
    PRIVATE
    fmt::fmt
    nlohmann_json::nlohmann_json
    SDL3_image::SDL3_image)
if(WIN32)
    target_link_libraries(raidline_sdl_client PRIVATE gdi32)
endif()
add_library(raidline::sdl_client ALIAS raidline_sdl_client)

set(RAIDLINE_PRODUCTION_TARGETS
    raidline_domain
    raidline_simulation
    raidline_services
    raidline_sdl_client)

raidline_assert_unique_compiled_sources(${RAIDLINE_PRODUCTION_TARGETS})
raidline_assert_no_sdl_includes(raidline_domain)
raidline_assert_no_sdl_includes(raidline_simulation)

add_executable(Project_Raidline src/main.cpp)
raidline_configure_executable(Project_Raidline)
target_link_libraries(Project_Raidline PRIVATE raidline::sdl_client)

add_custom_command(
    TARGET Project_Raidline
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:Project_Raidline>/assets)

enable_testing()
include(GoogleTest)

function(raidline_add_test target source module)
    if(NOT ARGC EQUAL 3 OR NOT source MATCHES "^tests/.+\\.cpp$")
        message(
            FATAL_ERROR
            "raidline_add_test accepts one tests/*.cpp source and one "
            "production module; tests must not compile business source.")
    endif()

    add_executable(${target} ${source})
    raidline_configure_executable(${target})
    target_link_libraries(
        ${target}
        PRIVATE
        GTest::gtest_main
        ${module})
    gtest_discover_tests(${target})
endfunction()

raidline_add_test(HealthTest tests/test_health.cpp raidline::domain)
raidline_add_test(
    CombatDamageDomainTest
    tests/test_combat_damage_domain.cpp
    raidline::domain)
raidline_add_test(
    ProfileCombatDomainTest
    tests/test_profile_combat_domain.cpp
    raidline::domain)
raidline_add_test(
    MedicalDomainTest
    tests/test_medical_domain.cpp
    raidline::domain)
raidline_add_test(
    InputSystemTest
    tests/test_input_system.cpp
    raidline::sdl_client)
raidline_add_test(
    GameAudioTest
    tests/test_game_audio.cpp
    raidline::sdl_client)
raidline_add_test(
    UiLocalizationTest
    tests/test_ui_localization.cpp
    raidline::sdl_client)
target_compile_definitions(
    GameAudioTest
    PRIVATE
    RAIDLINE_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
raidline_add_test(PlayerTest tests/test_player.cpp raidline::simulation)
raidline_add_test(
    WeaponFireTest
    tests/test_weapon_fire.cpp
    raidline::simulation)
raidline_add_test(
    WeaponAimTest
    tests/test_weapon_aim.cpp
    raidline::simulation)
raidline_add_test(
    ShotResolutionTest
    tests/test_shot_resolution.cpp
    raidline::domain)
raidline_add_test(
    ShotFeedbackPresentationTest
    tests/test_shot_feedback_presentation.cpp
    raidline::simulation)
raidline_add_test(
    LogicalBallisticsTest
    tests/test_logical_ballistics.cpp
    raidline::simulation)
raidline_add_test(
    CollisionTest
    tests/test_collision.cpp
    raidline::simulation)
raidline_add_test(EnemyTest tests/test_enemy.cpp raidline::simulation)
raidline_add_test(
    EnemyAttackTest
    tests/test_enemy_attack.cpp
    raidline::simulation)
raidline_add_test(
    EnemyAiTest
    tests/test_enemy_ai.cpp
    raidline::simulation)
raidline_add_test(
    EnemySquadTest
    tests/test_enemy_squad.cpp
    raidline::simulation)
raidline_add_test(
    HitResolutionTest
    tests/test_hit_resolution.cpp
    raidline::simulation)
raidline_add_test(
    GameplayWorldTest
    tests/test_gameplay_world.cpp
    raidline::simulation)
raidline_add_test(
    StorageCabinetTest
    tests/test_storage_cabinet.cpp
    raidline::simulation)
raidline_add_test(
    LootTableTest
    tests/test_loot_table.cpp
    raidline::domain)
raidline_add_test(
    RaidSessionTest
    tests/test_raid_session.cpp
    raidline::domain)
raidline_add_test(
    RaidSettlementTest
    tests/test_raid_settlement.cpp
    raidline::domain)
raidline_add_test(
    GameSessionTest
    tests/test_game_session.cpp
    raidline::services)
raidline_add_test(
    AlphaExtractionSessionTest
    tests/test_alpha_extraction_session.cpp
    raidline::services)
raidline_add_test(
    AlphaHardeningTest
    tests/test_alpha_hardening.cpp
    raidline::services)
raidline_add_test(
    PersistentSessionTest
    tests/test_persistent_session.cpp
    raidline::services)
raidline_add_test(
    GameFlowTest
    tests/test_game_flow.cpp
    raidline::services)
raidline_add_test(
    ExtractionPointTest
    tests/test_extraction_point.cpp
    raidline::simulation)
raidline_add_test(TextureTest tests/test_texture.cpp raidline::sdl_client)
raidline_add_test(
    AnimationTest
    tests/test_animation.cpp
    raidline::simulation)
raidline_add_test(
    ParticleTest
    tests/test_particle.cpp
    raidline::simulation)
raidline_add_test(
    ParticleSystemTest
    tests/test_particle_system.cpp
    raidline::simulation)
raidline_add_test(
    ContentRegistryTest
    tests/test_content_registry.cpp
    raidline::domain)
raidline_add_test(
    ProfileStateTest
    tests/test_profile_state.cpp
    raidline::domain)
raidline_add_test(
    InventoryDomainTest
    tests/test_inventory_domain.cpp
    raidline::domain)
raidline_add_test(
    EconomyDomainTest
    tests/test_economy_domain.cpp
    raidline::domain)
raidline_add_test(
    BaseResourceDomainTest
    tests/test_base_resource_domain.cpp
    raidline::domain)
raidline_add_test(
    BaseServiceDomainTest
    tests/test_base_service_domain.cpp
    raidline::domain)
raidline_add_test(
    WorldClockTest
    tests/test_world_clock.cpp
    raidline::domain)
raidline_add_test(
    WeaponAmmoDomainTest
    tests/test_weapon_ammo_domain.cpp
    raidline::domain)
raidline_add_test(
    MaintenanceDomainTest
    tests/test_maintenance_domain.cpp
    raidline::domain)
raidline_add_test(
    RaidActionTest
    tests/test_raid_action.cpp
    raidline::domain)
raidline_add_test(
    RaidLifecycleTest
    tests/test_raid_lifecycle.cpp
    raidline::domain)
raidline_add_test(
    BaseWorldTest
    tests/test_base_world.cpp
    raidline::simulation)
raidline_add_test(
    WeaponClearGestureTest
    tests/test_weapon_clear_gesture.cpp
    raidline::simulation)
raidline_add_test(
    SaveRepositoryTest
    tests/test_save_repository.cpp
    raidline::services)
file(TO_CMAKE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    RAIDLINE_SOURCE_ASSET_DIR)
target_compile_definitions(
    ContentRegistryTest
    PRIVATE
    "RAIDLINE_SOURCE_ASSET_DIR=\"${RAIDLINE_SOURCE_ASSET_DIR}\"")
raidline_add_test(
    ItemDefinitionTest
    tests/test_item_definition.cpp
    raidline::domain)
raidline_add_test(
    ItemInstanceTest
    tests/test_item_instance.cpp
    raidline::domain)
raidline_add_test(
    GroundItemTest
    tests/test_ground_item.cpp
    raidline::simulation)
raidline_add_test(
    GridInventoryTest
    tests/test_grid_inventory.cpp
    raidline::domain)
raidline_add_test(
    InventoryTransferTest
    tests/test_inventory_transfer.cpp
    raidline::domain)
raidline_add_test(
    InventoryInteractionTest
    tests/test_inventory_interaction.cpp
    raidline::sdl_client)
raidline_add_test(
    MouseInventoryInteractionTest
    tests/test_mouse_inventory_interaction.cpp
    raidline::sdl_client)
raidline_add_test(
    ProfileInventoryInteractionTest
    tests/test_profile_inventory_interaction.cpp
    raidline::sdl_client)
