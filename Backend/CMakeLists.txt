cmake_minimum_required(VERSION 3.20)

project(semantic_fs_backend)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

if(TARGET libtesseract)
    set(TESSERACT_TARGET libtesseract)
elseif(TARGET Tesseract::libtesseract)
    set(TESSERACT_TARGET Tesseract::libtesseract)
elseif(TARGET tesseract::libtesseract)
    set(TESSERACT_TARGET tesseract::libtesseract)
else()
    message(FATAL_ERROR "Tesseract target not found")
endif()

set(SEMANTIC_FS_CORE_SOURCES
    src/chunking/chunker.cpp
    src/embeddings/embedding.cpp
    src/extractors/extractor_factory.cpp
    src/extractors/image_extractor.cpp
    src/extractors/pdf_extractor.cpp
    src/extractors/tesseract_ocr_extractor.cpp
    src/extractors/text_extractor.cpp
    src/processing/text_cleaner.cpp
    src/scanner/scanner.cpp
    src/scanner/path_policy.cpp
    src/monitoring/safe_startup_coordinator.cpp
    src/monitoring/outbox_publisher.cpp
    src/monitoring/durable_startup_coverage_sink.cpp
    src/monitoring/event_coalescer.cpp
    src/monitoring/file_stability_probe.cpp
    src/monitoring/reconciliation_service.cpp
    src/monitoring/file_monitor_service.cpp
    src/search/semantic_search.cpp
    src/storage/chunks.cpp
    src/storage/database.cpp
    src/storage/files.cpp
    src/storage/vectors.cpp
    src/storage/sqlite_raii.cpp
    src/storage/sqlite_migrations.cpp
    src/storage/sqlite_catalog_outbox.cpp
)

set(SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE OFF)
if(WIN32)
    set(SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE ON)
    list(APPEND SEMANTIC_FS_CORE_SOURCES
        src/scanner/windows_path_semantics.cpp
        src/scanner/windows_file_system_view.cpp
        src/monitoring/windows_file_watcher.cpp
        src/monitoring/file_watcher_factory.cpp
    )
endif()

add_library(semantic_fs_core STATIC
    ${SEMANTIC_FS_CORE_SOURCES}
)

target_include_directories(semantic_fs_core
    PUBLIC
    include
)
target_compile_definitions(semantic_fs_core PUBLIC SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE=$<BOOL:${SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE}>)

target_link_libraries(semantic_fs_core
    PUBLIC
    ${TESSERACT_TARGET}
    unofficial::sqlite3::sqlite3
)

add_executable(semantic_fs_backend
    src/app/main.cpp
)

target_link_libraries(semantic_fs_backend
    PRIVATE
    semantic_fs_core
    fmt::fmt
    spdlog::spdlog
)

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)

    add_executable(semantic_fs_core_tests
        tests/extractors/image_extractor_smoke_test.cpp
        tests/monitoring/contracts_compile_test.cpp
        tests/monitoring/phase4_contracts_test.cpp
        tests/monitoring/phase4_coalescing_stability_test.cpp
        tests/monitoring/phase4_sqlite_migrations_test.cpp
        tests/monitoring/phase4_atomic_transitions_test.cpp
        tests/monitoring/phase4_reconciliation_storage_test.cpp
        tests/monitoring/phase4_reconciliation_service_test.cpp
        tests/monitoring/watcher_contracts_test.cpp
        tests/monitoring/safe_startup_coordinator_test.cpp
        tests/monitoring/path_policy_test.cpp
        tests/monitoring/initial_scanner_test.cpp
        tests/monitoring/sqlite_catalog_outbox_test.cpp
        tests/monitoring/phase4_publisher_status_quota_test.cpp
        tests/monitoring/phase4_file_monitor_lifecycle_test.cpp
        tests/monitoring/outbox_publisher_test.cpp
        tests/monitoring/durable_startup_coverage_sink_test.cpp
    )
    if(WIN32)
        target_sources(semantic_fs_core_tests PRIVATE
            tests/monitoring/native_file_system_view_test.cpp
            tests/monitoring/windows_file_watcher_test.cpp
        )
    endif()

    target_link_libraries(semantic_fs_core_tests
        PRIVATE
        semantic_fs_core
        Catch2::Catch2WithMain
    )

    catch_discover_tests(semantic_fs_core_tests)
endif()
