#include "places/Brief.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace aige {

using json = nlohmann::json;

namespace {

std::vector<std::string> Words(const std::string& text) {
    std::vector<std::string> out;
    std::string word;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!word.empty()) {
            out.push_back(word);
            word.clear();
        }
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

json FieldJson(const Ontology& ontology, const FieldDef& f) {
    json j = {{"type", f.type}};
    if (!f.defaultValue.is_null()) j["default"] = f.defaultValue;
    if (f.min) j["min"] = *f.min;
    if (f.max) j["max"] = *f.max;
    if (!f.unit.empty()) j["unit"] = f.unit;
    if (!f.values.empty()) j["values"] = f.values;
    if (!f.vocabulary.empty()) {
        json values = json::array();
        if (const auto* scheme = ontology.Scheme(f.vocabulary)) {
            for (const auto& c : *scheme) values.push_back(c.id);
        }
        j["values"] = values;
    }
    if (!f.when.empty()) j["when"] = f.when;
    if (!f.description.empty()) j["description"] = f.description;
    return j;
}

std::pair<int, int> Range(const json& v) {
    if (v.is_number()) return {v.get<int>(), v.get<int>()};
    if (v.is_object() && v.contains("count")) return Range(v["count"]);
    if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) return {v[0].get<int>(), v[1].get<int>()};
    return {-1, -1};
}

// Leaves of an object, by path (arrays are leaves).
void Flatten(const json& value, const std::string& prefix, std::map<std::string, json>& out) {
    if (value.is_object() && !value.empty()) {
        for (const auto& [key, child] : value.items()) Flatten(child, prefix.empty() ? key : prefix + "." + key, out);
    } else if (!prefix.empty()) {
        out[prefix] = value;
    }
}

} // namespace

json InterpretContext(const Ontology& ontology, const std::string& only) {
    json types = json::array();
    std::set<std::string> vocabularies;
    for (const auto& [id, build] : ontology.Builds()) {
        if (!only.empty() && id != only) continue;
        json scale = json::array();
        for (const auto& s : build.scale) {
            scale.push_back({{"class", s.id}, {"buildings", {s.buildings.first, s.buildings.second}},
                             {"side", {s.side.first, s.side.second}}, {"zones", {s.zones.first, s.zones.second}}});
        }
        json components = json::array();
        for (const auto& c : build.components) {
            const ModuleDef* m = ontology.Module(c);
            if (m == nullptr) continue;
            json fields = json::object();
            for (const auto* group : {&m->quantity, &m->quality}) {
                for (const auto& [name, f] : *group) {
                    if (!f.ask) continue;
                    fields[name] = FieldJson(ontology, f);
                }
            }
            components.push_back({{"id", m->id}, {"name", m->name}, {"description", m->description},
                                  {"level", PlaceLevelName(m->level)}, {"fields", fields}});
        }
        json root = json::object();
        if (const ModuleDef* r = ontology.Module(build.root)) {
            for (const auto* group : {&r->quantity, &r->quality}) {
                for (const auto& [name, f] : *group) {
                    if (f.ask) root[name] = FieldJson(ontology, f);
                }
            }
        }
        // Brief fields naming a vocabulary ("biome": "biome") bring its values along.
        if (build.briefFields.is_object()) {
            for (const auto& value : build.briefFields) {
                if (!value.is_object()) continue;
                for (const auto& v : value) {
                    if (v.is_string()) vocabularies.insert(v.get<std::string>());
                }
            }
        }
        types.push_back({{"id", id}, {"name", build.name}, {"root", build.root}, {"rootFields", root},
                         {"counts", build.unit.label + " (scale.buildings counts these)"}, {"words", build.words},
                         {"scale", scale}, {"briefFields", build.briefFields}, {"components", components}});
    }
    json vocab = json::object();
    for (const auto& scheme : vocabularies) {
        if (const auto* values = ontology.Scheme(scheme)) {
            json list = json::array();
            for (const auto& c : *values) list.push_back(c.id);
            vocab[scheme] = list;
        }
    }
    return {{"buildTypes", types}, {"vocabularies", vocab},
            {"densityWords", {"none", "rare", "some", "common", "abundant"}}};
}

