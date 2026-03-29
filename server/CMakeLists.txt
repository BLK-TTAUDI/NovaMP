project(novaMP-master VERSION 1.0.0)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Crow CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

file(GLOB_RECURSE MASTER_SOURCES src/*.cpp)

add_executable(novaMP-master ${MASTER_SOURCES})

target_include_directories(novaMP-master PRIVATE src)

target_link_libraries(novaMP-master PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    unofficial::sqlite3::sqlite3
    OpenSSL::SSL
    OpenSSL::Crypto
    Crow::Crow
    tomlplusplus::tomlplusplus
)

if(WIN32)
    target_link_libraries(novaMP-master PRIVATE ws2_32)
endif()

install(TARGETS novaMP-master DESTINATION bin)
install(FILES config.toml schema.sql DESTINATION etc/novaMP-master)
