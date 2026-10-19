#pragma once
#include <string>
#include <vector>

enum class Severity { Note, Warning, Error };

struct Diagnostic {
    std::string file;
    int line = 0;
    int col = 0;
    Severity severity = Severity::Error;
    std::string message;
};

std::vector<Diagnostic> parse_compiler_output(const std::string& output);