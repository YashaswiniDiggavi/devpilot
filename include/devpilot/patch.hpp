#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct Edit {
    std::string file;
    std::string old_text;
    std::string new_text;
};

struct PatchResult {
    bool ok = false;
    std::string error;
};

using Backup = std::map<std::filesystem::path, std::string>;

std::vector<Edit> parse_edits(const std::string& llm_text);

PatchResult apply_edits(const std::filesystem::path& repo,
                        const std::vector<Edit>& edits, Backup& backup);

void revert(const Backup& backup);
