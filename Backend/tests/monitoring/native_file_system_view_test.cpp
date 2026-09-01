#include "semantic_fs/monitoring/i_file_system_view.h"
#include "semantic_fs/monitoring/i_path_semantics.h"
#include <catch2/catch_test_macros.hpp>
#include <windows.h>
using namespace semantic_fs::monitoring;
TEST_CASE("monitoring native_file_system_view: Windows adapters expose ordinal and legacy-long path semantics") {
    REQUIRE(SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE == 1);
    auto paths = makeWindowsPathSemantics();
    REQUIRE(paths->compareComponent("\xC3\x84", "\xC3\xA4") == 0);
    REQUIRE(paths->relativeTo({"C:/scan"}, {"C:\\scan\\child"})->utf8 == "child");
    const std::string longComponent(270, 'a');
    const AbsolutePath longRoot{"\\\\?\\C:\\scan"};
    const AbsolutePath longCandidate{longRoot.utf8 + "\\" + longComponent + "\\file.txt"};
    const auto longRelative = paths->relativeTo(longRoot, longCandidate);
    REQUIRE(longRelative);
    REQUIRE(longRelative->utf8 == longComponent + "/file.txt");
    char directory[MAX_PATH];
    REQUIRE(GetTempPathA(MAX_PATH, directory));
    auto view = makeWindowsFileSystemView();
    REQUIRE(std::holds_alternative<std::vector<FileSystemEntry>>(view->list({directory})));
}
