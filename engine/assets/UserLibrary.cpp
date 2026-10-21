#include "assets/UserLibrary.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string HashOf(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    uint64_t h = 1469598103934665603ull;
    char buffer[65536];
    while (in) {
        in.read(buffer, sizeof(buffer));
        for (std::streamsize i = 0; i < in.gcount(); ++i) h = (h ^ static_cast<unsigned char>(buffer[i])) * 1099511628211ull;
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(h));
    return text;
}

long long Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// A file name that is safe on every system: letters, digits, dashes, dots.
std::string SafeName(const std::string& name) {
    std::string out;
    for (const char c : name) out += std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' ? c : '-';
    if (out.empty() || out[0] == '.') out = "asset" + out;
    return out.substr(0, 80);
}

std::map<std::string, std::string> ReadMeta(const fs::path& file) {
    std::map<std::string, std::string> out;
    std::ifstream in(file.string() + ".meta");
    for (std::string line; std::getline(in, line);) {
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        out[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

void CopyBeside(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    for (const char* extra : {".meta", ".gen.json"}) {
        if (fs::exists(from.string() + extra, ec)) fs::copy_file(from.string() + extra, to.string() + extra, fs::copy_options::overwrite_existing, ec);
    }
}

json* Find(json& list, const std::string& id) {
    for (auto& e : list) {
        if (e.value("id", "") == id) return &e;
    }
    return nullptr;
}

} // namespace

UserLibrary::UserLibrary(fs::path dir) : dir_(std::move(dir)) {}

json UserLibrary::Load() const {
    std::ifstream in(dir_ / "library.json");
    json j = in ? json::parse(in, nullptr, false) : json();
    if (!j.is_object() || j.value("schema", "") != "aige.user-library/1") {
        j = {{"schema", "aige.user-library/1"}, {"folders", json::array()}, {"items", json::array()}, {"sharing", "unset"}};
    }
    for (const char* key : {"folders", "items"}) {
        if (!j.contains(key) || !j[key].is_array()) j[key] = json::array();
    }
    if (!j.contains("sharing")) j["sharing"] = "unset";
    return j;
}

void UserLibrary::Save(const json& library) const {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    const fs::path tmp = dir_ / "library.json.tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << library.dump(2) << "\n";
    }
    fs::rename(tmp, dir_ / "library.json", ec);
    if (ec) fs::copy_file(tmp, dir_ / "library.json", fs::copy_options::overwrite_existing, ec);
}

json UserLibrary::State() const {
    json j = Load();
    bool generated = false;
    for (auto& item : j["items"]) {
        item["path"] = "files/" + item.value("file", "");
        generated = generated || item.value("source", "") == "generated";
    }
    j["askSharing"] = generated && j.value("sharing", "unset") == "unset";
    j["dir"] = dir_.string();
    return j;
}

std::string UserLibrary::Add(const fs::path& file, const json& info, const std::string& folder) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return {};
    const std::string hash = HashOf(file);
    if (hash.empty()) return {};
    json library = Load();
    for (const auto& item : library["items"]) {
        if (item.value("hash", "") == hash) return item.value("id", ""); // already kept
    }
    fs::create_directories(dir_ / "files", ec);
    const std::string name = hash.substr(0, 8) + "-" + SafeName(file.filename().string());
    const fs::path target = dir_ / "files" / name;
    fs::copy_file(file, target, fs::copy_options::overwrite_existing, ec);
    if (ec) return {};
    CopyBeside(file, target);
    const auto meta = ReadMeta(file);
    json item = {{"id", hash},
                 {"name", info.value("name", meta.contains("name") ? meta.at("name") : file.stem().string())},
                 {"file", name},
                 {"folder", folder},
                 {"source", info.value("source", meta.contains("source") ? meta.at("source") : std::string("project"))},
                 {"licence", info.value("licence", meta.contains("licence") ? meta.at("licence") : std::string())},
                 {"author", info.value("author", meta.contains("author") ? meta.at("author") : std::string())},
                 {"url", info.value("url", meta.contains("url") ? meta.at("url") : std::string())},
                 {"origin", info.value("origin", "")},
                 {"created", Now()},
                 {"hash", hash},
                 {"meta", meta}};
    // Generated art: shared as the user chose for it (asked when they haven't chosen).
    const std::string sharing = library.value("sharing", "unset");
    item["shared"] = item["source"] == "generated" ? (sharing == "always" ? "yes" : sharing == "never" ? "no" : "unasked") : "no";
    library["items"].push_back(item);
    Save(library);
    return hash;
}

