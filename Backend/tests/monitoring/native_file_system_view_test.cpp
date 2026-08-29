#include "semantic_fs/monitoring/i_file_system_view.h"
#include "semantic_fs/monitoring/i_path_semantics.h"
#include <catch2/catch_test_macros.hpp>
#include <windows.h>
using namespace semantic_fs::monitoring;
TEST_CASE("native_file_system_view: Windows adapters expose ordinal path semantics and filesystem listings") {
    REQUIRE(SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE == 1);
    auto paths = makeWindowsPathSemantics();
    REQUIRE(paths->compareComponent("\xC3\x84", "\xC3\xA4") == 0);
    REQUIRE(paths->relativeTo({"C:/scan"}, {"C:\\scan\\child"})->utf8 == "child");
    char directory[MAX_PATH];
    REQUIRE(GetTempPathA(MAX_PATH, directory));
    auto view = makeWindowsFileSystemView();
    REQUIRE(std::holds_alternative<std::vector<FileSystemEntry>>(view->list({directory})));
}