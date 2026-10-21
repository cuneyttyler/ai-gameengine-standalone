#include "gameplay/Localization.h"

#include <algorithm>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

nlohmann::json ReadJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return nlohmann::json();
    return nlohmann::json::parse(in, nullptr, false);
}

} // namespace

std::string FillPlaceholders(const std::string& text, const std::map<std::string, std::string>& values) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            const size_t close = text.find('}', i);
            if (close != std::string::npos) {
                auto it = values.find(text.substr(i + 1, close - i - 1));
                if (it != values.end()) {
                    out += it->second;
                    i = close;
                    continue;
                }
            }
        }
        out += text[i];
    }
    return out;
}

void Localization::Load(const fs::path& projectRoot, const std::string& language) {
    language_ = language;
    available_.clear();
    table_.clear();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(projectRoot / "loc", ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") available_.push_back(entry.path().stem().string());
    }
    std::sort(available_.begin(), available_.end());
    if (language.empty()) return;
    const nlohmann::json table = ReadJson(projectRoot / "loc" / (language + ".json"));
    if (!table.is_object()) return;
    for (const auto& [key, value] : table.items()) {
        if (value.is_string() && !value.get<std::string>().empty()) table_[key] = value.get<std::string>();
    }
}

std::string Localization::Text(const std::string& key, const std::string& fallback) const {
    auto it = table_.find(key);
    return it == table_.end() ? fallback : it->second;
}

std::string Localization::Format(const std::string& key, const std::string& fallback,
                                 const std::map<std::string, std::string>& values) const {
    return FillPlaceholders(Text(key, fallback), values);
}

std::map<std::string, std::string> Localization::CollectSource(const fs::path& projectRoot) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(projectRoot / "dialogue", ec)) {
        if (entry.path().extension() != ".json") continue;
        const std::string id = entry.path().stem().string();
        const nlohmann::json dialogue = ReadJson(entry.path());
        for (const auto& node : dialogue.value("nodes", nlohmann::json::array())) {
            if (!node.is_object()) continue;
            const std::string nodeId = node.value("id", "");
            if (!node.value("text", "").empty()) out["dialogue/" + id + "/" + nodeId] = node.value("text", "");
            int n = 0;
            for (const auto& choice : node.value("choices", nlohmann::json::array())) {
                ++n;
                if (choice.is_object() && !choice.value("text", "").empty()) {
                    out["dialogue/" + id + "/" + nodeId + "/" + std::to_string(n)] = choice.value("text", "");
                }
            }
        }
    }
    for (const auto& entry : fs::directory_iterator(projectRoot / "quests", ec)) {
        if (entry.path().extension() != ".json") continue;
        const std::string id = entry.path().stem().string();
        const nlohmann::json quest = ReadJson(entry.path());
        if (!quest.is_object()) continue;
        if (!quest.value("name", "").empty()) out["quest/" + id + "/name"] = quest.value("name", "");
        if (!quest.value("description", "").empty()) out["quest/" + id + "/description"] = quest.value("description", "");
        for (const auto& objective : quest.value("objectives", nlohmann::json::array())) {
            if (objective.is_object() && !objective.value("text", "").empty()) {
                out["quest/" + id + "/" + objective.value("id", "")] = objective.value("text", "");
            }
        }
    }
    // Records: their text fields named like player-facing text.
    for (const auto& typeDir : fs::directory_iterator(projectRoot / "records", ec)) {
        if (!typeDir.is_directory()) continue;
        for (const auto& file : fs::directory_iterator(typeDir.path(), ec)) {
            if (file.path().extension() != ".json") continue;
            const nlohmann::json record = ReadJson(file.path());
            if (!record.is_object()) continue;
            for (const char* field : {"name", "description", "title", "text"}) {
                if (record.contains(field) && record[field].is_string() && !record[field].get<std::string>().empty()) {
                    out["record/" + typeDir.path().filename().string() + "/" + file.path().stem().string() + "/" + field] =
                        record[field].get<std::string>();
                }
            }
        }
    }
    return out;
}

Localization& ActiveLocalization() {
    static Localization localization;
    return localization;
}

} // namespace aige
