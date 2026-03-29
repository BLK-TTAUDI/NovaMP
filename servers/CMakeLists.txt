project(novaMP-server VERSION 1.0.0)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog        CONFIG REQUIRED)
find_package(asio          CONFIG REQUIRED)
find_package(OpenSSL              REQUIRED)
find_package(ZLIB                 REQUIRED)
find_package(tomlplusplus  CONFIG REQUIRED)
find_package(lua           CONFIG REQUIRED)

file(GLOB_RECURSE SERVER_SOURCES src/*.cpp)
add_executable(novaMP-server ${SERVER_SOURCES})
target_include_directories(novaMP-server PRIVATE src)
target_link_libraries(novaMP-server PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    asio::asio
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    tomlplusplus::tomlplusplus
    lua::lua
)

if(WIN32)
    target_link_libraries(novaMP-server PRIVATE ws2_32 mswsock)
endif()

# ── Post-build: copy runtime resources ───────────────────────────────────────
add_custom_command(TARGET novaMP-server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources"
        "$<TARGET_FILE_DIR:novaMP-server>/Resources")

# ── Install rules ─────────────────────────────────────────────────────────────
install(TARGETS    novaMP-server   DESTINATION bin)
install(DIRECTORY  Resources       DESTINATION share/novaMP-server)
install(FILES      ServerConfig.toml start.bat start.sh
        DESTINATION bin
        OPTIONAL)
install(FILES      "${CMAKE_CURRENT_SOURCE_DIR}/../README.md"
                   "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
        DESTINATION .)

# ── CPack ─────────────────────────────────────────────────────────────────────
if(WIN32)
    set(CPACK_GENERATOR            "ZIP")
    set(CPACK_PACKAGE_FILE_NAME    "novaMP-server-win64")
else()
    set(CPACK_GENERATOR            "TGZ;DEB")
    set(CPACK_PACKAGE_FILE_NAME    "novaMP-server-linux-amd64")
    set(CPACK_DEBIAN_FILE_NAME     "novaMP-server-linux-amd64.deb")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libssl3, libstdc++6, liblua5.3-0")
endif()

set(CPACK_PACKAGE_NAME             "novaMP-server")
set(CPACK_PACKAGE_VENDOR           "NovaMP Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NovaMP dedicated game server for BeamNG.drive")
set(CPACK_PACKAGE_VERSION          "1.0.0")
set(CPACK_PACKAGE_CONTACT          "https://github.com/BLK-TTAUDI/NovaMP")

include(CPack)
