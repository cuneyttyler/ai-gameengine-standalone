// The revision language (docs/places/revise-language.md): what a revision of a place can do,
// as instructions with parameters (data/places/revise-language.json). The AI turns the user's
// words into a program of them; this checks each instruction and lowers it to the plan's
// changes (the Spec ops the build applies, then compiled to PLL and solved) and to what the
// layout keeps (a zone grown where it stands, a zone held).
#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <set>
#include <string>

namespace aige {

class Ontology;

// The language: {params, instructions[]}, read once from the places data.
const nlohmann::json& ReviseLanguage();

// The instructions for a step ("layout" or "plan"; "plan" also stands for planned and built):
// [{do, params, required, says, shown, examples}], for the AI and for the Build panel.
nlohmann::json ReviseInstructionsFor(const std::string& step);

// A step's name in the language: "layout", or "plan" for the plan and after.
std::string ReviseStep(const std::string& stage);

struct LoweredRevision {
    nlohmann::json changes = nlohmann::json::array(); // the plan's ops (move, set, zone, add, remove, swap, occupy, shift)
    nlohmann::json brief = nlohmann::json::object();  // Brief path -> value
    nlohmann::json expect = nlohmann::json::array();  // facts to check on the new layout (a move's relations)
    std::set<std::string> inPlace;                    // zones grown or shrunk where they stand
    std::set<std::string> keep;                       // zones held where they are
    nlohmann::json shown = nlohmann::json::array();   // each instruction run, as the user reads it
    nlohmann::json notes = nlohmann::json::array();   // instructions left out, and why
    bool any() const { return !changes.empty() || !brief.empty() || !keep.empty(); }
};

// Checks and lowers a program ([{do, ...params}]) at a step. `zoneCells`: each zone's land now
// (a size asked "×1.5" is of the land the user sees). `brief`: the Brief now (its building count).
LoweredRevision LowerRevision(const Ontology& ontology, const nlohmann::json& spec, const nlohmann::json& brief,
                              const std::map<std::string, int>& zoneCells, const std::string& step, const nlohmann::json& program);

// One instruction as the user reads it ("Grow residential-ward ×1.5").
std::string ShowInstruction(const nlohmann::json& instruction);

} // namespace aige
