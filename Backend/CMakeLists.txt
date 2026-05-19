cmake_minimum_required(VERSION 3.20)

project(semantic_fs_backend)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB_RECURSE SOURCES
    src/*.cpp
)

add_executable(semantic_fs_backend
    ${SOURCES}
)

target_include_directories(semantic_fs_backend
    PRIVATE
    include
)

target_link_libraries(semantic_fs_backend
    PRIVATE
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
