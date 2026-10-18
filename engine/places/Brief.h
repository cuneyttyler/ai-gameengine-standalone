#pragma once

#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aige {

// Interpret and Confirm (development_plan.md 7.4, place-interaction.md A3):
// what the AI is told can be built, merging its Brief drafts into one with
// the open questions marked, filling in what it left out, and the window the
// user confirms it in.

// What can be built, for the interpret prompt: each build type with its
// scale table, Brief fields and components (each with its asked fields and
// their allowed values). `buildType` empty: all of them.
nlohmann::json InterpretContext(const Ontology& ontology, const std::string& buildType = {});

// Whether a request asks to build a place ("build a large village by a
// river", "make a town"), rather than to change something.
bool LooksLikeBuildRequest(const Ontology& ontology, const std::string& text);

// Paths into a Brief: "scale.class", "components.garden.density".
nlohmann::json BriefValue(const nlohmann::json& brief, const std::string& path);
void SetBriefValue(nlohmann::json& brief, const std::string& path, const nlohmann::json& value);

// One field of the merged Brief, and how sure the drafts were of it.
struct BriefField {
    std::string path;
    nlohmann::json value;
    std::vector<nlohmann::json> alternatives; // the other drafts' values, if they differ
    std::vector<std::string> readings;        // which reading proposed each alternative
    double confidence = 1.0;                  // the lowest the drafts gave it
    bool disputed = false;                    // the drafts disagree
    bool low() const { return confidence < 0.6; }
};

struct BriefMerge {
    nlohmann::json brief;
    std::vector<BriefField> fields;
    std::vector<std::string> assumptions; // every draft's, without repeats
    std::vector<std::string> questions;
    const BriefField* Field(const std::string& path) const;
};

// Merges drafts ({brief, confidence, assumptions, questions, reading}): each
// field takes the most common value (a tie goes to the "typical" reading).
// A component is on if most drafts have it on; its fields are compared only
// among the drafts that have it on.
BriefMerge MergeBriefs(const std::vector<nlohmann::json>& drafts);

// Fills in what's missing: the scale numbers from the table, the class from
// the numbers (the numbers win), each component's defaults (on, its asset
// source auto), and the project's earlier answers (`defaults`).
nlohmann::json CompleteBrief(const Ontology& ontology, nlohmann::json brief, const nlohmann::json& defaults = {});

// After the user changes `path`: a new class resets the numbers to the
// class's; new numbers move the class to the one they fit.
void FollowScale(const Ontology& ontology, nlohmann::json& brief, const std::string& changedPath);

// When the AI can't read the request: a draft (as Interpret returns them) made
// from the request's own words: its size class ("large village"), the
// components and features it names ("gardens", "a river", "walled"), a few
// usual components, every field marked as a guess, and an assumption saying so.
nlohmann::json FallbackDraft(const Ontology& ontology, const std::string& query);

// The project defaults a confirmed Brief leaves (per build type: component
// sources and settings, style): the next build starts from them.
nlohmann::json DefaultsAfter(const nlohmann::json& brief, nlohmann::json defaults);

} // namespace aige
