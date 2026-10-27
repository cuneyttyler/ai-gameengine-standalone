#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aige {

// The place ontology (docs/places/place-ontology.md, development_plan.md 7.1): what
// the builder knows about places — build types and their scale tables,
// modules (the parts a place is made of) with their fields, slots, children,
// constraints and relations, the concept vocabularies, asset roles, relation
// types, design rules and tolerances — as one graph every step consults.
//
// Loaded from JSON files: the engine's library (data/places/) and, on top,
// a project's own (<project>/places/), which may add or replace entries.
//
//   classes.json        module classes (Zone, Parcel, BuildingParcel, ...)
//   buildtypes.json     build types, their root module, components, scale table
//   vocabularies.json   concept schemes: values, labels, synonyms, broader
//   roles.json          asset roles (what a slot needs)
//   relations.json      spatial relation types (docs/places/layout-ir.md §6)
//   rules.json          design rules, each with its source
//   tolerances.json     conformity tolerances (aige.tolerance/1)
//   modules/*.module.json   one module each (aige.module/1)

// Levels, outer to inner. A child is never at an outer level than its parent,
// except networks (a ward has its own streets) and abstract parts.
enum class PlaceLevel { Site, Network, Zone, Parcel, Feature, Dressing, Abstract };
std::optional<PlaceLevel> PlaceLevelFromName(const std::string& name);
const char* PlaceLevelName(PlaceLevel level);

// One quantity or quality field of a module (docs/places/place-schema.md §2).
struct FieldDef {
    std::string name;
    bool quantity = true;      // else quality
    std::string type;          // count density share area size length spacing width ratio | enum enumSet mix tags description bool assetChoice
    nlohmann::json defaultValue;
    std::optional<double> min, max;              // count, area, length, spacing, width, share
    nlohmann::json minSize, maxSize;             // size: {"w":, "h":}
    std::vector<double> levels;                  // density: none, rare, some, common, abundant -> numbers
    std::string unit;
    std::vector<std::string> values;             // enum: closed values given inline
    std::string vocabulary;                      // enum / enumSet / mix: a concept scheme
    std::string when;                            // exists only when this holds (an expression)
    bool ask = false;                            // asked in the confirmation window
    std::string description;
};

struct SlotBinding {
    std::string when;     // condition (empty: always)
    std::string role;     // an asset role ("crop|flower": either)
    std::string concepts; // a quality field naming the concepts ("flower-mix"), or ""
    std::vector<std::string> conceptList; // or the concepts themselves
};

struct SlotDef {
    std::string name;
    bool essential = true;
    std::string when;
    std::vector<SlotBinding> binds; // one plain role is one binding with no condition
    nlohmann::json fallback;
    bool primary = false; // the area's main object (its house, its well)
};

struct ChildDef {
    std::string module;   // a module id, or a class ("class:BuildingParcel")
    int min = 0;
    int max = 1'000'000;
    std::string count;    // a field of the parent giving how many ("beds"), or ""
    double weight = 1.0;  // how typical (consistsOf weight)
    std::string when;
};

struct RelationDef {
    std::string type;
    std::vector<std::string> members; // "self", "parent.primary", "parent.street", ...
    nlohmann::json args;
    bool hard = false;
    double weight = 1.0;
};

struct ModuleDef {
    std::string id;
    std::string name;
    std::string description;
    PlaceLevel level = PlaceLevel::Feature;
    std::vector<std::string> buildTypes;
    std::vector<std::string> classes;       // ontology classes it belongs to (BuildingParcel, ...)
    std::map<std::string, FieldDef> quantity;
    std::map<std::string, FieldDef> quality;
    std::vector<SlotDef> slots;
    std::vector<ChildDef> children;
    std::vector<std::string> constraints;   // aggregate constraints (docs/places/place-schema.md §4.3)
    std::vector<RelationDef> relations;
    std::vector<std::string> required;      // modules or classes the place must also have
    std::vector<std::string> excludes;
    std::map<std::string, double> prefersSite; // interest criterion -> weight in [-1, 1]
    nlohmann::json compile;                 // the compile template: PLL statements with placeholders
    // How its things are laid out, by slot (docs/places/ontology-in-use.md): {"car": {"pattern": "bays", "depth": 2,
    // "aisle": 2, "ends": 1, "facing": "aisle"}}: the solver places them so, without asking anyone.
    nlohmann::json layout = nlohmann::json::object();
    std::string source;                     // where it came from: "engine", a project, "learned"

