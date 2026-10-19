#include "devpilot/diagnostics.hpp"
#include <regex>
#include <sstream>

std::vector<Diagnostic> parse_compiler_output(const std::string& output) {
    static const std::regex re(
        R"(^(.+?):(\d+):(\d+): (fatal error|error|warning|note): (.*)$)");
    std::vector<Diagnostic> out;
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch m;
        if (!std::regex_match(line, m, re)) continue;
        Diagnostic d;
        d.file = m[1];
        d.line = std::stoi(m[2]);
        d.col = std::stoi(m[3]);
        const std::string sev = m[4];
        d.severity = sev == "warning" ? Severity::Warning
                   : sev == "note"    ? Severity::Note
                                      : Severity::Error;
        d.message = m[5];
        out.push_back(std::move(d));
    }
    return out;
}
