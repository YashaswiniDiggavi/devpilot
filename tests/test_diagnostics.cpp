#include <gtest/gtest.h>
#include "devpilot/diagnostics.hpp"

TEST(Diagnostics, ParsesGccError) {
    std::string log =
        "src/a.cpp:12:5: error: 'foo' was not declared in this scope\n"
        "src/a.cpp:3:1: note: declared here\n";
    auto d = parse_compiler_output(log);
    ASSERT_EQ(d.size(), 2u);
    EXPECT_EQ(d[0].file, "src/a.cpp");
    EXPECT_EQ(d[0].line, 12);
    EXPECT_EQ(d[0].severity, Severity::Error);
    EXPECT_EQ(d[1].severity, Severity::Note);
}
