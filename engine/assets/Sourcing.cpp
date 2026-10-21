#include "assets/Sourcing.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

const char* ImportanceName(Importance importance) {
    switch (importance) {
        case Importance::Critical: return "critical";
        case Importance::Normal: return "normal";
        case Importance::Optional: return "optional";
    }
    return "normal";
}

NeedProfile ProfileOf(const std::string& layer, bool essential, bool joined, int uses) {
    NeedProfile p;
    const bool structural = layer == "building" || layer == "ground" || joined;
    p.importance = !essential ? Importance::Optional : structural ? Importance::Critical : Importance::Normal;
    p.styleCritical = structural || uses >= 20;
    return p;
}

std::string LicencePolicy::Normalise(const std::string& licence) {
    std::string s;
    for (const char c : licence) s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::replace(s.begin(), s.end(), '_', '-');
    auto has = [&](const char* t) { return s.find(t) != std::string::npos; };
    if (s.empty() || has("UNKNOWN") || has("PERSONAL") || has("ALL RIGHTS")) return "";
    if (has("CC0") || has("PUBLIC DOMAIN")) return "CC0";
    if (has("OGA-BY") || has("OGA BY")) return "OGA-BY";
    if (has("NC")) return "CC-BY-NC";      // non-commercial: never by default
    if (has("ND")) return "CC-BY-ND";
    if (has("SA") && (has("CC") || has("BY"))) return "CC-BY-SA";
    if (has("CC-BY") || has("CC BY") || has("ATTRIBUTION")) return "CC-BY";
    if (has("GPL")) return "GPL";
    if (has("MIT")) return "MIT";
    return s;
}

bool LicencePolicy::Allowed(const std::string& licence) const {
    const std::string n = Normalise(licence);
    if (n.empty()) return false;
    if (n == "CC0" || n == "CC-BY" || n == "OGA-BY") return true;
    if (n == "CC-BY-SA") return shareAlike;
    return extra.contains(n);
}

bool LicencePolicy::NeedsAttribution(const std::string& licence) const {
    const std::string n = Normalise(licence);
    return n == "CC-BY" || n == "OGA-BY" || n == "CC-BY-SA" || n == "GPL" || n == "MIT";
}

SourcingSettings ReadSourcingSettings(const fs::path& projectRoot) {
    SourcingSettings s;
    std::ifstream in(projectRoot / "places" / "sourcing.json");
    if (!in) return s;
    const json j = json::parse(in, nullptr, false);
    if (!j.is_object()) return s;
    s.webSearch = j.value("webSearch", false);
    s.defaultSource = j.value("defaultSource", std::string("search"));
    s.licences.shareAlike = j.value("shareAlike", false);
    for (const auto& l : j.value("extraLicences", json::array())) {
        if (l.is_string()) s.licences.extra.insert(LicencePolicy::Normalise(l.get<std::string>()));
    }
    return s;
}

void WriteSourcingSettings(const fs::path& projectRoot, const SourcingSettings& settings) {
    std::error_code ec;
    fs::create_directories(projectRoot / "places", ec);
    json extra = json::array();
    for (const auto& l : settings.licences.extra) extra.push_back(l);
    std::ofstream(projectRoot / "places" / "sourcing.json")
        << json{{"webSearch", settings.webSearch}, {"shareAlike", settings.licences.shareAlike}, {"extraLicences", extra},
                {"defaultSource", settings.defaultSource}}.dump(2) << "\n";
}

std::vector<Candidate> Licensed(const std::vector<Candidate>& candidates, const LicencePolicy& licences) {
    std::vector<Candidate> out;
    for (const auto& c : candidates) {
        if (c.entry == nullptr) continue;
        const bool own = (c.entry->source == "project" || c.entry->source == "generated") && c.entry->licence.empty();
        if (own || licences.Allowed(c.entry->licence)) out.push_back(c);
    }
    return out;
}

namespace {

const Candidate* Best(const std::vector<Candidate>& list, const std::string& grade) {
    for (const auto& c : list) {
        if (grade.empty() ? !c.grade.empty() : c.grade == grade) return &c;
    }
    return nullptr;
}

std::string SourceOf(const Candidate& c, const std::string& fallback) {
    if (c.entry && (c.entry->source == "generated" || c.entry->source == "web" || c.entry->source == "library")) return c.entry->source;
    return fallback;
}

} // namespace