std::string UserLibrary::NewFolder(const std::string& name, const std::string& parent) {
    if (name.find_first_not_of(" \t") == std::string::npos) return "a folder needs a name";
    json library = Load();
    if (!parent.empty() && !Find(library["folders"], parent)) return "no such folder";
    std::set<std::string> ids;
    for (const auto& f : library["folders"]) ids.insert(f.value("id", ""));
    std::string id = SafeName(name);
    for (int n = 2; ids.contains(id); ++n) id = SafeName(name) + "-" + std::to_string(n);
    library["folders"].push_back({{"id", id}, {"name", name}, {"parent", parent}});
    Save(library);
    return {};
}

std::string UserLibrary::RenameFolder(const std::string& id, const std::string& name) {
    json library = Load();
    json* f = Find(library["folders"], id);
    if (!f) return "no such folder";
    if (name.find_first_not_of(" \t") == std::string::npos) return "a folder needs a name";
    (*f)["name"] = name;
    Save(library);
    return {};
}

std::string UserLibrary::DeleteFolder(const std::string& id) {
    json library = Load();
    json* f = Find(library["folders"], id);
    if (!f) return "no such folder";
    const std::string parent = f->value("parent", "");
    for (auto& other : library["folders"]) {
        if (other.value("parent", "") == id) other["parent"] = parent;
    }
    for (auto& item : library["items"]) {
        if (item.value("folder", "") == id) item["folder"] = parent;
    }
    auto& folders = library["folders"];
    folders.erase(std::remove_if(folders.begin(), folders.end(), [&](const json& x) { return x.value("id", "") == id; }), folders.end());
    Save(library);
    return {};
}

std::string UserLibrary::Move(const std::string& item, const std::string& folder) {
    json library = Load();
    json* i = Find(library["items"], item);
    if (!i) return "no such item";
    if (!folder.empty() && !Find(library["folders"], folder)) return "no such folder";
    (*i)["folder"] = folder;
    Save(library);
    return {};
}

std::string UserLibrary::Rename(const std::string& item, const std::string& name) {
    json library = Load();
    json* i = Find(library["items"], item);
    if (!i) return "no such item";
    (*i)["name"] = name;
    Save(library);
    return {};
}

std::string UserLibrary::Remove(const std::string& item) {
    json library = Load();
    json* i = Find(library["items"], item);
    if (!i) return "no such item";
    std::error_code ec;
    const fs::path file = dir_ / "files" / i->value("file", "");
    if (file.parent_path() == dir_ / "files") {
        fs::remove(file, ec);
        for (const char* extra : {".meta", ".gen.json"}) fs::remove(file.string() + extra, ec);
    }
    auto& items = library["items"];
    items.erase(std::remove_if(items.begin(), items.end(), [&](const json& x) { return x.value("id", "") == item; }), items.end());
    Save(library);
    return {};
}

std::string UserLibrary::SetShared(const std::string& item, const std::string& shared) {
    if (shared != "yes" && shared != "no" && shared != "unasked") return "shared is yes, no or unasked";
    json library = Load();
    json* i = Find(library["items"], item);
    if (!i) return "no such item";
    (*i)["shared"] = shared;
    Save(library);
    return {};
}

std::string UserLibrary::SetSharing(const std::string& choice) {
    if (choice != "always" && choice != "ask" && choice != "never") return "sharing is always, ask or never";
    json library = Load();
    library["sharing"] = choice;
    // What was waiting for the answer takes it (ask: each stays to be decided).
    for (auto& item : library["items"]) {
        if (item.value("source", "") != "generated" || item.value("shared", "") != "unasked") continue;
        if (choice == "always") item["shared"] = "yes";
        if (choice == "never") item["shared"] = "no";
    }
    Save(library);
    return {};
}

std::string UserLibrary::CopyToProject(const std::string& item, const fs::path& projectRoot) const {
    json library = Load();
    const json* i = Find(library["items"], item);
    if (!i) return {};
    const fs::path from = dir_ / "files" / i->value("file", "");
    std::error_code ec;
    fs::create_directories(projectRoot / "assets" / "library", ec);
    const std::string name = SafeName(i->value("file", "asset.png"));
    const fs::path to = projectRoot / "assets" / "library" / name;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return {};
    CopyBeside(from, to);
    return "assets/library/" + name;
}

