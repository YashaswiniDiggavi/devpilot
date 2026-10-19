#include <iostream>
#include "devpilot/diagnostics.hpp"
#include "devpilot/process.hpp"

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: devpilot <repo_path>\n"; return 2; }
    std::filesystem::path repo = argv[1];

    auto cfg = run_process({"cmake", "-S", ".", "-B", "build", "-G", "Ninja"},
                           repo, std::chrono::seconds(120));
    auto bld = run_process({"cmake", "--build", "build"},
                           repo, std::chrono::seconds(300));
    if (bld.exit_code == 0) { std::cout << "Build OK\n"; return 0; }

    auto diags = parse_compiler_output(cfg.output + bld.output);
    std::cout << "Build FAILED, " << diags.size() << " diagnostics\n";
    for (auto& d : diags)
        std::cout << d.file << ":" << d.line << " " << d.message << "\n";
    return 1;
}
