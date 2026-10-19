#include <unistd.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include "devpilot/diagnostics.hpp"
#include "devpilot/llm.hpp"
#include "devpilot/patch.hpp"
#include "devpilot/process.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

static const char* kSystem =
    "You are DevPilot, an expert C++ build-fixing agent. You are given compiler "
    "diagnostics and the relevant source files. Propose the MINIMAL fix.\n"
    "Respond with ONLY a JSON object, no prose, in this exact shape:\n"
    "{\"explanation\": \"one sentence root cause\", "
    "\"edits\": [{\"file\": \"path relative to repo root\", "
    "\"old\": \"exact text to replace, must appear exactly once\", "
    "\"new\": \"replacement text\"}]}\n"
    "Never edit CI files. Never delete tests to make them pass.";

// Builds the project inside a locked-down Docker container.
// Set DEVPILOT_SANDBOX=0 to build directly on the host (debugging only).
static ProcessResult build(const fs::path& repo) {
    const char* flag = std::getenv("DEVPILOT_SANDBOX");
    if (flag && std::string(flag) == "0") {
        auto cfg = run_process({"cmake", "-S", ".", "-B", "build", "-G", "Ninja"}, repo, 120s);
        if (cfg.exit_code != 0) return cfg;
        return run_process({"cmake", "--build", "build"}, repo, 300s);
    }
    const std::string user = std::to_string(getuid()) + ":" + std::to_string(getgid());
    return run_process(
        {"docker", "run", "--rm",
         "--network", "none",               // no internet
         "--memory", "1g", "--cpus", "2",   // resource limits
         "--pids-limit", "256",             // stops fork bombs
         "--cap-drop", "ALL",               // no Linux capabilities
         "--security-opt", "no-new-privileges",
         "--user", user,                    // not root; files stay owned by you
         "-e", "HOME=/tmp",
         "-v", repo.string() + ":/work", "-w", "/work",
         "devpilot-sandbox",
         "timeout", "280", "bash", "-c",
         "rm -rf build && cmake -S . -B build -G Ninja && cmake --build build"},
        repo, 330s);
}

// Diagnostic paths may be container paths (/work/...), relative to build/, or relative to the repo.
static fs::path resolve(const fs::path& repo, std::string p) {
    if (p.rfind("/work/", 0) == 0) p = (repo / p.substr(6)).string();
    for (fs::path base : {repo / "build", repo}) {
        fs::path c = fs::weakly_canonical(fs::path(p).is_absolute() ? fs::path(p) : base / p);
        auto rel = c.lexically_relative(repo);
        if (!rel.empty() && *rel.begin() != ".." && fs::is_regular_file(c)) return c;
    }
    return {};
}

static std::string read_file(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: devpilot <repo_path>\n"; return 2; }
    const fs::path repo = fs::canonical(argv[1]);
    constexpr int kMaxIterations = 5;
    std::string history;
    int total_in = 0, total_out = 0;

    for (int it = 1; it <= kMaxIterations; ++it) {
        std::cout << "\n=== Iteration " << it << ": building ===\n";
        auto b = build(repo);
        if (b.exit_code == 0) {
            std::cout << "BUILD OK. tokens in/out: " << total_in << "/" << total_out << "\n";
            return 0;
        }

        auto diags = parse_compiler_output(b.output);
        std::set<fs::path> files;
        std::ostringstream prompt;
        prompt << "The build failed.\n\n## Diagnostics\n";
        int shown = 0;
        for (const auto& d : diags) {
            if (d.severity != Severity::Error || shown++ >= 10) continue;
            prompt << d.file << ":" << d.line << ": " << d.message << "\n";
            if (auto f = resolve(repo, d.file); !f.empty()) files.insert(f);
        }
        if (shown == 0) {  // not a compiler error (e.g. CMake or linker): show the log tail
            const auto& o = b.output;
            prompt << "(none parsed)\n\n## Raw log tail\n" << o.substr(o.size() > 3000 ? o.size() - 3000 : 0) << "\n";
            // No diagnostic named a file, so give the model the project's source files.
            for (const auto& e : fs::recursive_directory_iterator(repo)) {
                if (!e.is_regular_file()) continue;
                auto rel = fs::relative(e.path(), repo);
                const std::string top = rel.begin()->string();
                if (top == "build" || top == ".git") continue;
                const std::string ext = e.path().extension().string();
                const bool wanted = ext == ".cpp" || ext == ".cc" || ext == ".hpp" || ext == ".h" || ext == ".txt";
                if (wanted && files.size() < 8 && fs::file_size(e.path()) < 20000)
                    files.insert(e.path());
            }
        }
        prompt << "\n## Source files\n";
        for (const auto& f : files)
            prompt << "--- " << fs::relative(f, repo).string() << " ---\n" << read_file(f) << "\n";
        if (!history.empty()) prompt << "\n## Earlier attempts\n" << history;

        std::cout << "Build failed (" << shown << " errors). Asking the model...\n";
        auto reply = llm_complete(kSystem, prompt.str());
        if (!reply.ok) { std::cerr << "LLM error: " << reply.error << "\n"; return 3; }
        total_in += reply.input_tokens;
        total_out += reply.output_tokens;

        std::vector<Edit> edits;
        try {
            edits = parse_edits(reply.text);
        } catch (const std::exception& e) {
            history += "Attempt " + std::to_string(it) + ": your reply was not valid JSON (" + e.what() + ").\n";
            continue;
        }

        Backup backup;
        auto pr = apply_edits(repo, edits, backup);
        if (!pr.ok) {
            revert(backup);
            std::cout << "Patch rejected: " << pr.error << "\n";
            history += "Attempt " + std::to_string(it) + ": patch rejected: " + pr.error + "\n";
            continue;
        }
        std::cout << "Applied " << edits.size() << " edit(s).\n";
        history += "Attempt " + std::to_string(it) + ": applied edits to " + edits[0].file +
                   ", but the build may still be failing; see the updated files above.\n";
    }
    std::cout << "\nGAVE UP after " << kMaxIterations << " iterations.\n";
    return 1;
}