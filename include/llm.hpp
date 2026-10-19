#pragma once
#include <string>

struct LlmReply {
    bool ok = false;
    std::string text;
    std::string error;
    int input_tokens = 0;
    int output_tokens = 0;
};

LlmReply llm_complete(const std::string& system, const std::string& user);