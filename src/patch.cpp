#include "devpilot/patch.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<Edit> parse_edits(const std::string& text) {
    auto a = text.find('{');
    auto b = text.rfind('}');
    if (a == std::string::npos || b == std::string::npos || b < a)
        throw std::runtime_error("no JSON object in model output");
    json j = json::parse(text.substr(a, b - a + 1));

    std::vector<Edit> edits;
    for (auto& e : j.at("edits"))
        edits.push_back({e.at("file").get<std::string>(),
                         e.at("old").get<std::string>(),
                         e.at("new").get<std::string>()});
    return edits;
}

PatchResult apply_edits(const fs::path& repo, const std::vector<Edit>& edits, Backup& backup) {
    constexpr std::size_t kMaxEdits = 5, kMaxChars = 4000;
    if (edits.empty()) return {false, "no edits proposed"};
    if (edits.size() > kMaxEdits) return {false, "too many edits"};

    const fs::path root = fs::weakly_canonical(repo);
    for (const auto& e : edits) {
        if (e.old_text.size() + e.new_text.size() > kMaxChars)
            return {false, "edit too large: " + e.file};

        fs::path full = fs::weakly_canonical(root / e.file);
        fs::path rel = full.lexically_relative(root);
        if (rel.empty() || *rel.begin() == "..")
            return {false, "path escapes repo: " + e.file};
        const std::string first = rel.begin()->string();
        if (first == ".github" || first == "build" || first == ".git")
            return {false, "forbidden path: " + e.file};
        if (!fs::is_regular_file(full)) return {false, "file not found: " + e.file};

        std::string content = read_all(full);
        auto pos = content.find(e.old_text);
        if (e.old_text.empty() || pos == std::string::npos)
            return {false, "old text not found in " + e.file};
        if (content.find(e.old_text, pos + 1) != std::string::npos)
            return {false, "old text not unique in " + e.file};

        backup.emplace(full, content);
        content.replace(pos, e.old_text.size(), e.new_text);
        std::ofstream out(full, std::ios::binary | std::ios::trunc);
        out << content;
    }
    return {true, ""};
}

void revert(const Backup& backup) {
    for (const auto& [path, content] : backup) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << content;
    }
}