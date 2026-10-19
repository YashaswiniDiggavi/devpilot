#include <gtest/gtest.h>
#include <fstream>
#include "devpilot/patch.hpp"
namespace fs = std::filesystem;

static fs::path make_repo() {
    fs::path d = fs::temp_directory_path() / "dp_patch_test";
    fs::remove_all(d);
    fs::create_directories(d);
    std::ofstream(d / "a.cpp") << "int x = 1;\n";
    return d;
}

TEST(Patch, AppliesUniqueEdit) {
    auto repo = make_repo();
    Backup b;
    auto r = apply_edits(repo, {{"a.cpp", "x = 1", "x = 2"}}, b);
    EXPECT_TRUE(r.ok);
    revert(b);
    std::ifstream in(repo / "a.cpp");
    std::string s; std::getline(in, s);
    EXPECT_EQ(s, "int x = 1;");
}

TEST(Patch, RejectsPathEscape) {
    auto repo = make_repo();
    Backup b;
    EXPECT_FALSE(apply_edits(repo, {{"../../etc/passwd", "a", "b"}}, b).ok);
}