Decision Decide(const std::string& policy, const NeedProfile& profile, const SourceCandidates& all, const LicencePolicy& licences,
                bool webEnabled, bool canGenerate, const Tried& tried) {
    SourceCandidates c{Licensed(all.project, licences), Licensed(all.library, licences), Licensed(all.web, licences)};
    Decision d;
    auto use = [&](const char* step, const char* action, const Candidate* candidate, const std::string& source, const std::string& reason) {
        d.step = step;
        d.action = action;
        d.candidate = *candidate;
        d.source = SourceOf(*candidate, source);
        d.reason = reason;
        return d;
    };
    auto pending = [&](const char* step, const char* action, const std::string& reason) {
        d.step = step;
        d.action = action;
        d.source = action == std::string("web") ? "web" : "generated";
        d.reason = reason;
        return d;
    };
    // A6 over the sources the policy reaches; then A7.
    auto fallback = [&](std::vector<const std::vector<Candidate>*> lists, const std::vector<std::string>& names) {
        const Candidate* best = nullptr;
        std::string from;
        for (size_t i = 0; i < lists.size(); ++i) {
            for (const auto& cand : *lists[i]) {
                if (!cand.grade.empty() && (best == nullptr || cand.fit > best->fit)) best = &cand, from = names[i];
            }
        }
        if (best) return use("A6", from == "library" || from == "web" ? "import" : "adapt", best, from, "the best that fits, adapted (" + best->grade + ")");
        d.step = "A7";
        d.source = "none";
        if (profile.importance == Importance::Optional) {
            d.action = "drop";
            d.reason = "optional, and nothing fits: its slots stay empty";
        } else {
            d.action = "placeholder";
            d.reason = "nothing fits and nothing could be made: a labelled placeholder";
        }
        return d;
    };
    // A source the user chose is honoured: the project's art counts as found only if it came
    // from there (generated art in the project doesn't stop a web search the user asked for).
    auto fromSource = [](const std::vector<Candidate>& list, std::initializer_list<const char*> sources) {
        std::vector<Candidate> out;
        for (const auto& cand : list) {
            if (!cand.entry) continue;
            for (const char* s : sources) {
                if (cand.entry->source == s) {
                    out.push_back(cand);
                    break;
                }
            }
        }
        return out;
    };
    if (policy == "project") {
        if (const Candidate* g = Best(c.project, "good")) return use("A1", "use", g, "project", "fits well");
        return fallback({&c.project}, {"project"});
    }
    if (policy == "library") {
        const auto fromLibrary = fromSource(c.project, {"library", "aige"});
        if (const Candidate* g = Best(fromLibrary, "good")) return use("A1", "use", g, "library", "from the library, already in the project");
        if (const Candidate* g = Best(c.library, "good")) return use("A2", "import", g, "library", "from the AIGE library");
        return fallback({&c.project, &c.library}, {"project", "library"});
    }
    if (policy == "search") {
        // Found art first (the most reliable): what the project has that fits, the user's library,
        // then the web; the Art tool when nothing found fits.
        if (const Candidate* g = Best(c.project, "good")) return use("S1", "use", g, "project", "fits well, already in the project");
        if (const Candidate* g = Best(c.library, "good")) return use("S2", "import", g, "library", "from your library");
        if (const Candidate* g = Best(c.web, "good")) return use("S3", "import", g, "web", "free art from the web, licence checked");
        if (!tried.web) return pending("S3", "web", "searching approved sites for art that fits");
        if (canGenerate && !tried.generate) return pending("S4", "generate", "nothing found fits: made by the Art tool");
        return fallback({&c.project, &c.library, &c.web}, {"project", "library", "web"});
    }
    if (policy == "web") {
        const auto fromWeb = fromSource(c.project, {"web"});
        if (const Candidate* g = Best(fromWeb, "good")) return use("A1", "use", g, "web", "from the web, already in the project");
        if (const Candidate* g = Best(c.web, "good")) {
            d = use("A4", "import", g, "web", "free art from the web, licence checked");
            d.warn = profile.styleCritical;
            return d;
        }
        if (!tried.web) {
            d = pending("A4", "web", "searching approved sites for free art");
            d.warn = profile.styleCritical;
            return d;
        }
        // The web had nothing that fits: the Art tool makes it.
        if (canGenerate && !tried.generate) return pending("A5", "generate", "nothing fitting on the web: made by the Art tool");
        return fallback({&c.project, &c.web}, {"project", "web"});
    }
    if (policy == "generate") {
        if (tried.generate) {
            if (const Candidate* g = Best(c.project, "good"); g && g->entry && g->entry->source == "generated") {
                return use("G", "use", g, "generated", "made by the Art tool, checked");
            }
            return fallback({&c.project}, {"project"});
        }
        if (canGenerate) return pending("G", "generate", "made by the Art tool in the place's style");
        // Couldn't be made: free art from the web, if searching is on.
        if (webEnabled) {
            const auto fromWeb = fromSource(c.project, {"web"});
            if (const Candidate* g = Best(fromWeb, "good")) return use("A4", "use", g, "web", "made art failed: from the web");
            if (!tried.web) return pending("A4", "web", "the Art tool's tries failed: searching approved sites");
        }
        return fallback({&c.project}, {"project"});
    }
    // auto
    if (const Candidate* g = Best(c.project, "good")) return use("A1", "use", g, "project", "fits well");
    if (const Candidate* g = Best(c.library, "good")) return use("A2", "import", g, "library", "from the AIGE library");
    if (profile.styleCritical && canGenerate && !tried.generate) {
        return pending("A3", "generate", "style-critical: made in the place's style (web art rarely matches a pack)");
    }
    if (!profile.styleCritical && webEnabled) {
        if (const Candidate* g = Best(c.web, "good")) return use("A4", "import", g, "web", "free art from the web, licence checked");
        if (!tried.web) return pending("A4", "web", "searching approved sites for free art");
    }
    if (canGenerate && !tried.generate) return pending("A5", "generate", "nothing fits: made by the Art tool");
    // Made and checked, and it failed: the web, if searching is on.
    if (webEnabled && !tried.web) return pending("A4", "web", "the Art tool's tries failed: searching approved sites");
    std::vector<const std::vector<Candidate>*> lists = {&c.project, &c.library};
    std::vector<std::string> names = {"project", "library"};
    if (webEnabled) lists.push_back(&c.web), names.push_back("web");
    return fallback(lists, names);
}