bool LooksLikeBuildRequest(const Ontology& ontology, const std::string& text) {
    static const std::set<std::string> verbs = {"build", "make", "create", "generate", "design", "construct", "lay",
                                                "add", "place", "put"};
    static const std::set<std::string> edits = {"move", "delete", "remove", "rename", "change", "edit", "fix", "tougher",
                                                "weaker", "faster", "slower", "recolor", "recolour"};
    std::set<std::string> places;
    for (const auto& [id, build] : ontology.Builds()) {
        places.insert(id);
        for (const auto& w : Words(build.name)) places.insert(w);
        for (const auto& s : build.scale) {
            for (const auto& w : Words(s.id)) {
                if (w != "large" && w != "small") places.insert(w);
            }
        }
        if (const ModuleDef* root = ontology.Module(build.root)) places.insert(root->id);
        for (const auto& w : build.words) {
            if (w.find(' ') == std::string::npos) places.insert(w);
        }
    }
    // The request's first sentence says what it is: "Build a village by a river.
    // Add gardens." is a build; "This part looks bad, can you revise it? ...the
    // village builder..." is not, however often a place is named later on.
    const size_t end = text.find_first_of(".?!\n");
    const auto first = Words(end == std::string::npos ? text : text.substr(0, end));
    const auto all = Words(text);
    auto scan = [&](const std::vector<std::string>& words, bool& verb, bool& place, bool& edit) {
        for (const auto& w : words) {
            verb |= verbs.contains(w);
            edit |= edits.contains(w);
            std::string singular = w.size() > 3 && w.back() == 's' ? w.substr(0, w.size() - 1) : w;
            place |= places.contains(w) || places.contains(singular);
        }
    };
    bool verb = false, place = false, edit = false;
    scan(first, verb, place, edit);
    // "a village by a river" (just the place) counts; "make the town's skeletons tougher" doesn't.
    if (edit) return false;
    if (place && (verb || first.size() <= 12)) return true;
    // A short request may name the place after its first sentence ("Please. A village by a river").
    if (all.size() > 25) return false;
    verb = place = edit = false;
    scan(all, verb, place, edit);
    return !edit && place && verb;
}

json BriefValue(const json& brief, const std::string& path) {
    const json* at = &brief;
    std::istringstream in(path);
    std::string key;
    while (std::getline(in, key, '.')) {
        if (!at->is_object() || !at->contains(key)) return nullptr;
        at = &(*at)[key];
    }
    return *at;
}

void SetBriefValue(json& brief, const std::string& path, const json& value) {
    json* at = &brief;
    std::istringstream in(path);
    std::string key;
    std::vector<std::string> keys;
    while (std::getline(in, key, '.')) keys.push_back(key);
    for (size_t i = 0; i < keys.size(); ++i) {
        if (!at->is_object()) *at = json::object();
        if (i + 1 == keys.size()) {
            if (value.is_null()) at->erase(keys[i]);
            else (*at)[keys[i]] = value;
        } else {
            at = &(*at)[keys[i]];
        }
    }
}

const BriefField* BriefMerge::Field(const std::string& path) const {
    for (const auto& f : fields) {
        if (f.path == path) return &f;
    }
    return nullptr;
}

