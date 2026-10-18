#include "places/OntologyExport.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <set>
#include <sstream>

namespace aige {

using json = nlohmann::json;

namespace {

const char* kPrefixes = R"(@prefix gbo:     <https://w3id.org/aige/gbo#> .
@prefix gbl:     <https://w3id.org/aige/gbo/library#> .
@prefix gbb:     <https://w3id.org/aige/gbo/build#> .
@prefix gbv:     <https://w3id.org/aige/gbo/vocab#> .
@prefix gbr:     <https://w3id.org/aige/gbo/role#> .
@prefix gbx:     <https://w3id.org/aige/gbo/relation#> .
@prefix gbd:     <https://w3id.org/aige/gbo/rule#> .
@prefix gbs:     <https://w3id.org/aige/gbo/shapes#> .
@prefix owl:     <http://www.w3.org/2002/07/owl#> .
@prefix rdf:     <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
@prefix rdfs:    <http://www.w3.org/2000/01/rdf-schema#> .
@prefix xsd:     <http://www.w3.org/2001/XMLSchema#> .
@prefix skos:    <http://www.w3.org/2004/02/skos/core#> .
@prefix sh:      <http://www.w3.org/ns/shacl#> .
@prefix dcterms: <http://purl.org/dc/terms/> .
)";

const std::vector<std::string> kDensityWords = {"none", "rare", "some", "common", "abundant"};

// A Turtle literal.
std::string Lit(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out + "\"";
}
std::string En(const std::string& text) { return Lit(text) + "@en"; }
std::string Json(const json& value) { return Lit(value.dump()) + "^^rdf:JSON"; }
std::string Bool(bool b) { return b ? "true" : "false"; }
std::string Int(long long n) { return std::to_string(n); }
// Always with a point, so it reads as xsd:decimal.
std::string Dec(double d) {
    char buffer[64];
    auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), d, std::chars_format::fixed);
    std::string text(buffer, ec == std::errc() ? end : buffer);
    if (text.find('.') == std::string::npos) text += ".0";
    return text;
}

// A prefixed name when the local part is safe in Turtle, else a full IRI.
std::string Term(const std::string& prefix, const std::string& ns, const std::string& local) {
    bool safe = !local.empty() && local.front() != '-' && local.front() != '.' && local.back() != '.';
    for (const char c : local) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) safe = false;
    }
    if (safe) return prefix + ":" + local;
    std::string encoded;
    for (const unsigned char c : local) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            static const char* hex = "0123456789ABCDEF";
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 15];
        }
    }
    return "<" + ns + encoded + ">";
}

const std::string kLib = "https://w3id.org/aige/gbo/library#";

std::string ModuleTerm(const std::string& id) { return Term("gbl", kLib, GboClassName(id)); }
std::string ClassTerm(const std::string& cls) { return Term("gbl", kLib, cls); }
std::string FieldTerm(const ModuleDef& m, const std::string& field) { return Term("gbl", kLib, GboClassName(m.id) + "." + field); }
std::string SlotTerm(const ModuleDef& m, const std::string& slot) { return Term("gbl", kLib, GboClassName(m.id) + ".slot." + slot); }
std::string ChildTerm(const ModuleDef& m, const std::string& child) {
    const std::string target = child.rfind("class:", 0) == 0 ? child.substr(6) : child;
    return Term("gbl", kLib, GboClassName(m.id) + ".child." + target);
}
std::string BuildTerm(const std::string& id) { return Term("gbb", "https://w3id.org/aige/gbo/build#", id); }
std::string ScaleTerm(const std::string& build, const std::string& id) {
    return Term("gbb", "https://w3id.org/aige/gbo/build#", build + "." + id);
}
std::string SchemeTerm(const std::string& scheme) { return Term("gbv", "https://w3id.org/aige/gbo/vocab#", scheme); }
std::string ConceptTerm(const std::string& scheme, const std::string& id) {
    return Term("gbv", "https://w3id.org/aige/gbo/vocab#", scheme + "." + id);
}
std::string RoleTerm(const std::string& id) { return Term("gbr", "https://w3id.org/aige/gbo/role#", id); }
std::string RelationTerm(const std::string& id) { return Term("gbx", "https://w3id.org/aige/gbo/relation#", id); }
std::string RuleTerm(const std::string& id) { return Term("gbd", "https://w3id.org/aige/gbo/rule#", id); }
std::string ShapeTerm(const std::string& local) { return Term("gbs", "https://w3id.org/aige/gbo/shapes#", local); }