int UserLibrary::KeepGenerated(const fs::path& projectRoot) {
    const fs::path folder = projectRoot / "assets" / "textures" / "generated";
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return 0;
    json library = Load();
    std::set<std::string> kept;
    for (const auto& item : library["items"]) kept.insert(item.value("hash", ""));
    std::string generated;
    for (const auto& f : library["folders"]) {
        if (f.value("name", "") == "Generated" && f.value("parent", "").empty()) generated = f.value("id", "");
    }
    int added = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        const fs::path file = entry.path();
        if (!entry.is_regular_file() || file.extension() != ".png") continue;
        const std::string stem = file.stem().string();
        if (stem.size() > 9 && stem.substr(stem.size() - 9) == "-seamless") continue; // made from one that's kept
        if (kept.contains(HashOf(file))) continue;
        if (generated.empty()) {
            NewFolder("Generated");
            const json folders = Load()["folders"];
            for (const auto& f : folders) {
                if (f.value("name", "") == "Generated" && f.value("parent", "").empty()) generated = f.value("id", "");
            }
        }
        if (!Add(file, {{"source", "generated"}, {"origin", projectRoot.filename().string()}}, generated).empty()) ++added;
    }
    return added;
}

std::string UserLibrary::Export(const std::string& folder, const fs::path& to) const {
    json library = Load();
    std::string name = "my-library";
    std::set<std::string> within;
    if (!folder.empty()) {
        const json* f = Find(library["folders"], folder);
        if (!f) return "no such folder";
        name = SafeName(f->value("name", folder));
        // The folder and those inside it.
        within.insert(folder);
        for (bool grew = true; grew;) {
            grew = false;
            for (const auto& x : library["folders"]) {
                if (within.contains(x.value("parent", "")) && within.insert(x.value("id", "")).second) grew = true;
            }
        }
    }
    const fs::path out = to / (name + ".aige-library");
    std::error_code ec;
    fs::create_directories(out / "files", ec);
    if (ec) return "couldn't write to " + out.string();
    json exported = {{"schema", "aige.user-library/1"}, {"folders", json::array()}, {"items", json::array()}, {"sharing", "unset"}};
    for (const auto& f : library["folders"]) {
        if (folder.empty() || within.contains(f.value("id", ""))) exported["folders"].push_back(f);
    }
    for (const auto& item : library["items"]) {
        if (!folder.empty() && !within.contains(item.value("folder", ""))) continue;
        const fs::path from = dir_ / "files" / item.value("file", "");
        fs::copy_file(from, out / "files" / item.value("file", ""), fs::copy_options::overwrite_existing, ec);
        CopyBeside(from, out / "files" / item.value("file", ""));
        exported["items"].push_back(item);
    }
    std::ofstream(out / "library.json", std::ios::binary) << exported.dump(2) << "\n";
    return {};
}

std::string UserLibrary::Import(const fs::path& from) {
    std::ifstream in(from / "library.json");
    const json given = in ? json::parse(in, nullptr, false) : json();
    if (!given.is_object() || given.value("schema", "") != "aige.user-library/1") return "not an AIGE library folder (no library.json)";
    // Into a folder named after it; its folders under that one.
    const std::string top = from.stem().string();
    NewFolder(top);
    std::string into;
    const json folders = Load()["folders"];
    for (const auto& f : folders) {
        if (f.value("parent", "").empty() && f.value("name", "") == top) into = f.value("id", "");
    }
    std::map<std::string, std::string> folderIds;
    for (const auto& f : given.value("folders", json::array())) {
        const std::string parent = f.value("parent", "");
        NewFolder(f.value("name", "folder"), folderIds.contains(parent) ? folderIds[parent] : into);
        const json now = Load()["folders"];
        folderIds[f.value("id", "")] = now.back().value("id", "");
    }
    int added = 0;
    for (const auto& item : given.value("items", json::array())) {
        const std::string folder = folderIds.contains(item.value("folder", "")) ? folderIds[item.value("folder", "")] : into;
        json info = item;
        info.erase("id");
        if (!Add(from / "files" / item.value("file", ""), info, folder).empty()) ++added;
    }
    return added > 0 || given.value("items", json::array()).empty() ? std::string() : "none of its files could be read";
}

} // namespace aige