    const FieldDef* Field(const std::string& name) const;
};

// A class of modules (classes.json): BuildingParcel, Ward, WaterBody, ...
struct ModuleClass {
    std::string id;
    std::string label;
    std::string description;
    std::vector<std::string> superclasses;
};

struct Concept {
    std::string id;
    std::string scheme;
    std::string label;
    std::vector<std::string> alt; // synonyms
    std::string broader;          // a concept id in the same scheme, or ""
    nlohmann::json look;          // how it shows (a terrain's tone, made, water, solid, blocksLight, hills)
    std::string prompt;           // what choosing it means, for the AI and the Brief ("each block divided into regular rows")
};

struct Role {
    std::string id;
    std::string label;
    std::string broader;
    std::string affordance; // terrain, environmental-object, interactive-object, item, character
    // How big a thing of this role is (cells across; a cell is about a metre) and how tall for its
    // width, and what to draw for it ("a small clump of wildflowers", not "a flower"); 0 and
    // "": its broader role's (place-refine.md §2).
    float size = 0.0f;
    float tall = 0.0f;
    // How big it is against what it appears with (docs/places/ontology-in-use.md, Relative sizes): {to: a role, or
    // "storey" (a building's storey), height: a share of its height, width: of its width}. Resolved at load into size
    // and tall (its height for its width), so every role's size agrees with the others'.
    nlohmann::json relative;
    float storey = 0.0f; // a building role's storey height (cell widths): what "storey" means in a relative size
    std::string draw;
    nlohmann::json look; // how it shows (vary, light {radius, strength}, doorway): place-refine.md §13
};

struct RelationType {
    std::string id;
    int minMembers = 2;
    int maxMembers = 2;          // -1: any number
    std::vector<std::string> args;
    bool hardByDefault = false;
    std::string description;
    // A functional relation's use (docs/dungeon.md §0): {position, facing, art}: where its subject stands, which way it
    // faces relative to the other, how its art is made. Empty for the spatial ones.
    nlohmann::json use = nlohmann::json::object();
};

struct ScaleClass {
    std::string id;              // hamlet, village, large-village, town, city
    std::pair<int, int> buildings;
    std::pair<int, int> side;    // cells, the area's side
    std::pair<int, int> zones;
};

struct BuildType {
    std::string id;
    std::string name;
    std::string root;                    // the root module
    std::vector<std::string> stages;
    std::vector<std::string> components; // modules allowed as Brief components
    std::vector<ScaleClass> scale;
    nlohmann::json briefFields;          // build-type-specific Brief fields (shape options, ...)
    // What its size is counted in (7.10): the Brief's scale.buildings counts this
    // unit, the root's `field` holds it, and it's the parts of `cls` below the root
    // (buildings of a settlement, rooms of a dungeon, sites of a region).
    struct Unit {
        std::string field = "buildings";
        std::string cls = "BuildingParcel";
        std::string label = "buildings";
    } unit;
    // How it's laid out: "site" (zones partitioned, parcels along streets),
    // "dungeon" (levels of rooms and corridors) or "interior" (floors of rooms).
    std::string layout = "site";
    std::vector<std::string> words;      // words a request names it by ("cave", "fortress")
    bool isDefault = false;              // the one a request that names none gets
    int rank = 0;                        // a request naming several: the one holding the others wins (a region's towns)
    nlohmann::json look;                 // how it shows: lighting (sun, indoor), parts ([{key, role, at}])

    const ScaleClass* Scale(const std::string& id) const;
};

struct DesignRule {
    std::string id;
    std::string text;
    std::string source;
    std::string appliesTo; // a module or class
    nlohmann::json relation; // optional: the relation it implies
};

struct Tolerances {
    double countFraction = 0.05;  // counts within ±5%...
    int exactBelow = 10;          // ...or exactly, below this
    double areaFraction = 0.10;
    int densityLevels = 1;        // within one level's band
    double Count(double target) const; // the allowed difference for a count target
};

