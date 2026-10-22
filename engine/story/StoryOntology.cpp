#include "story/StoryOntology.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#ifndef AIGE_STORY_DATA_DIR
#define AIGE_STORY_DATA_DIR "data/story"
#endif

namespace aige {

namespace {

std::optional<nlohmann::json> ReadJson(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    try {
        return nlohmann::json::parse(in);
    } catch (const std::exception& e) {
        if (error) *error = path.filename().string() + ": " + e.what();
        return std::nullopt;
    }
}

std::vector<std::string> Strings(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    if (j.contains(key) && j[key].is_array()) {
        for (const auto& v : j[key]) {
            if (v.is_string()) out.push_back(v.get<std::string>());
        }
    }
    return out;
}

std::map<std::string, std::string> Fields(const nlohmann::json& j) {
    std::map<std::string, std::string> out;
    if (j.contains("fields") && j["fields"].is_object()) {
        for (const auto& [k, v] : j["fields"].items()) out[k] = v.is_string() ? v.get<std::string>() : "text";
    }
    return out;
}

void Merge(StoryOntology& o, const std::filesystem::path& dir, std::string* error) {
    if (auto j = ReadJson(dir / "classes.json", error)) {
        if (j->contains("version")) o.version = j->value("version", o.version);
        const auto classes = j->value("classes", nlohmann::json::object());
        for (const auto& [id, c] : classes.items()) {
            StoryClassDef d;
            d.id = id;
            d.layer = c.value("layer", "");
            d.parent = c.value("parent", "");
            d.label = c.value("label", id);
            d.comment = c.value("comment", "");
            d.fields = Fields(c);
            d.required = Strings(c, "required");
            o.classes[id] = std::move(d);
        }
    }
    if (auto j = ReadJson(dir / "properties.json", error)) {
        const auto properties = j->value("properties", nlohmann::json::object());
        for (const auto& [id, p] : properties.items()) {
            StoryPropertyDef d;
            d.id = id;
            d.comment = p.value("comment", "");
            d.domain = Strings(p, "domain");
            d.range = Strings(p, "range");
            d.temporal = p.value("temporal", false);
            d.fields = Fields(p);
            o.properties[id] = std::move(d);
        }
    }
    if (auto j = ReadJson(dir / "vocabularies.json", error)) {
        const auto vocabularies = j->value("vocabularies", nlohmann::json::object());
        for (const auto& [id, values] : vocabularies.items()) {
            auto& list = o.vocabularies[id];
            for (const auto& v : values) {
                if (v.is_string() && std::find(list.begin(), list.end(), v.get<std::string>()) == list.end()) list.push_back(v);
            }
        }
        const auto inverse = j->value("inverse", nlohmann::json::object());
        for (const auto& [k, v] : inverse.items()) o.inverse[k] = v.is_string() ? v.get<std::string>() : "";
        const auto strategies = j->value("strategies", nlohmann::json::object());
        for (const auto& [k, v] : strategies.items()) {
            for (const auto& s : v) {
                if (s.is_string()) o.strategies[k].push_back(s);
            }
        }
    }
    if (auto j = ReadJson(dir / "templates.json", error)) {
        const auto templates = j->value("templates", nlohmann::json::object());
        for (const auto& [id, t] : templates.items()) {
            StoryTemplateDef d;
            d.id = id;
            d.label = t.value("label", id);
            for (const auto& a : t.value("acts", nlohmann::json::array())) d.acts.push_back({a.value("id", ""), a.value("share", 0.0f)});
            for (const auto& b : t.value("beats", nlohmann::json::array())) d.beats.push_back({b.value("id", ""), b.value("at", 0.0f)});
            d.roles = Strings(t, "roles");
            o.templates[id] = std::move(d);
        }
    }
    if (auto j = ReadJson(dir / "rules.json", error)) {
        for (const auto& r : j->value("rules", nlohmann::json::array())) {
            StoryRuleDef d;
            d.id = r.value("id", "");
            d.group = r.value("group", "");
            d.severity = r.value("severity", "warning");
            d.text = r.value("text", "");
            d.message = r.value("message", d.text);
            d.critic = r.value("critic", false);
            auto it = std::find_if(o.rules.begin(), o.rules.end(), [&](const StoryRuleDef& x) { return x.id == d.id; });
            if (it != o.rules.end()) *it = std::move(d);
            else o.rules.push_back(std::move(d));
        }
    }
}

} // namespace

const StoryOntology& StoryOntology::Engine() {
    static const StoryOntology engine = Load(AIGE_STORY_DATA_DIR);
    return engine;
}

StoryOntology StoryOntology::ForProject(const std::filesystem::path& projectRoot) {
    StoryOntology o = Engine();
    std::error_code ec;
    if (!projectRoot.empty() && std::filesystem::is_directory(projectRoot / "story" / "ontology", ec)) {
        o.Extend(projectRoot / "story" / "ontology");
    }
    return o;
}

StoryOntology StoryOntology::Load(const std::filesystem::path& dir, std::string* error) {
    StoryOntology o;
    Merge(o, dir, error);
    return o;
}

void StoryOntology::Extend(const std::filesystem::path& dir) { Merge(*this, dir, nullptr); }

const StoryClassDef* StoryOntology::Class(const std::string& id) const {
    auto it = classes.find(id);
    return it == classes.end() ? nullptr : &it->second;
}

const StoryPropertyDef* StoryOntology::Property(const std::string& id) const {
    auto it = properties.find(id);
    return it == properties.end() ? nullptr : &it->second;
}

const StoryRuleDef* StoryOntology::Rule(const std::string& id) const {
    for (const auto& r : rules) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

bool StoryOntology::IsA(const std::string& cls, const std::string& ancestor) const {
    std::string at = cls;
    for (int guard = 0; guard < 16 && !at.empty(); ++guard) {
        if (at == ancestor) return true;
        const auto* c = Class(at);
        if (!c) return false;
        at = c->parent;
    }
    return false;
}

bool StoryOntology::InVocabulary(const std::string& scheme, const std::string& value) const {
    auto it = vocabularies.find(scheme);
    return it != vocabularies.end() && std::find(it->second.begin(), it->second.end(), value) != it->second.end();
}

std::string StoryOntology::FieldType(const std::string& cls, const std::string& field) const {
    std::string at = cls;
    for (int guard = 0; guard < 16 && !at.empty(); ++guard) {
        const auto* c = Class(at);
        if (!c) return {};
        if (auto it = c->fields.find(field); it != c->fields.end()) return it->second;
        at = c->parent;
    }
    return {};
}

nlohmann::json StoryOntology::Summary() const {
    nlohmann::json classesJson = nlohmann::json::object();
    for (const auto& [id, c] : classes) {
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& [name, type] : c.fields) fields.push_back(name + ":" + type);
        classesJson[id] = {{"layer", c.layer}, {"fields", fields}};
        if (!c.parent.empty()) classesJson[id]["parent"] = c.parent;
    }
    nlohmann::json propertiesJson = nlohmann::json::object();
    for (const auto& [id, p] : properties) {
        propertiesJson[id] = {{"domain", p.domain}, {"range", p.range}};
        if (p.temporal) propertiesJson[id]["temporal"] = true;
        if (!p.fields.empty()) {
            nlohmann::json fields = nlohmann::json::array();
            for (const auto& [name, type] : p.fields) fields.push_back(name + ":" + type);
            propertiesJson[id]["fields"] = fields;
        }
    }
    nlohmann::json templatesJson = nlohmann::json::object();
    for (const auto& [id, t] : templates) {
        nlohmann::json acts = nlohmann::json::array();
        for (const auto& [a, share] : t.acts) acts.push_back(a);
        templatesJson[id] = {{"acts", acts}, {"roles", t.roles}};
    }
    return {{"classes", classesJson}, {"properties", propertiesJson}, {"vocabularies", vocabularies},
            {"templates", templatesJson}};
}

// --- Turtle -------------------------------------------------------------------------------

namespace {

std::string Lit(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

// "three-act" -> "ThreeAct" (concepts keep their ids: gsv:SceneMode-cutscene is awkward,
// so concepts are gsv:<scheme>_<id with - as _>).
std::string Concept(const std::string& scheme, const std::string& id) {
    std::string out = scheme + "_";
    for (char c : id) out += (c == '-' ? '_' : c);
    return "gsv:" + out;
}

const char* kPrefixes =
    "@prefix gso:     <https://w3id.org/aige/gso#> .\n"
    "@prefix gsv:     <https://w3id.org/aige/gso/vocab#> .\n"
    "@prefix gss:     <https://w3id.org/aige/gso/shapes#> .\n"
    "@prefix owl:     <http://www.w3.org/2002/07/owl#> .\n"
    "@prefix rdf:     <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
    "@prefix rdfs:    <http://www.w3.org/2000/01/rdf-schema#> .\n"
    "@prefix xsd:     <http://www.w3.org/2001/XMLSchema#> .\n"
    "@prefix skos:    <http://www.w3.org/2004/02/skos/core#> .\n"
    "@prefix sh:      <http://www.w3.org/ns/shacl#> .\n"
    "@prefix dcterms: <http://purl.org/dc/terms/> .\n\n";

} // namespace

std::string ExportGsoVocabTurtle(const StoryOntology& o) {
    std::ostringstream out;
    out << "# GSO vocabulary: classes, properties and concept schemes\n#\n"
           "# GENERATED from data/story/ by the engine (engine/story/StoryOntology.cpp).\n"
           "# Don't edit: change the JSON files and regenerate (ontology/README.md).\n\n"
        << kPrefixes;
    out << "<https://w3id.org/aige/gso/vocab>\n    a owl:Ontology ;\n    owl:versionInfo " << Lit(o.version) << " ;\n"
        << "    owl:imports <https://w3id.org/aige/gso> ;\n"
        << "    dcterms:title \"GSO vocabulary: the Game Story Ontology's classes, properties and vocabularies\"@en ;\n"
        << "    dcterms:license <https://creativecommons.org/licenses/by/4.0/> ;\n"
        << "    dcterms:creator \"AIGE project\" .\n\n# --- Classes ---\n\n";
    for (const auto& [id, c] : o.classes) {
        out << "gso:" << id << "\n    a owl:Class ;\n";
        if (!c.parent.empty()) out << "    rdfs:subClassOf gso:" << c.parent << " ;\n";
        out << "    gso:layer " << Lit(c.layer) << " ;\n";
        if (!c.comment.empty()) out << "    rdfs:comment " << Lit(c.comment) << "@en ;\n";
        out << "    rdfs:label " << Lit(c.label) << "@en .\n\n";
    }
    out << "# --- Properties ---\n\n";
    for (const auto& [id, p] : o.properties) {
        out << "gso:" << id << "\n    a owl:ObjectProperty ;\n";
        if (p.temporal) out << "    a gso:TemporalProperty ;\n";
        auto list = [&](const char* predicate, const std::vector<std::string>& classes) {
            if (classes.empty()) return;
            if (classes.size() == 1) {
                out << "    " << predicate << " gso:" << classes[0] << " ;\n";
                return;
            }
            out << "    " << predicate << " [ a owl:Class ; owl:unionOf (";
            for (const auto& c : classes) out << " gso:" << c;
            out << " ) ] ;\n";
        };
        list("rdfs:domain", p.domain);
        list("rdfs:range", p.range);
        out << "    rdfs:label " << Lit(id) << "@en ;\n    rdfs:comment " << Lit(p.comment) << "@en .\n\n";
    }
    out << "# --- Vocabularies (SKOS) ---\n\n";
    for (const auto& [scheme, values] : o.vocabularies) {
        out << "gsv:" << scheme << "\n    a skos:ConceptScheme ;\n    skos:prefLabel " << Lit(scheme) << "@en .\n\n";
        for (const auto& v : values) {
            out << Concept(scheme, v) << "\n    a skos:Concept ;\n    skos:inScheme gsv:" << scheme << " ;\n"
                << "    skos:notation " << Lit(v) << " ;\n    skos:prefLabel " << Lit(v) << "@en .\n\n";
        }
    }
    out << "# --- Structure templates ---\n\n";
    for (const auto& [id, t] : o.templates) {
        out << Concept("StructureTemplate", id) << "\n    gso:acts " << Lit([&] {
            std::string s;
            for (const auto& [a, share] : t.acts) s += (s.empty() ? "" : ", ") + a;
            return s;
        }()) << " ;\n    gso:roles " << Lit([&] {
            std::string s;
            for (const auto& r : t.roles) s += (s.empty() ? "" : ", ") + r;
            return s;
        }()) << " .\n\n";
    }
    out << "# --- Rules ---\n\n";
    for (const auto& r : o.rules) {
        std::string local = r.id;
        std::replace(local.begin(), local.end(), '-', '_');
        out << "gso:rule_" << local << "\n    a gso:StoryRule ;\n    gso:ruleId " << Lit(r.id) << " ;\n"
            << "    gso:severity " << Lit(r.severity) << " ;\n";
        if (r.critic) out << "    gso:judgedBy \"critic\" ;\n";
        out << "    rdfs:comment " << Lit(r.text) << "@en .\n\n";
    }
    return out.str();
}

std::string ExportGsoShapesTurtle(const StoryOntology& o) {
    std::ostringstream out;
    out << "# GSO shapes: SHACL for story knowledge graphs\n#\n"
           "# GENERATED from data/story/ by the engine (engine/story/StoryOntology.cpp).\n"
           "# Don't edit: change the JSON files and regenerate (ontology/README.md).\n\n"
        << kPrefixes;
    out << "<https://w3id.org/aige/gso/shapes>\n    a owl:Ontology ;\n    owl:versionInfo " << Lit(o.version) << " ;\n"
        << "    owl:imports <https://w3id.org/aige/gso> ;\n"
        << "    dcterms:title \"GSO shapes: SHACL for story knowledge graphs\"@en ;\n"
        << "    dcterms:description \"Types, required fields and closed vocabularies of each class. Temporal facts, "
           "causality and continuity rules are beyond SHACL Core; the engine checks them (engine/story/StoryRules).\"@en ;\n"
        << "    dcterms:license <https://creativecommons.org/licenses/by/4.0/> ;\n"
        << "    dcterms:creator \"AIGE project\" .\n\n";
    for (const auto& [id, c] : o.classes) {
        if (c.fields.empty()) continue;
        out << "gss:" << id << "Shape\n    a sh:NodeShape ;\n    sh:targetClass gso:" << id;
        for (const auto& [name, type] : c.fields) {
            out << " ;\n    sh:property [\n        sh:path gso:" << name << " ;\n";
            if (std::find(c.required.begin(), c.required.end(), name) != c.required.end()) out << "        sh:minCount 1 ;\n";
            out << "        sh:maxCount " << (type == "list" ? "999" : "1") << " ;\n";
            if (type == "text") out << "        sh:datatype xsd:string\n";
            else if (type == "number") out << "        sh:datatype xsd:decimal\n";
            else if (type == "boolean") out << "        sh:datatype xsd:boolean\n";
            else if (type == "list") out << "        sh:nodeKind sh:Literal\n";
            else if (o.vocabularies.contains(type)) {
                out << "        sh:in (";
                for (const auto& v : o.vocabularies.at(type)) out << " " << Lit(v);
                out << " )\n";
            } else out << "        sh:class gso:" << type << "\n";
            out << "    ]";
        }
        out << " .\n\n";
    }
    return out.str();
}

} // namespace aige
