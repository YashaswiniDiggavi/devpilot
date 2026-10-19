#include "devpilot/llm.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
#include <thread>

using json = nlohmann::json;

static size_t write_cb(char* p, size_t size, size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, size * n);
    return size * n;
}

LlmReply llm_complete(const std::string& system, const std::string& user) {
    LlmReply r;
    const char* key = std::getenv("GEMINI_API_KEY");
    if (!key) { r.error = "GEMINI_API_KEY not set"; return r; }
    const char* model_env = std::getenv("DEVPILOT_MODEL");
    const std::string model = model_env ? model_env : "gemini-2.5-flash";
    const std::string url =
        "https://generativelanguage.googleapis.com/v1beta/models/" + model + ":generateContent";

    json body = {
        {"systemInstruction", {{"parts", json::array({ {{"text", system}} })}}},
        {"contents", json::array({
            {{"role", "user"}, {"parts", json::array({ {{"text", user}} })}}
        })}
    };
    const std::string payload = body.dump();

    for (int attempt = 0; attempt < 4; ++attempt) {
        if (attempt > 0)
            std::this_thread::sleep_for(std::chrono::seconds(5 << attempt));  // 10s, 20s, 40s
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
            continue;
        }        if (status != 200) { r.error = "HTTP " + std::to_string(status) + ": " + response; return r; }

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
            return r;
        } catch (const std::exception& e) {
            r.error = std::string("bad response: ") + e.what();
            return r;
        }
    }
    return r;
}