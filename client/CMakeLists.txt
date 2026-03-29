project(novaMP-client VERSION 1.0.0)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog        CONFIG REQUIRED)
find_package(asio          CONFIG REQUIRED)
find_package(OpenSSL              REQUIRED)
find_package(ZLIB                 REQUIRED)
find_package(tomlplusplus  CONFIG REQUIRED)

file(GLOB_RECURSE CLIENT_SOURCES src/*.cpp)
add_executable(novaMP-client ${CLIENT_SOURCES})
target_include_directories(novaMP-client PRIVATE src)
target_link_libraries(novaMP-client PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    asio::asio
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    tomlplusplus::tomlplusplus
)

if(WIN32)
    target_link_libraries(novaMP-client PRIVATE ws2_32 mswsock)
    # Embed a version/description resource so Windows shows it in Properties
    set(RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/novaMP_client.rc")
    file(WRITE "${RC_FILE}"
        "1 VERSIONINFO\n"
        "FILEVERSION 1,0,0,0\n"
        "PRODUCTVERSION 1,0,0,0\n"
        "BEGIN\n"
        "  BLOCK \"StringFileInfo\"\n"
        "  BEGIN\n"
        "    BLOCK \"040904b0\"\n"
        "    BEGIN\n"
        "      VALUE \"FileDescription\",  \"NovaMP Client\\0\"\n"
        "      VALUE \"FileVersion\",      \"1.0.0\\0\"\n"
        "      VALUE \"InternalName\",     \"novaMP-client\\0\"\n"
        "      VALUE \"ProductName\",      \"NovaMP\\0\"\n"
        "      VALUE \"ProductVersion\",   \"1.0.0\\0\"\n"
        "      VALUE \"LegalCopyright\",   \"AGPL-3.0\\0\"\n"
        "    END\n"
        "  END\n"
        "  BLOCK \"VarFileInfo\"\n"
        "  BEGIN\n"
        "    VALUE \"Translation\", 0x409, 1200\n"
        "  END\n"
        "END\n"
    )
    target_sources(novaMP-client PRIVATE "${RC_FILE}")
endif()

# ── Post-build: copy runtime resources ───────────────────────────────────────
add_custom_command(TARGET novaMP-client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources"
        "$<TARGET_FILE_DIR:novaMP-client>/Resources")

# ── Install rules (used by CPack) ─────────────────────────────────────────────
install(TARGETS   novaMP-client          DESTINATION bin)
install(DIRECTORY Resources              DESTINATION share/novaMP-client)
install(FILES     "${CMAKE_CURRENT_SOURCE_DIR}/../README.md"
                  "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
        DESTINATION .)

# ── CPack — Windows NSIS installer ───────────────────────────────────────────
if(WIN32)
    set(CPACK_GENERATOR "NSIS")

    set(CPACK_PACKAGE_NAME             "NovaMP")
    set(CPACK_PACKAGE_VENDOR           "NovaMP Project")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-source multiplayer client for BeamNG.drive")
    set(CPACK_PACKAGE_VERSION          "1.0.0")
    set(CPACK_PACKAGE_VERSION_MAJOR    "1")
    set(CPACK_PACKAGE_VERSION_MINOR    "0")
    set(CPACK_PACKAGE_VERSION_PATCH    "0")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "NovaMP")
    set(CPACK_PACKAGE_EXECUTABLES      "novaMP-client" "NovaMP Client")

    # Output filename: novaMP-client-win64-setup.exe
    set(CPACK_PACKAGE_FILE_NAME "novaMP-client-win64-setup")

    set(CPACK_NSIS_DISPLAY_NAME        "NovaMP Client")
    set(CPACK_NSIS_PACKAGE_NAME        "NovaMP")
    set(CPACK_NSIS_CONTACT             "https://github.com/BLK-TTAUDI/NovaMP")
    set(CPACK_NSIS_URL_INFO_ABOUT      "https://novaMP.gg")
    set(CPACK_NSIS_HELP_LINK           "https://github.com/BLK-TTAUDI/NovaMP/wiki")

    # Create desktop shortcut
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$DESKTOP\\\\NovaMP Client.lnk' '$INSTDIR\\\\bin\\\\novaMP-client.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$DESKTOP\\\\NovaMP Client.lnk'")

    # Add/remove Programs entry
    set(CPACK_NSIS_MODIFY_PATH ON)

    # Include MSVC runtime redistributable if building with MSVC
    if(MSVC)
        include(InstallRequiredSystemLibraries)
    endif()

# ── CPack — Linux DEB + TGZ ───────────────────────────────────────────────────
else()
    set(CPACK_GENERATOR "TGZ;DEB")

    set(CPACK_PACKAGE_NAME             "novaMP-client")
    set(CPACK_PACKAGE_VENDOR           "NovaMP Project")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-source multiplayer client for BeamNG.drive")
    set(CPACK_PACKAGE_VERSION          "1.0.0")
    set(CPACK_PACKAGE_VERSION_MAJOR    "1")
    set(CPACK_PACKAGE_VERSION_MINOR    "0")
    set(CPACK_PACKAGE_VERSION_PATCH    "0")
    set(CPACK_PACKAGE_CONTACT          "https://github.com/BLK-TTAUDI/NovaMP")

    # TGZ output: novaMP-client-linux-amd64.tar.gz
    set(CPACK_PACKAGE_FILE_NAME        "novaMP-client-linux-amd64")

    # DEB-specific
    set(CPACK_DEBIAN_PACKAGE_NAME      "novaMP-client")
    set(CPACK_DEBIAN_PACKAGE_SECTION   "games")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS   "libssl3, libstdc++6, libgcc-s1")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE  "https://novaMP.gg")
    set(CPACK_DEBIAN_FILE_NAME         "novaMP-client-linux-amd64.deb")
endif()

include(CPack)