Catalog LoadAssetLibrary(const fs::path& projectRoot) {
    Catalog catalog;
    std::ifstream in(projectRoot / "library" / "catalog.json");
    if (!in) return catalog;
    const json j = json::parse(in, nullptr, false);
    for (const auto& e : j.is_array() ? j : j.value("entries", json::array())) {
        if (!e.is_object()) continue;
        CatalogEntry entry;
        entry.id = e.value("id", e.value("path", ""));
        entry.path = "library/" + e.value("path", "");
        entry.name = e.value("name", "");
        entry.kind = e.value("kind", "");
        for (const auto& r : e.value("roles", json::array())) entry.roles.push_back(r);
        for (const auto& c : e.value("concepts", json::array())) entry.concepts.push_back(c);
        entry.perspective = e.value("perspective", "");
        entry.layer = e.value("layer", "");
        entry.footprint.w = e.value("w", 1);
        entry.footprint.h = e.value("h", 1);
        entry.affordance = e.value("affordance", "");
        entry.source = "library";
        entry.licence = e.value("licence", "");
        entry.author = e.value("author", "");
        entry.url = e.value("url", "");
        entry.rect = {0, 0, e.value("pixelsW", 64), e.value("pixelsH", 64)};
        catalog.entries.push_back(entry);
    }
    return catalog;
}

bool WriteCredits(const fs::path& projectRoot, const json& used, const LicencePolicy& licences) {
    static const std::string begin = "<!-- aige:credits (kept by the engine; edit outside these lines) -->";
    static const std::string end = "<!-- /aige:credits -->";
    std::set<std::string> lines;
    for (const auto& u : used) {
        const std::string licence = u.value("licence", "");
        if (!licences.NeedsAttribution(licence)) continue;
        std::string line = "- " + fs::path(u.value("path", "")).filename().string();
        if (!u.value("author", "").empty()) line += " by " + u.value("author", "");
        line += " (" + LicencePolicy::Normalise(licence) + ")";
        if (!u.value("url", "").empty()) line += ": " + u.value("url", "");
        lines.insert(line);
    }
    const fs::path file = projectRoot / "CREDITS.md";
    std::string text;
    {
        std::ifstream in(file);
        std::stringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }
    std::string section = begin + "\n";
    if (lines.empty()) section += "No assets that need attribution.\n";
    for (const auto& l : lines) section += l + "\n";
    section += end;
    const size_t a = text.find(begin), b = text.find(end);
    if (a != std::string::npos && b != std::string::npos && b > a) {
        text = text.substr(0, a) + section + text.substr(b + end.size());
    } else {
        if (text.empty()) text = "# Credits\n\nArt used in this game that needs attribution.\n\n";
        text += (text.ends_with("\n") ? "" : "\n") + section + "\n";
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

} // namespace aige
