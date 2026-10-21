#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

// Rules the code has broken before and must not again, checked in the sources.
namespace fs = std::filesystem;

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

// nlohmann's value() returns a copy; items() over that copy refers to it, and
// the copy is gone before the loop body runs (a crash or garbage, six times so
// far). Hold the value in a named local first.
TEST(SourceRulesTest, NoItemsOverATemporaryJsonValue) {
    const std::regex bad(R"(\.value\([^;{]*\)\s*\.items\(\))");
    std::string found;
    for (const char* dir : {"engine", "apps/editor"}) {
        for (const auto& entry : fs::recursive_directory_iterator(fs::path(AIGE_SOURCE_DIR) / dir)) {
            const auto ext = entry.path().extension();
            if (ext != ".cpp" && ext != ".h") continue;
            if (entry.path().generic_string().find("/ui/") != std::string::npos) continue;
            std::ifstream in(entry.path());
            std::string line;
            for (int n = 1; std::getline(in, line); ++n) {
                if (std::regex_search(line, bad)) found += "  " + entry.path().generic_string() + ":" + std::to_string(n) + "\n";
            }
        }
    }
    EXPECT_TRUE(found.empty()) << "items() over a temporary from value():\n" << found;
}

// The same for a range-for over a call's member: `for (x : Load()["folders"])` or
// `for (x : build.State()["items"])` loops over a temporary's element (the library and its
// test, 2026-10-21). Name the result first.
TEST(SourceRulesTest, NoLoopOverATemporarysElement) {
    // Calls that return a copy (the accessors returning a reference, Solved() and Spec(), are fine).
    const std::regex bad(R"(for\s*\([^:;]*:\s*[\w.>-]*\b(State|Load|Snapshot|Card)\(\)\s*\[)");
    std::string found;
    for (const char* dir : {"engine", "apps/editor", "tests"}) {
        for (const auto& entry : fs::recursive_directory_iterator(fs::path(AIGE_SOURCE_DIR) / dir)) {
            const auto ext = entry.path().extension();
            if (ext != ".cpp" && ext != ".h") continue;
            if (entry.path().generic_string().find("/ui/") != std::string::npos) continue;
            std::ifstream in(entry.path());
            std::string line;
            for (int n = 1; std::getline(in, line); ++n) {
                const size_t start = line.find_first_not_of(' ');
                if (start != std::string::npos && line.compare(start, 2, "//") == 0) continue; // a comment
                if (std::regex_search(line, bad)) found += "  " + entry.path().generic_string() + ":" + std::to_string(n) + "\n";
            }
        }
    }
    EXPECT_TRUE(found.empty()) << "a range-for over a temporary's element:\n" << found;
}
