#include "devpilot/llm.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>
#include <thread>

using json = nlohmann::json;

static size_t write_cb(char* p, size_t size, size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, size * n);
    return size * n;
}

// Small, stable, non-cryptographic hash (FNV-1a) used to name cache files.
static std::string fnv1a_hex(const std::string& s) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", h);
    return buf;
}

static std::filesystem::path cache_path(const std::string& key_material) {
    const char* dir = std::getenv("DEVPILOT_CACHE_DIR");
    const char* home = std::getenv("HOME");
    std::filesystem::path base =
        dir ? std::filesystem::path(dir)
            : std::filesystem::path(home ? home : ".") / ".cache" / "devpilot";
    return base / (fnv1a_hex(key_material) + ".json");
}

LlmReply llm_complete(const std::string& system, const std::string& user) {
    LlmReply r;
    const char* key = std::getenv("GEMINI_API_KEY");
    const char* model_env = std::getenv("DEVPILOT_MODEL");
    const std::string model = model_env ? model_env : "gemini-3.5-flash-lite";
    const std::string url =
        "https://generativelanguage.googleapis.com/v1beta/models/" + model + ":generateContent";

    json body = {
        {"systemInstruction", {{"parts", json::array({ {{"text", system}} })}}},
        {"contents", json::array({
            {{"role", "user"}, {"parts", json::array({ {{"text", user}} })}}
        })}
    };
    const std::string payload = body.dump();

    // --- On-disk response cache: identical request => no API call ---
    const bool use_cache = std::getenv("DEVPILOT_NO_CACHE") == nullptr;
    const auto cpath = cache_path(model + "\n" + payload);
    if (use_cache) {
        std::ifstream in(cpath);
        if (in) {
            try {
                json c = json::parse(in);
                r.text = c.at("text").get<std::string>();
                r.input_tokens = c.value("in", 0);
                r.output_tokens = c.value("out", 0);
                r.ok = true;
                std::cerr << "(cache hit)\n";
                return r;
            } catch (...) {
                // corrupt cache file: fall through to a real call
            }
        }
    }

    // A key is only required when we actually need to call the API.
    if (!key) { r.error = "GEMINI_API_KEY not set"; return r; }

    for (int attempt = 0; attempt < 4; ++attempt) {
        if (attempt > 0)  // exponential backoff: 10s, 20s, 40s
            std::this_thread::sleep_for(std::chrono::seconds(5 << attempt));

        CURL* curl = curl_easy_init();
        if (!curl) { r.error = "curl init failed"; return r; }

        std::string response;
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "content-type: application/json");
        headers = curl_slist_append(headers, (std::string("x-goog-api-key: ") + key).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

        CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK) { r.error = curl_easy_strerror(rc); continue; }
        if (status == 429 || status >= 500) {
            r.error = "HTTP " + std::to_string(status) + ": " + response.substr(0, 300);
            // A daily quota will not recover by waiting a few seconds: stop retrying.
            if (response.find("PerDay") != std::string::npos) return r;
            continue;
        }
        if (status != 200) { r.error = "HTTP " + std::to_string(status) + ": " + response; return r; }

        try {
            json j = json::parse(response);
            for (auto& part : j.at("candidates").at(0).at("content").at("parts"))
                if (part.contains("text")) r.text += part["text"].get<std::string>();
            if (j.contains("usageMetadata")) {
                r.input_tokens = j["usageMetadata"].value("promptTokenCount", 0);
                r.output_tokens = j["usageMetadata"].value("candidatesTokenCount", 0);
            }
            if (r.text.empty()) { r.error = "empty reply: " + response.substr(0, 300); return r; }
            r.ok = true;

            if (use_cache) {  // save successful replies
                std::error_code ec;
                std::filesystem::create_directories(cpath.parent_path(), ec);
                std::ofstream out(cpath);
                out << json{{"text", r.text}, {"in", r.input_tokens}, {"out", r.output_tokens}}.dump();
            }
            return r;
        } catch (const std::exception& e) {
            r.error = std::string("bad response: ") + e.what();
            return r;
        }
    }
    return r;  // all retries failed; r.error says why
}