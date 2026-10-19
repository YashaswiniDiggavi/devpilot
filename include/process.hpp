#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

struct ProcessResult {
    int exit_code = -1;
    bool timed_out = false;
    std::string output;      // stdout + stderr merged
    double seconds = 0.0;
};

ProcessResult run_process(const std::vector<std::string>& argv,
                          const std::filesystem::path& cwd,
                          std::chrono::seconds timeout,
                          std::size_t max_output = 1 << 20);