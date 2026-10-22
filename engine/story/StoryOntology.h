#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// The Game Story Ontology (GSO, docs/story-ontology.md, development_plan.md 21.1):
// classes, properties, vocabularies, structure templates and rules, loaded from
// data/story/ and extended by a project's story/ontology/ (same file names; entries
// add to or replace the engine's).

struct StoryClassDef {
    std::string id, layer, parent, label, comment;
    std::map<std::string, std::string> fields; // name -> "text" | "number" | "boolean" | "list" | a class | a vocabulary
    std::vector<std::string> required;
};

struct StoryPropertyDef {
    std::string id, comment;
    std::vector<std::string> domain, range; // empty: anything
    bool temporal = false;                  // holds over an interval of scenes
    std::map<std::string, std::string> fields;
};

struct StoryTemplateDef {
    std::string id, label;
    std::vector<std::pair<std::string, float>> acts;  // id, share of the story
    std::vector<std::pair<std::string, float>> beats; // id, position 0-1
    std::vector<std::string> roles;                   // narrative roles it needs
};

struct StoryRuleDef {
    std::string id, group, severity, text, message;
    bool critic = false; // judged by the AI, not by a check
};

class StoryOntology {
public:
    // data/story (AIGE_STORY_DATA_DIR), loaded once.
    static const StoryOntology& Engine();
    // The engine's, plus a project's story/ontology/ if there is one.
    static StoryOntology ForProject(const std::filesystem::path& projectRoot);
    static StoryOntology Load(const std::filesystem::path& dir, std::string* error = nullptr);
    void Extend(const std::filesystem::path& dir);

    std::map<std::string, StoryClassDef> classes;
    std::map<std::string, StoryPropertyDef> properties;
    std::map<std::string, std::vector<std::string>> vocabularies;
    std::map<std::string, std::string> inverse;                       // relationship type -> its inverse
    std::map<std::string, std::vector<std::string>> strategies;       // motivation -> quest strategies
    std::map<std::string, StoryTemplateDef> templates;
    std::vector<StoryRuleDef> rules;
    std::string version;

    const StoryClassDef* Class(const std::string& id) const;
    const StoryPropertyDef* Property(const std::string& id) const;
    const StoryRuleDef* Rule(const std::string& id) const;
    // `cls` is `ancestor` or under it (Action is an Event).
    bool IsA(const std::string& cls, const std::string& ancestor) const;
    bool InVocabulary(const std::string& scheme, const std::string& value) const;
    // The field's type on a class, looking up through its parents ("" if none).
    std::string FieldType(const std::string& cls, const std::string& field) const;

    // A compact summary for prompts and the AI context.
    nlohmann::json Summary() const;
};

// Published forms (ontology/README.md), deterministic so a test can compare them with
// the committed files: gso-vocab.ttl (OWL classes and properties + SKOS schemes) and
// gso-shapes.ttl (SHACL shapes for the classes' required and typed fields).
inline constexpr const char* kGsoNamespace = "https://w3id.org/aige/gso";
std::string ExportGsoVocabTurtle(const StoryOntology& ontology);
std::string ExportGsoShapesTurtle(const StoryOntology& ontology);

} // namespace aige
