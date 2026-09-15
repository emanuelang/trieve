cmake_minimum_required(VERSION 3.20)

project(semantic_fs_backend)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(unofficial-cld3 CONFIG REQUIRED)

if(TARGET libtesseract)
    set(TESSERACT_TARGET libtesseract)
elseif(TARGET Tesseract::libtesseract)
    set(TESSERACT_TARGET Tesseract::libtesseract)
elseif(TARGET tesseract::libtesseract)
    set(TESSERACT_TARGET tesseract::libtesseract)
else()
    message(FATAL_ERROR "Tesseract target not found")
endif()

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
    CURL::libcurl
    ${TESSERACT_TARGET}
    unofficial::cld3::cld3
)