BriefMerge MergeBriefs(const std::vector<json>& drafts) {
    BriefMerge merge;
    merge.brief = json::object();
    std::vector<const json*> briefs;
    std::vector<std::string> readings;
    std::vector<const json*> confidences;
    for (const auto& d : drafts) {
        if (!d.is_object() || !d.contains("brief") || !d["brief"].is_object()) continue;
        briefs.push_back(&d["brief"]);
        readings.push_back(d.value("reading", ""));
        confidences.push_back(d.contains("confidence") && d["confidence"].is_object() ? &d["confidence"] : nullptr);
        std::set<std::string> seen;
        for (const auto& a : merge.assumptions) seen.insert(a);
        for (const char* key : {"assumptions", "questions"}) {
            auto& into = std::string(key) == "assumptions" ? merge.assumptions : merge.questions;
            for (const auto& s : d.value(key, json::array())) {
                if (!s.is_string()) continue;
                const std::string text = s.get<std::string>();
                if (std::find(into.begin(), into.end(), text) == into.end()) into.push_back(text);
            }
        }
        // Decisions: the same point (by the field it sets, else by its question) once.
        for (const auto& dec : d.value("decisions", json::array())) {
            if (!dec.is_object() || dec.value("question", "").empty()) continue;
            const std::string path = dec.value("path", "");
            auto norm = [](std::string t) {
                std::string out;
                for (const char c : t) {
                    if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return out;
            };
            json* same = nullptr;
            for (auto& m : merge.decisions) {
                if ((!path.empty() && m.value("path", "") == path) || norm(m.value("question", "")) == norm(dec.value("question", ""))) same = &m;
            }
            const std::string reading = d.value("reading", "");
            if (same == nullptr) {
                json one = dec;
                one["readings"] = json::object();
                merge.decisions.push_back(one);
                same = &merge.decisions.back();
            } else {
                // Another reading's answer: an option to pick.
                const std::string assumed = dec.value("assumed", "");
                json& options = (*same)["options"];
                if (!options.is_array()) options = json::array();
                if (!assumed.empty() && assumed != same->value("assumed", "") &&
                    std::find(options.begin(), options.end(), assumed) == options.end()) {
                    options.push_back(assumed);
                }
            }
            if (!reading.empty()) (*same)["readings"][reading] = dec.value("assumed", "");
        }
    }
    if (briefs.empty()) return merge;
    const auto typical = std::find(readings.begin(), readings.end(), "typical");
    const size_t preferred = typical == readings.end() ? 0 : static_cast<size_t>(typical - readings.begin());

    auto confidenceOf = [&](size_t i, const std::string& path) {
        double c = 1.0;
        if (confidences[i] == nullptr) return c;
        for (const auto& [key, value] : confidences[i]->items()) {
            if (!value.is_number()) continue;
            if (path == key || path.rfind(key + ".", 0) == 0 || key.rfind(path + ".", 0) == 0) c = std::min(c, value.get<double>());
        }
        return c;
    };
    // One field: the values of the drafts in `which`.
    auto decide = [&](const std::string& path, const std::vector<std::pair<size_t, json>>& values) {
        if (values.empty()) return;
        std::map<std::string, std::vector<size_t>> groups;
        std::vector<std::string> order;
        for (const auto& [i, v] : values) {
            const std::string key = v.dump();
            if (!groups.contains(key)) order.push_back(key);
            groups[key].push_back(i);
        }
        std::string best = order.front();
        for (const auto& key : order) {
            const auto& g = groups[key];
            const auto& b = groups[best];
            const bool hasPreferred = std::find(g.begin(), g.end(), preferred) != g.end();
            if (g.size() > b.size() || (g.size() == b.size() && hasPreferred)) best = key;
        }
        BriefField field;
        field.path = path;
        field.value = json::parse(best);
        field.disputed = groups.size() > 1;
        for (const auto& key : order) {
            if (key == best) continue;
            field.alternatives.push_back(json::parse(key));
            field.readings.push_back(readings[groups[key].front()]);
        }
        for (const auto& [i, v] : values) field.confidence = std::min(field.confidence, confidenceOf(i, path));
        SetBriefValue(merge.brief, path, field.value);
        merge.fields.push_back(std::move(field));
    };

    // Everything but the components.
    std::map<std::string, std::vector<std::pair<size_t, json>>> leaves;
    for (size_t i = 0; i < briefs.size(); ++i) {
        std::map<std::string, json> flat;
        for (const auto& [key, value] : briefs[i]->items()) {
            if (key != "components") Flatten(value, key, flat);
        }
        for (auto& [path, value] : flat) leaves[path].push_back({i, value});
    }
    for (const auto& [path, values] : leaves) decide(path, values);

    // Components: on by majority; their fields among the drafts that have them on.
    std::vector<std::string> ids;
    for (const json* b : briefs) {
        const json components = b->value("components", json::object());
        for (const auto& [id, c] : components.items()) {
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        }
    }
    merge.brief["components"] = json::object();
    for (const auto& id : ids) {
        std::vector<std::pair<size_t, json>> on;
        std::vector<size_t> enabledIn;
        for (size_t i = 0; i < briefs.size(); ++i) {
            const json c = briefs[i]->value("components", json::object()).value(id, json());
            const bool enabled = c.is_object() && c.value("enabled", true);
            on.push_back({i, enabled});
            if (enabled) enabledIn.push_back(i);
        }
        decide("components." + id + ".enabled", on);
        if (!BriefValue(merge.brief, "components." + id + ".enabled").get<bool>()) {
            merge.brief["components"][id] = {{"enabled", false}};
            continue;
        }
        std::map<std::string, std::vector<std::pair<size_t, json>>> fields;
        for (size_t i : enabledIn) {
            std::map<std::string, json> flat;
            const json c = (*briefs[i])["components"][id];
            for (const auto& [key, value] : c.items()) {
                if (key != "enabled") Flatten(value, "components." + id + "." + key, flat);
            }
            for (auto& [path, value] : flat) fields[path].push_back({i, value});
        }
        for (const auto& [path, values] : fields) decide(path, values);
    }
    return merge;
}

json CompleteBrief(const Ontology& ontology, json brief, const json& defaults) {
    if (!brief.is_object()) brief = json::object();
    brief["schema"] = "aige.brief/1";
    if (!brief.contains("buildType") && ontology.DefaultBuild()) brief["buildType"] = ontology.DefaultBuild()->id;
    const BuildType* build = ontology.Build(brief.value("buildType", ""));
    if (build == nullptr) return brief;
    const json mine = defaults.is_object() ? defaults.value(build->id, json::object()) : json::object();

    // Scale: the numbers win; a class alone gets its numbers.
    json& scale = brief["scale"];
    if (!scale.is_object()) scale = json::object();
    const auto numbers = Range(scale.value("buildings", json()));
    if (numbers.first >= 0) {
        FollowScale(ontology, brief, "scale.buildings");
    } else {
        const ScaleClass* cls = build->Scale(scale.value("class", ""));
        if (cls == nullptr && !build->scale.empty()) cls = &build->scale[build->scale.size() > 1 ? 1 : 0];
        if (cls) {
            scale["class"] = cls->id;
            FollowScale(ontology, brief, "scale.class");
        }
    }

    // Style and the rest: the project's earlier answers where the request said nothing.
    for (const char* key : {"style", "shape"}) {
        const json previous = mine.value(key, json::object());
        if (!previous.is_object()) continue;
        json& now = brief[key];
        if (!now.is_object()) now = json::object();
        for (const auto& [k, v] : previous.items()) {
            if (!now.contains(k)) now[k] = v;
        }
    }
    if (!brief.contains("components") || !brief["components"].is_object()) brief["components"] = json::object();
    const json previous = mine.value("components", json::object());
    for (auto& [id, c] : brief["components"].items()) {
        if (!c.is_object()) c = json::object();
        if (!c.contains("enabled")) c["enabled"] = true;
        if (!c["enabled"].get<bool>()) {
            c = {{"enabled", false}};
            continue;
        }
        // A zone's count is how many of that zone. A count far above what a place of this
        // size has in zones is its buildings (a reading wrote "count: 10-40" for the houses).
        if (const ModuleDef* zm = ontology.Module(id); zm && zm->level == PlaceLevel::Zone && c.contains("count")) {
            const auto [lo, hi] = Range(c["count"]);
            const ScaleClass* cls = build->Scale(brief["scale"].value("class", ""));
            const int most = cls ? std::max(2, cls->zones.second) : 6;
            if (hi > most) {
                for (const char* field : {"plots", "farmsteads", "tents", "rooms", "sites", "pois"}) {
                    const FieldDef* f = zm->Field(field);
                    if (f == nullptr || f->type != "count") continue;
                    json& params = c["params"];
                    if (!params.is_object()) params = json::object();
                    if (!params.contains(field)) params[field] = static_cast<int>(std::lround((lo + hi) / 2.0));
                    break;
                }
                c["count"] = 1;
            }
        }
        // A density in other words ("medium", "dense"): the nearest of the five.
        if (c.contains("density") && c["density"].is_string()) {
            static const std::map<std::string, std::string> words = {
                {"zero", "none"}, {"no", "none"}, {"absent", "none"},
                {"few", "rare"}, {"low", "rare"}, {"sparse", "rare"}, {"scarce", "rare"}, {"light", "rare"}, {"very low", "rare"},
                {"medium", "some"}, {"moderate", "some"}, {"normal", "some"}, {"average", "some"}, {"mid", "some"}, {"several", "some"},
                {"high", "common"}, {"dense", "common"}, {"many", "common"}, {"frequent", "common"}, {"lots", "common"},
                {"very high", "abundant"}, {"very dense", "abundant"}, {"plentiful", "abundant"}, {"lush", "abundant"}, {"packed", "abundant"}};
            std::string d = c["density"].get<std::string>();
            for (char& ch : d) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (auto it = words.find(d); it != words.end()) c["density"] = it->second;
        }
        if (!c.contains("assets")) {
            const json earlier = previous.is_object() ? previous.value(id, json::object()).value("assets", json()) : json();
            c["assets"] = earlier.is_object() ? earlier : json{{"source", "auto"}};
        }
    }
    return brief;
}

void FollowScale(const Ontology& ontology, json& brief, const std::string& changed) {
    const BuildType* build = ontology.Build(brief.value("buildType", ""));
    if (build == nullptr || !brief.contains("scale")) return;
    json& scale = brief["scale"];
    auto area = [&](const ScaleClass& c) {
        scale["area"] = {{"area", {c.side.first * c.side.first, c.side.second * c.side.second}}};
    };
    if (changed == "scale.class") {
        if (const ScaleClass* c = build->Scale(scale.value("class", ""))) {
            scale["buildings"] = {{"count", {c->buildings.first, c->buildings.second}}};
            area(*c);
        }
        return;
    }
    if (changed.rfind("scale.buildings", 0) != 0) return;
    const auto [lo, hi] = Range(scale.value("buildings", json()));
    if (lo < 0) return;
    // The class whose range holds the numbers' middle.
    const int mid = (lo + hi) / 2;
    const ScaleClass* fit = nullptr;
    for (const auto& c : build->scale) {
        if (mid >= c.buildings.first && mid <= c.buildings.second) fit = &c;
    }
    if (fit == nullptr && !build->scale.empty()) fit = mid < build->scale.front().buildings.first ? &build->scale.front() : &build->scale.back();
    if (fit) {
        scale["class"] = fit->id;
        if (!scale.contains("area")) area(*fit);
    }
}

namespace {
// The build type the request names most (its id, its words, its size classes), and its score (0: none named).
std::pair<const BuildType*, int> NamedBuild(const Ontology& ontology, const std::string& query) {
    std::string text;
    for (const auto& w : Words(query)) text += " " + w;
    text += " ";
    auto says = [&](const std::string& phrase) { return text.find(" " + phrase + " ") != std::string::npos; };
    auto saysWord = [&](const std::string& word) { return says(word) || says(word + "s"); };
    const BuildType* chosen = ontology.DefaultBuild();
    int best = 0;
    for (const auto& [id, b] : ontology.Builds()) {
        int score = saysWord(id) ? 3 : 0;
        for (const auto& w : b.words) {
            std::string spaced = w;
            std::replace(spaced.begin(), spaced.end(), '-', ' ');
            score += saysWord(spaced) ? 2 : 0;
        }
        for (const auto& s : b.scale) {
            std::string words = s.id;
            std::replace(words.begin(), words.end(), '-', ' ');
            score += says(words) ? 1 : 0;
        }
        if (score > 0) score += 10 * b.rank;
        if (score > best || (score == best && score > 0 && b.isDefault)) {
            best = score;
            chosen = &b;
        }
    }
    return {chosen, best};
}
} // namespace

std::string NamedBuildType(const Ontology& ontology, const std::string& query) {
    const auto [build, score] = NamedBuild(ontology, query);
    return build && score > 0 ? build->id : std::string();
}

json FallbackDraft(const Ontology& ontology, const std::string& query) {
    if (ontology.Builds().empty()) return json();
    std::string text;
    for (const auto& w : Words(query)) text += " " + w;
    text += " ";
    auto says = [&](const std::string& phrase) { return text.find(" " + phrase + " ") != std::string::npos; };
    auto saysWord = [&](const std::string& word) { return says(word) || says(word + "s"); };
    // The build type: the one the request names most (its id, its words, its size classes); else the default.
    const BuildType* chosen = ontology.DefaultBuild();
    int best = 0;
    for (const auto& [id, b] : ontology.Builds()) {
        int score = saysWord(id) ? 3 : 0;
        for (const auto& w : b.words) {
            std::string spaced = w;
            std::replace(spaced.begin(), spaced.end(), '-', ' ');
            score += saysWord(spaced) ? 2 : 0;
        }
        for (const auto& s : b.scale) {
            std::string words = s.id;
            std::replace(words.begin(), words.end(), '-', ' ');
            score += says(words) ? 1 : 0;
        }
        if (score > 0) score += 10 * b.rank;
        if (score > best || (score == best && score > 0 && b.isDefault)) {
            best = score;
            chosen = &b;
        }
    }
    const BuildType& build = *chosen;
    // The size class: the longest one named ("large village" before "village").
    std::string cls;
    for (const auto& s : build.scale) {
        std::string words = s.id;
        std::replace(words.begin(), words.end(), '-', ' ');
        if (says(words) && words.size() > cls.size()) cls = s.id;
    }
    if (cls.empty()) {
        if (saysWord("big") || saysWord("large")) cls = build.scale.size() > 2 ? build.scale[2].id : build.scale.back().id;
        else cls = build.scale.size() > 1 ? build.scale[1].id : build.scale.front().id;
    }
    json components = json::object();
    if (std::find(build.components.begin(), build.components.end(), "residential-ward") != build.components.end()) {
        components["residential-ward"] = {{"enabled", true}};
        if (cls != "hamlet") components["market-ward"] = {{"enabled", true}};
        if (cls == "town" || cls == "city") components["craftsmen-ward"] = {{"enabled", true}};
    } else if (const ModuleDef* root = ontology.Module(build.root)) {
        // The zones the root needs (a dungeon's level, a camp's ground).
        for (const ChildDef& rule : root->children) {
            const ModuleDef* m = ontology.Module(rule.module);
            const bool component = std::find(build.components.begin(), build.components.end(), rule.module) != build.components.end();
            if (m && component && m->level == PlaceLevel::Zone && rule.min >= 1) components[rule.module] = {{"enabled", true}};
        }
    }
    // Components the request names, by their id or name ("gardens", "market", "docks").
    for (const auto& id : build.components) {
        const ModuleDef* m = ontology.Module(id);
        if (m == nullptr) continue;
        std::string name = m->name;
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string spaced = id;
        std::replace(spaced.begin(), spaced.end(), '-', ' ');
        const std::string head = spaced.substr(0, spaced.find(' '));
        if (says(spaced) || says(name) || (head.size() > 3 && saysWord(head) && (m->level == PlaceLevel::Feature || m->level == PlaceLevel::Zone))) {
            components[id] = {{"enabled", true}};
        }
    }
    json features = json::array();
    const json allowed = build.briefFields.value("features", json::array());
    for (const char* f : {"river", "lake"}) {
        if (saysWord(f) && std::find(allowed.begin(), allowed.end(), f) != allowed.end()) features.push_back({{"module", f}});
    }
    const bool walled = (saysWord("wall") || says("walled") || saysWord("palisade")) && build.briefFields.contains("walled");
    json brief = {{"schema", "aige.brief/1"}, {"buildType", build.id}, {"name", "New " + build.name},
                  {"scale", {{"class", cls}}}, {"walled", walled ? (saysWord("palisade") || build.id == "camp" ? "palisade" : "stone") : "none"},
                  {"features", features},
                  {"shape", {{"layout", "organic"}}}, {"components", components}};
    json confidence = {{"scale.class", 0.3}, {"name", 0.2}, {"shape.layout", 0.3}, {"walled", 0.4}};
    for (const auto& [id, c] : components.items()) confidence["components." + id + ".enabled"] = 0.4;
    return {{"reading", "fallback"},
            {"brief", brief},
            {"confidence", confidence},
            {"assumptions", json::array({"The AI couldn't read the request, so this Brief comes from its words alone: check each field."})},
            {"questions", json::array()}};
}

json DefaultsAfter(const json& brief, json defaults) {
    if (!defaults.is_object()) defaults = json::object();
    const std::string type = brief.value("buildType", "");
    if (type.empty()) return defaults;
    json& mine = defaults[type];
    if (!mine.is_object()) mine = json::object();
    if (brief.contains("style") && brief["style"].is_object()) mine["style"] = brief["style"];
    json& components = mine["components"];
    if (!components.is_object()) components = json::object();
    const auto briefComponents = brief.value("components", json::object());
    for (const auto& [id, c] : briefComponents.items()) {
        if (c.is_object() && c.contains("assets")) components[id]["assets"] = c["assets"];
    }
    return defaults;
}

} // namespace aige