std::string LevelTerm(PlaceLevel level) {
    switch (level) {
        case PlaceLevel::Site: return "gbo:Site";
        case PlaceLevel::Network: return "gbo:Network";
        case PlaceLevel::Zone: return "gbo:Zone";
        case PlaceLevel::Parcel: return "gbo:Parcel";
        case PlaceLevel::Feature: return "gbo:Feature";
        case PlaceLevel::Dressing: return "gbo:Dressing";
        case PlaceLevel::Abstract: return "gbo:AbstractPart";
    }
    return "gbo:Place";
}

// A module or class a child rule, `requires` or a rule names.
std::string TargetTerm(const Ontology& ontology, const std::string& target) {
    const std::string id = target.rfind("class:", 0) == 0 ? target.substr(6) : target;
    if (ontology.Module(id)) return ModuleTerm(id);
    return ClassTerm(id);
}

// One subject's statements: "s p o ;" lines ending in " .".
class Block {
public:
    explicit Block(std::string subject) : subject_(std::move(subject)) {}
    Block& Add(const std::string& predicate, const std::string& object) {
        lines_.push_back({predicate, object});
        return *this;
    }
    Block& AddAll(const std::string& predicate, const std::vector<std::string>& objects) {
        for (const auto& o : objects) Add(predicate, o);
        return *this;
    }
    std::string Str() const {
        std::ostringstream out;
        out << subject_;
        // Group objects of one predicate: "p o1, o2".
        std::vector<std::pair<std::string, std::vector<std::string>>> grouped;
        for (const auto& [p, o] : lines_) {
            if (!grouped.empty() && grouped.back().first == p) grouped.back().second.push_back(o);
            else grouped.push_back({p, {o}});
        }
        for (size_t i = 0; i < grouped.size(); ++i) {
            out << (i == 0 ? "\n    " : " ;\n    ") << grouped[i].first << " ";
            for (size_t j = 0; j < grouped[i].second.size(); ++j) out << (j ? ", " : "") << grouped[i].second[j];
        }
        out << " .\n\n";
        return out.str();
    }

private:
    std::string subject_;
    std::vector<std::pair<std::string, std::string>> lines_;
};

// A blank node: "[ p o ; p o ]".
std::string Anon(const std::vector<std::pair<std::string, std::string>>& props, const std::string& indent = "        ") {
    std::string out = "[\n";
    for (size_t i = 0; i < props.size(); ++i) {
        out += indent + props[i].first + " " + props[i].second + (i + 1 < props.size() ? " ;\n" : "\n");
    }
    return out + indent.substr(0, indent.size() >= 4 ? indent.size() - 4 : 0) + "]";
}

std::string List(const std::vector<std::string>& items) {
    std::string out = "(";
    for (const auto& item : items) out += " " + item;
    return out + " )";
}

bool IsQuantityNumber(const std::string& type) {
    return type == "count" || type == "share" || type == "area" || type == "length" || type == "width" || type == "ratio" ||
           type == "spacing";
}

std::string Header(const std::string& which, const std::string& title, const std::string& description) {
    std::ostringstream out;
    out << "# " << title << "\n#\n# GENERATED from data/places/ by the engine (engine/places/OntologyExport.cpp).\n"
        << "# Don't edit: change the JSON files and regenerate (ontology/README.md).\n\n"
        << kPrefixes << "\n";
    out << Block("<" + std::string(kGboNamespace) + "/" + which + ">")
               .Add("a", "owl:Ontology")
               .Add("owl:versionIRI", "<" + std::string(kGboNamespace) + "/" + which + "/" + kGboVersion + ">")
               .Add("owl:versionInfo", Lit(kGboVersion))
               .Add("owl:imports", "<" + std::string(kGboNamespace) + ">")
               .Add("dcterms:title", En(title))
               .Add("dcterms:description", En(description))
               .Add("dcterms:license", "<https://creativecommons.org/licenses/by/4.0/>")
               .Add("dcterms:creator", Lit("AIGE project"))
               .Str();
    return out.str();
}

} // namespace

