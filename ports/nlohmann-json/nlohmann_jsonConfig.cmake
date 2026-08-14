if(NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    get_filename_component(
        _nlohmann_json_prefix
        "${CMAKE_CURRENT_LIST_DIR}/../.."
        ABSOLUTE)
    set_target_properties(
        nlohmann_json::nlohmann_json
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
        "${_nlohmann_json_prefix}/include")
    unset(_nlohmann_json_prefix)
endif()
