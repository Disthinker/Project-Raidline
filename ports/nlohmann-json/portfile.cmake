vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO nlohmann/json
    REF "v${VERSION}"
    SHA512 6cc1e86261f8fac21cc17a33da3b6b3c3cd5c116755651642af3c9e99bb3538fd42c1bd50397a77c8fb6821bc62d90e6b91bcdde77a78f58f2416c62fc53b97d
    HEAD_REF master
)

file(
    INSTALL "${SOURCE_PATH}/single_include/nlohmann"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(
    INSTALL "${CMAKE_CURRENT_LIST_DIR}/nlohmann_jsonConfig.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/nlohmann_json")
file(
    INSTALL "${SOURCE_PATH}/LICENSE.MIT"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright)