class Ontology {
public:
    // Loads every directory in order (later ones add to and replace earlier
    // ones). Returns problems with the files themselves (unreadable JSON,
    // malformed entries); Check() finds inconsistencies between entries.
    std::vector<std::string> Load(const std::vector<std::filesystem::path>& dirs);

    // Inconsistencies: unknown modules, classes, roles, vocabularies or
    // relation types referenced anywhere; levels running outward; constraint
    // expressions that don't parse; build types whose components aren't modules.
    std::vector<std::string> Check() const;

    const ModuleDef* Module(const std::string& id) const;
    const BuildType* Build(const std::string& id) const;
    // The build type whose root is this module (a Spec's), or nullptr.
    const BuildType* BuildOfRoot(const std::string& rootModule) const;
    // The build type a request gets when it names none (the settlement).
    const BuildType* DefaultBuild() const;
    const RelationType* Relation(const std::string& id) const;
    const Role* FindRole(const std::string& id) const;
    const Concept* FindConcept(const std::string& scheme, const std::string& id) const;
    const std::vector<Concept>* Scheme(const std::string& id) const;
    const Tolerances& Tolerance() const { return tolerances_; }

    const std::map<std::string, ModuleDef>& Modules() const { return modules_; }
    const std::map<std::string, BuildType>& Builds() const { return builds_; }
    const std::vector<DesignRule>& Rules() const { return rules_; }
    const std::map<std::string, ModuleClass>& Classes() const { return classes_; }
    const std::map<std::string, std::vector<Concept>>& Schemes() const { return schemes_; }
    const std::map<std::string, Role>& Roles() const { return roles_; }
    const std::map<std::string, RelationType>& Relations() const { return relations_; }

    // Class membership: a module is in its classes and their superclasses.
    // A role's size, height for its width and what to draw, its own or its nearest broader role's.
    float RoleSize(const std::string& role) const;
    // A look property of a role or a concept: its own, else its nearest broader one's (null if none).
    nlohmann::json RoleLook(const std::string& role, const std::string& key) const;
    nlohmann::json ConceptLook(const std::string& scheme, const std::string& id, const std::string& key) const;
    float RoleTall(const std::string& role) const;
    std::string RoleDraw(const std::string& role) const;
    bool IsA(const std::string& module, const std::string& cls) const;
    // Concept hierarchy: `id` is `ancestor` or narrower than it (rose IsKindOf flower).
    bool IsKindOf(const std::string& scheme, const std::string& id, const std::string& ancestor) const;
    // A word in a request ("roses", "Smithy") to concepts it names, in any scheme: labels and synonyms, any case.
    std::vector<const Concept*> ConceptsNamed(const std::string& word) const;

    // Whether `child` may sit inside `parent` (directly, by module or class), and the definition that allows it.
    const ChildDef* ChildRule(const std::string& parent, const std::string& child) const;

    // What a step needs to know about a module, for a prompt: the module, its
    // allowed children with their weights and fields, its relations and the
    // rules that apply to it, and the concepts its fields use.
    nlohmann::json Subgraph(const std::string& module) const;

private:
    std::map<std::string, std::vector<std::string>> superclasses_; // class -> its direct superclasses
    std::map<std::string, ModuleClass> classes_;
    // A module's "extends" (its parents, the build types offering it) and extensions.json files
    // (fields on modules, children, components), applied once every folder is loaded.
    void ResolveRelativeSizes(std::vector<std::string>& problems); // roles' relative sizes into size and tall
    void ApplyExtensions(const nlohmann::json& extensions, std::vector<std::string>& problems);
    std::map<std::string, ModuleDef> modules_;
    std::map<std::string, BuildType> builds_;
    std::map<std::string, std::vector<Concept>> schemes_;
    std::map<std::string, Role> roles_;
    std::map<std::string, RelationType> relations_;
    std::vector<DesignRule> rules_;
    Tolerances tolerances_;
};

// Where the engine's own place library lives: AIGE_PLACES_DATA_DIR when built
// with it, else data/places next to the source tree's root.
std::filesystem::path EnginePlacesDir();

} // namespace aige