std::string GboClassName(const std::string& id) {
    std::string out;
    bool upper = true;
    for (const char c : id) {
        if (c == '-' || c == '_' || c == ' ') {
            upper = true;
            continue;
        }
        out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    return out;
}

std::string ExportLibraryTurtle(const Ontology& ontology) {
    std::ostringstream out;
    out << Header("library", "GBO library: modules, vocabularies, roles, relations and rules",
                  "The engine's place library in GBO terms: every module with its fields, slots, child rules, constraints, "
                  "relations and site preferences; the module classes; the build types with their scale tables; the concept "
                  "schemes; the asset roles; the spatial relation types; and the design rules with their sources.");

    // Module classes.
    out << "# --- Module classes ---\n\n";
    for (const auto& [id, cls] : ontology.Classes()) {
        Block b(ClassTerm(id));
        b.Add("a", "owl:Class, gbo:ModuleClass");
        if (cls.superclasses.empty()) b.Add("rdfs:subClassOf", "gbo:Place");
        for (const auto& s : cls.superclasses) b.Add("rdfs:subClassOf", ClassTerm(s));
        b.Add("rdfs:label", En(cls.label));
        if (!cls.description.empty()) b.Add("rdfs:comment", En(cls.description));
        out << b.Str();
    }

    // Modules.
    out << "# --- Modules ---\n\n";
    for (const auto& [id, m] : ontology.Modules()) {
        Block b(ModuleTerm(id));
        b.Add("a", "owl:Class, gbo:Module");
        b.Add("rdfs:subClassOf", LevelTerm(m.level));
        for (const auto& cls : m.classes) b.Add("rdfs:subClassOf", ClassTerm(cls));
        // The child rules as OWL: what a part may be, and how many of each at least / at most.
        std::vector<std::string> allowed;
        for (const auto& c : m.children) {
            const std::string target = TargetTerm(ontology, c.module);
            allowed.push_back(target);
            if (c.min > 0 && c.when.empty()) {
                b.Add("rdfs:subClassOf", Anon({{"a", "owl:Restriction"}, {"owl:onProperty", "gbo:hasPart"},
                                               {"owl:minQualifiedCardinality", Lit(Int(c.min)) + "^^xsd:nonNegativeInteger"},
                                               {"owl:onClass", target}}));
            }
            if (c.max < 1'000'000 && c.count.empty()) {
                b.Add("rdfs:subClassOf", Anon({{"a", "owl:Restriction"}, {"owl:onProperty", "gbo:hasPart"},
                                               {"owl:maxQualifiedCardinality", Lit(Int(c.max)) + "^^xsd:nonNegativeInteger"},
                                               {"owl:onClass", target}}));
            }
        }
        if (allowed.empty()) {
            b.Add("rdfs:subClassOf", Anon({{"a", "owl:Restriction"}, {"owl:onProperty", "gbo:hasPart"},
                                           {"owl:maxCardinality", "\"0\"^^xsd:nonNegativeInteger"}}));
        } else {
            b.Add("rdfs:subClassOf",
                  Anon({{"a", "owl:Restriction"}, {"owl:onProperty", "gbo:hasPart"},
                        {"owl:allValuesFrom", allowed.size() == 1 ? allowed[0] : Anon({{"a", "owl:Class"}, {"owl:unionOf", List(allowed)}}, "            ")}}));
        }
        b.Add("rdfs:label", En(m.name));
        if (!m.description.empty()) b.Add("rdfs:comment", En(m.description));
        b.Add("gbo:moduleId", Lit(m.id));
        b.Add("gbo:level", LevelTerm(m.level));
        for (const auto& build : m.buildTypes) b.Add("gbo:allowedIn", BuildTerm(build));
        for (const auto* fields : {&m.quantity, &m.quality}) {
            for (const auto& [name, f] : *fields) b.Add("gbo:hasField", FieldTerm(m, name));
        }
        for (const auto& slot : m.slots) b.Add("gbo:hasSlot", SlotTerm(m, slot.name));
        for (const auto& c : m.children) b.Add("gbo:hasChildRule", ChildTerm(m, c.module));
        for (const auto& c : m.constraints) b.Add("gbo:constraint", Lit(c) + "^^gbo:Expression");
        for (const auto& r : m.relations) {
            std::vector<std::pair<std::string, std::string>> props = {
                {"a", "gbo:RelationUse"}, {"gbo:relationType", RelationTerm(r.type)}, {"gbo:members", Json(r.members)},
                {"gbo:weight", Dec(r.weight)}};
            if (r.hard) props.push_back({"gbo:hard", "true"});
            if (r.args.is_object() && !r.args.empty()) props.push_back({"gbo:arguments", Json(r.args)});
            b.Add("gbo:typicallyRelated", Anon(props));
        }
        for (const auto& t : m.required) b.Add("gbo:requires", TargetTerm(ontology, t));
        for (const auto& t : m.excludes) b.Add("gbo:excludes", TargetTerm(ontology, t));
        for (const auto& [criterion, weight] : m.prefersSite) {
            b.Add("gbo:prefersSite", Anon({{"a", "gbo:SitePreference"}, {"gbo:criterion", Lit(criterion)}, {"gbo:weight", Dec(weight)}}));
        }
        b.Add("gbo:origin", Lit(m.source));
        out << b.Str();

        // Its fields: each a gbo:Field and a property of the module's places.
        for (const auto* fields : {&m.quantity, &m.quality}) {
            for (const auto& [name, f] : *fields) {
                Block fb(FieldTerm(m, name));
                const bool object = f.type == "size" || (!f.vocabulary.empty() && (f.type == "enum" || f.type == "enumSet"));
                fb.Add("a", std::string(f.quantity ? "gbo:QuantityField" : "gbo:QualityField") +
                                (object ? ", owl:ObjectProperty" : ", owl:DatatypeProperty"));
                fb.Add("rdfs:label", En(name));
                if (!f.description.empty()) fb.Add("rdfs:comment", En(f.description));
                fb.Add("rdfs:domain", ModuleTerm(m.id));
                fb.Add("gbo:fieldName", Lit(name));
                fb.Add("gbo:fieldType", "gbo:" + f.type);
                if (f.min) fb.Add("gbo:minValue", Dec(*f.min));
                if (f.max) fb.Add("gbo:maxValue", Dec(*f.max));
                if (f.minSize.is_object()) {
                    if (f.minSize.contains("w")) fb.Add("gbo:minWidth", Dec(f.minSize["w"].get<double>()));
                    if (f.minSize.contains("h")) fb.Add("gbo:minHeight", Dec(f.minSize["h"].get<double>()));
                }
                if (f.maxSize.is_object()) {
                    if (f.maxSize.contains("w")) fb.Add("gbo:maxWidth", Dec(f.maxSize["w"].get<double>()));
                    if (f.maxSize.contains("h")) fb.Add("gbo:maxHeight", Dec(f.maxSize["h"].get<double>()));
                }
                if (!f.defaultValue.is_null()) fb.Add("gbo:defaultValue", Json(f.defaultValue));
                if (!f.unit.empty()) fb.Add("gbo:unit", Lit(f.unit));
                if (f.type == "density") fb.Add("gbo:densityLevels", Json(f.levels));
                for (const auto& v : f.values) fb.Add("gbo:allowedValue", Lit(v));
                if (!f.vocabulary.empty()) fb.Add("gbo:vocabulary", SchemeTerm(f.vocabulary));
                if (!f.when.empty()) fb.Add("gbo:when", Lit(f.when) + "^^gbo:Expression");
                if (f.ask) fb.Add("gbo:askedInConfirmation", "true");
                out << fb.Str();
            }
        }
        // Its slots.
        for (const auto& slot : m.slots) {
            Block sb(SlotTerm(m, slot.name));
            sb.Add("a", "gbo:Slot");
            sb.Add("gbo:slotName", Lit(slot.name));
            sb.Add("gbo:essential", Bool(slot.essential));
            if (slot.primary) sb.Add("gbo:primary", "true");
            std::set<std::string> roles, fields, concepts;
            for (const auto& bind : slot.binds) {
                for (size_t start = 0; start <= bind.role.size();) {
                    const size_t bar = bind.role.find('|', start);
                    const std::string role = bind.role.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
                    if (!role.empty()) roles.insert(RoleTerm(role));
                    if (bar == std::string::npos) break;
                    start = bar + 1;
                }
                for (size_t start = 0; start <= bind.concepts.size() && !bind.concepts.empty();) {
                    const size_t plus = bind.concepts.find('+', start);
                    std::string field = bind.concepts.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
                    field.erase(0, field.find_first_not_of(' '));
                    field.erase(field.find_last_not_of(' ') + 1);
                    if (m.Field(field)) fields.insert(FieldTerm(m, field));
                    if (plus == std::string::npos) break;
                    start = plus + 1;
                }
                for (const auto& word : bind.conceptList) {
                    if (auto found = ontology.ConceptsNamed(word); !found.empty()) {
                        concepts.insert(ConceptTerm(found.front()->scheme, found.front()->id));
                    }
                }
            }
            sb.AddAll("gbo:needsRole", {roles.begin(), roles.end()});
            sb.AddAll("gbo:conceptsFrom", {fields.begin(), fields.end()});
            sb.AddAll("gbo:showsConcept", {concepts.begin(), concepts.end()});
            if (!slot.when.empty()) sb.Add("gbo:when", Lit(slot.when) + "^^gbo:Expression");
            out << sb.Str();
        }
        // Its child rules.
        for (const auto& c : m.children) {
            Block cb(ChildTerm(m, c.module));
            cb.Add("a", "gbo:ChildRule");
            cb.Add("gbo:child", TargetTerm(ontology, c.module));
            cb.Add("gbo:minCount", Int(c.min));
            if (c.max < 1'000'000) cb.Add("gbo:maxCount", Int(c.max));
            if (!c.count.empty() && m.Field(c.count)) cb.Add("gbo:countField", FieldTerm(m, c.count));
            cb.Add("gbo:weight", Dec(c.weight));
            if (!c.when.empty()) cb.Add("gbo:when", Lit(c.when) + "^^gbo:Expression");
            out << cb.Str();
        }
    }

    // Build types.
    out << "# --- Build types ---\n\n";
    for (const auto& [id, build] : ontology.Builds()) {
        Block b(BuildTerm(id));
        b.Add("a", "gbo:BuildType");
        b.Add("rdfs:label", En(build.name));
        b.Add("gbo:rootModule", ModuleTerm(build.root));
        std::vector<std::string> components;
        for (const auto& c : build.components) components.push_back(ModuleTerm(c));
        b.AddAll("gbo:component", components);
        b.Add("gbo:stages", Json(build.stages));
        if (!build.briefFields.is_null()) b.Add("gbo:briefFields", Json(build.briefFields));
        for (const auto& s : build.scale) b.Add("gbo:hasScale", ScaleTerm(id, s.id));
        out << b.Str();
        for (const auto& s : build.scale) {
            out << Block(ScaleTerm(id, s.id))
                       .Add("a", "gbo:ScaleClass")
                       .Add("rdfs:label", En(s.id))
                       .Add("gbo:minBuildings", Int(s.buildings.first))
                       .Add("gbo:maxBuildings", Int(s.buildings.second))
                       .Add("gbo:minSide", Int(s.side.first))
                       .Add("gbo:maxSide", Int(s.side.second))
                       .Add("gbo:minZones", Int(s.zones.first))
                       .Add("gbo:maxZones", Int(s.zones.second))
                       .Str();
        }
    }

    // Vocabularies.
    out << "# --- Vocabularies (SKOS) ---\n\n";
    for (const auto& [scheme, values] : ontology.Schemes()) {
        Block b(SchemeTerm(scheme));
        b.Add("a", "skos:ConceptScheme");
        b.Add("skos:prefLabel", En(scheme));
        std::vector<std::string> tops;
        for (const auto& c : values) {
            if (c.broader.empty()) tops.push_back(ConceptTerm(scheme, c.id));
        }
        b.AddAll("skos:hasTopConcept", tops);
        out << b.Str();
        for (const auto& c : values) {
            Block cb(ConceptTerm(scheme, c.id));
            cb.Add("a", "skos:Concept");
            cb.Add("skos:inScheme", SchemeTerm(scheme));
            cb.Add("skos:prefLabel", En(c.label));
            std::vector<std::string> alts;
            for (const auto& a : c.alt) alts.push_back(En(a));
            cb.AddAll("skos:altLabel", alts);
            cb.Add("skos:notation", Lit(c.id));
            if (c.broader.empty()) cb.Add("skos:topConceptOf", SchemeTerm(scheme));
            else cb.Add("skos:broader", ConceptTerm(scheme, c.broader));
            out << cb.Str();
        }
    }

    // Roles.
    out << "# --- Asset roles ---\n\n";
    for (const auto& [id, role] : ontology.Roles()) {
        Block b(RoleTerm(id));
        b.Add("a", "gbo:Role, skos:Concept");
        b.Add("skos:inScheme", "gbo:roles");
        b.Add("skos:prefLabel", En(role.label));
        b.Add("skos:notation", Lit(id));
        if (role.broader.empty()) b.Add("skos:topConceptOf", "gbo:roles");
        else b.Add("skos:broader", RoleTerm(role.broader));
        if (!role.affordance.empty()) {
            b.Add("gbo:affordance", ontology.FindConcept("affordance", role.affordance) ? ConceptTerm("affordance", role.affordance)
                                                                                         : Lit(role.affordance));
        }
        out << b.Str();
    }

    // Relation types.
    out << "# --- Relation types ---\n\n";
    for (const auto& [id, r] : ontology.Relations()) {
        Block b(RelationTerm(id));
        b.Add("a", "gbo:RelationType");
        b.Add("rdfs:label", En(id));
        if (!r.description.empty()) b.Add("rdfs:comment", En(r.description));
        b.Add("gbo:minMembers", Int(r.minMembers));
        if (r.maxMembers >= 0) b.Add("gbo:maxMembers", Int(r.maxMembers));
        std::vector<std::string> args;
        for (const auto& a : r.args) args.push_back(Lit(a));
        b.AddAll("gbo:argument", args);
        b.Add("gbo:hardByDefault", Bool(r.hardByDefault));
        out << b.Str();
    }

    // Design rules.
    out << "# --- Design rules ---\n\n";
    std::vector<const DesignRule*> rules;
    for (const auto& r : ontology.Rules()) rules.push_back(&r);
    std::sort(rules.begin(), rules.end(), [](const DesignRule* a, const DesignRule* b) { return a->id < b->id; });
    for (const DesignRule* r : rules) {
        Block b(RuleTerm(r->id));
        b.Add("a", "gbo:DesignRule");
        b.Add("gbo:ruleText", En(r->text));
        b.Add("gbo:source", Lit(r->source));
        if (!r->appliesTo.empty()) b.Add("gbo:appliesTo", TargetTerm(ontology, r->appliesTo));
        if (!r->relation.is_null()) b.Add("gbo:impliesRelation", Json(r->relation));
        out << b.Str();
    }
    return out.str();
}

std::string ExportShapesTurtle(const Ontology& ontology) {
    std::ostringstream out;
    out << Header("shapes", "GBO shapes: SHACL for the library and for built places",
                  "Shapes that check the library itself (every module has a level and an id, every field a type, every "
                  "design rule a source) and, per module, the fields and parts of a built place described in GBO terms. "
                  "Conditions (gbo:when) and aggregate constraints (gbo:constraint) are beyond SHACL Core; the engine "
                  "evaluates them.");

    // --- Shapes of the library ---
    out << "# --- The library ---\n\n";
    auto exactlyOne = [](const std::string& path, const std::string& extra = {}) {
        return Anon({{"sh:path", path}, {"sh:minCount", "1"}, {"sh:maxCount", "1" + extra}});
    };
    std::vector<std::string> levels = {"gbo:Site", "gbo:Network", "gbo:Zone", "gbo:Parcel", "gbo:Feature", "gbo:Dressing", "gbo:AbstractPart"};
    out << Block(ShapeTerm("ModuleShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:Module")
               .Add("sh:property", exactlyOne("gbo:moduleId", " ;\n        sh:datatype xsd:string"))
               .Add("sh:property", exactlyOne("gbo:level", " ;\n        sh:in " + List(levels)))
               .Add("sh:property", Anon({{"sh:path", "rdfs:label"}, {"sh:minCount", "1"}}))
               .Add("sh:property", Anon({{"sh:path", "gbo:hasField"}, {"sh:class", "gbo:Field"}}))
               .Add("sh:property", Anon({{"sh:path", "gbo:hasSlot"}, {"sh:class", "gbo:Slot"}}))
               .Add("sh:property", Anon({{"sh:path", "gbo:hasChildRule"}, {"sh:class", "gbo:ChildRule"}}))
               .Add("sh:property", Anon({{"sh:path", "gbo:allowedIn"}, {"sh:class", "gbo:BuildType"}}))
               .Add("sh:property", Anon({{"sh:path", "gbo:constraint"}, {"sh:datatype", "gbo:Expression"}}))
               .Str();
    std::vector<std::string> quantityTypes, qualityTypes;
    for (const char* t : {"count", "density", "share", "area", "size", "length", "spacing", "width", "ratio"}) quantityTypes.push_back(std::string("gbo:") + t);
    for (const char* t : {"enum", "enumSet", "mix", "tags", "description", "bool", "assetChoice"}) qualityTypes.push_back(std::string("gbo:") + t);
    out << Block(ShapeTerm("FieldShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:Field")
               .Add("sh:property", exactlyOne("gbo:fieldName", " ;\n        sh:datatype xsd:string"))
               .Add("sh:property", exactlyOne("gbo:fieldType"))
               .Add("sh:property", Anon({{"sh:path", "gbo:vocabulary"}, {"sh:maxCount", "1"}, {"sh:class", "skos:ConceptScheme"}}))
               .Add("sh:property", Anon({{"sh:path", "gbo:defaultValue"}, {"sh:maxCount", "1"}, {"sh:datatype", "rdf:JSON"}}))
               .Str();
    out << Block(ShapeTerm("QuantityFieldShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:QuantityField")
               .Add("sh:property", Anon({{"sh:path", "gbo:fieldType"}, {"sh:in", List(quantityTypes)}}))
               .Str();
    out << Block(ShapeTerm("QualityFieldShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:QualityField")
               .Add("sh:property", Anon({{"sh:path", "gbo:fieldType"}, {"sh:in", List(qualityTypes)}}))
               .Str();
    out << Block(ShapeTerm("DensityFieldShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetSubjectsOf", "gbo:densityLevels")
               .Add("sh:property", exactlyOne("gbo:densityLevels", " ;\n        sh:datatype rdf:JSON"))
               .Add("sh:property", Anon({{"sh:path", "gbo:fieldType"}, {"sh:hasValue", "gbo:density"}}))
               .Str();
    out << Block(ShapeTerm("SlotShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:Slot")
               .Add("sh:property", exactlyOne("gbo:slotName"))
               .Add("sh:property", exactlyOne("gbo:essential", " ;\n        sh:datatype xsd:boolean"))
               .Add("sh:property", Anon({{"sh:path", "gbo:needsRole"}, {"sh:minCount", "1"}, {"sh:class", "gbo:Role"}}))
               .Str();
    out << Block(ShapeTerm("ChildRuleShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:ChildRule")
               .Add("sh:property", exactlyOne("gbo:child"))
               .Add("sh:property", exactlyOne("gbo:minCount", " ;\n        sh:datatype xsd:integer ;\n        sh:minInclusive 0"))
               .Add("sh:property", Anon({{"sh:path", "gbo:maxCount"}, {"sh:maxCount", "1"}, {"sh:datatype", "xsd:integer"}}))
               .Add("sh:property", exactlyOne("gbo:weight", " ;\n        sh:datatype xsd:decimal ;\n        sh:minInclusive 0"))
               .Add("sh:property", Anon({{"sh:path", "gbo:countField"}, {"sh:maxCount", "1"}, {"sh:class", "gbo:Field"}}))
               .Str();
    out << Block(ShapeTerm("RelationUseShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:RelationUse")
               .Add("sh:property", exactlyOne("gbo:relationType", " ;\n        sh:class gbo:RelationType"))
               .Add("sh:property", exactlyOne("gbo:members", " ;\n        sh:datatype rdf:JSON"))
               .Add("sh:property", Anon({{"sh:path", "gbo:weight"}, {"sh:maxCount", "1"}, {"sh:minExclusive", "0"}}))
               .Str();
    out << Block(ShapeTerm("SitePreferenceShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:SitePreference")
               .Add("sh:property", exactlyOne("gbo:criterion"))
               .Add("sh:property", exactlyOne("gbo:weight", " ;\n        sh:minInclusive -1 ;\n        sh:maxInclusive 1"))
               .Str();
    out << Block(ShapeTerm("BuildTypeShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:BuildType")
               .Add("sh:property", exactlyOne("gbo:rootModule", " ;\n        sh:class gbo:Module"))
               .Add("sh:property", Anon({{"sh:path", "gbo:component"}, {"sh:class", "gbo:Module"}}))
               .Add("sh:property", Anon({{"sh:path", "gbo:hasScale"}, {"sh:minCount", "1"}, {"sh:class", "gbo:ScaleClass"}}))
               .Str();
    {
        Block b(ShapeTerm("ScaleClassShape"));
        b.Add("a", "sh:NodeShape").Add("sh:targetClass", "gbo:ScaleClass");
        for (const char* p : {"gbo:minBuildings", "gbo:maxBuildings", "gbo:minSide", "gbo:maxSide", "gbo:minZones", "gbo:maxZones"}) {
            b.Add("sh:property", exactlyOne(p, " ;\n        sh:datatype xsd:integer ;\n        sh:minInclusive 0"));
        }
        b.Add("sh:sparql", Anon({{"a", "sh:SPARQLConstraint"},
                                 {"sh:message", Lit("a scale class's ranges run low to high")},
                                 {"sh:prefixes", "<" + std::string(kGboNamespace) + "/shapes>"},
                                 {"sh:select", Lit("SELECT $this WHERE { $this gbo:minBuildings ?a ; gbo:maxBuildings ?b . FILTER (?a > ?b) }")}}));
        out << b.Str();
    }
    out << Block(ShapeTerm("RelationTypeShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:RelationType")
               .Add("sh:property", exactlyOne("gbo:minMembers", " ;\n        sh:datatype xsd:integer ;\n        sh:minInclusive 1"))
               .Add("sh:property", exactlyOne("gbo:hardByDefault", " ;\n        sh:datatype xsd:boolean"))
               .Str();
    out << Block(ShapeTerm("DesignRuleShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "gbo:DesignRule")
               .Add("sh:property", exactlyOne("gbo:ruleText"))
               .Add("sh:property", Anon({{"sh:path", "gbo:source"}, {"sh:minCount", "1"}, {"sh:message", Lit("every design rule names its source")}}))
               .Str();
    out << Block(ShapeTerm("ConceptShape"))
               .Add("a", "sh:NodeShape")
               .Add("sh:targetClass", "skos:Concept")
               .Add("sh:property", Anon({{"sh:path", "skos:inScheme"}, {"sh:minCount", "1"}}))
               .Add("sh:property", Anon({{"sh:path", "skos:prefLabel"}, {"sh:minCount", "1"}}))
               .Add("sh:property", exactlyOne("skos:notation"))
               .Str();
    out << Block("<" + std::string(kGboNamespace) + "/shapes>")
               .Add("sh:declare", Anon({{"sh:prefix", Lit("gbo")}, {"sh:namespace", "\"https://w3id.org/aige/gbo#\"^^xsd:anyURI"}}))
               .Str();

    // --- Shapes of built places, one per module ---
    out << "# --- Built places, per module ---\n\n";
    for (const auto& [id, m] : ontology.Modules()) {
        Block b(ShapeTerm(GboClassName(id) + "Shape"));
        b.Add("a", "sh:NodeShape");
        b.Add("sh:targetClass", ModuleTerm(id));
        for (const auto* fields : {&m.quantity, &m.quality}) {
            for (const auto& [name, f] : *fields) {
                std::vector<std::pair<std::string, std::string>> p = {{"sh:path", FieldTerm(m, name)}};
                const bool many = f.type == "enumSet" || f.type == "tags";
                if (!many) p.push_back({"sh:maxCount", "1"});
                if (f.type == "count") p.push_back({"sh:datatype", "xsd:integer"});
                else if (IsQuantityNumber(f.type)) p.push_back({"sh:datatype", "xsd:decimal"});
                if (IsQuantityNumber(f.type)) {
                    if (f.min) p.push_back({"sh:minInclusive", f.type == "count" ? Int(static_cast<long long>(*f.min)) : Dec(*f.min)});
                    if (f.max) p.push_back({"sh:maxInclusive", f.type == "count" ? Int(static_cast<long long>(*f.max)) : Dec(*f.max)});
                    if (f.type == "share") {
                        p.push_back({"sh:minInclusive", "0.0"});
                        p.push_back({"sh:maxInclusive", "1.0"});
                    }
                }
                if (f.type == "density") {
                    std::vector<std::string> words;
                    for (const auto& w : kDensityWords) words.push_back(Lit(w));
                    p.push_back({"sh:in", List(words)});
                }
                if (f.type == "size") {
                    auto side = [&](const char* key, const char* path) {
                        std::vector<std::pair<std::string, std::string>> s = {{"sh:path", path}, {"sh:minCount", "1"},
                                                                              {"sh:maxCount", "1"}, {"sh:datatype", "xsd:decimal"}};
                        if (f.minSize.is_object() && f.minSize.contains(key)) s.push_back({"sh:minInclusive", Dec(f.minSize[key].get<double>())});
                        if (f.maxSize.is_object() && f.maxSize.contains(key)) s.push_back({"sh:maxInclusive", Dec(f.maxSize[key].get<double>())});
                        return Anon(s, "                ");
                    };
                    p.push_back({"sh:node", Anon({{"sh:property", side("w", "gbo:sizeW")}, {"sh:property", side("h", "gbo:sizeH")}}, "            ")});
                }
                if (f.type == "enum" || f.type == "enumSet") {
                    std::vector<std::string> values;
                    if (!f.values.empty()) {
                        for (const auto& v : f.values) values.push_back(Lit(v));
                    } else if (const auto* scheme = ontology.Scheme(f.vocabulary)) {
                        for (const auto& c : *scheme) values.push_back(ConceptTerm(f.vocabulary, c.id));
                    }
                    if (!values.empty()) p.push_back({"sh:in", List(values)});
                }
                if (f.type == "mix" || f.type == "assetChoice") p.push_back({"sh:datatype", "rdf:JSON"});
                if (f.type == "bool") p.push_back({"sh:datatype", "xsd:boolean"});
                if (f.type == "tags" || f.type == "description") p.push_back({"sh:datatype", "xsd:string"});
                b.Add("sh:property", Anon(p));
            }
        }
        // Its parts: only allowed children, and each child rule's counts.
        std::vector<std::string> allowed;
        for (const auto& c : m.children) {
            const std::string target = TargetTerm(ontology, c.module);
            allowed.push_back(Anon({{"sh:class", target}}, "            "));
            if ((c.min > 0 && c.when.empty()) || (c.max < 1'000'000 && c.count.empty())) {
                std::vector<std::pair<std::string, std::string>> p = {
                    {"sh:path", "gbo:hasPart"}, {"sh:qualifiedValueShape", Anon({{"sh:class", target}}, "            ")}};
                if (c.min > 0 && c.when.empty()) p.push_back({"sh:qualifiedMinCount", Int(c.min)});
                if (c.max < 1'000'000 && c.count.empty()) p.push_back({"sh:qualifiedMaxCount", Int(c.max)});
                b.Add("sh:property", Anon(p));
            }
        }
        if (allowed.empty()) b.Add("sh:property", Anon({{"sh:path", "gbo:hasPart"}, {"sh:maxCount", "0"}}));
        else b.Add("sh:property", Anon({{"sh:path", "gbo:hasPart"}, {"sh:or", List(allowed)}}));
        out << b.Str();
    }
    return out.str();
}

} // namespace aige
