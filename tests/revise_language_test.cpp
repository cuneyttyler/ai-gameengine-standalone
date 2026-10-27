// The revision language (docs/places/revise-language.md): each instruction checked and lowered to the
// plan's changes exactly, without the AI.
#include "places/Ontology.h"
#include "places/ReviseLanguage.h"

#include <gtest/gtest.h>

#include <set>

using namespace aige;
using json = nlohmann::json;

namespace {

const Ontology& Library() {
    static Ontology o = [] {
        Ontology ontology;
        ontology.Load({EnginePlacesDir()});
        return ontology;
    }();
    return o;
}

json Spec() {
    return {{"root",
             {{"id", "root"},
              {"module", "village"},
              {"children",
               json::array({{{"id", "ward-1"},
                             {"module", "residential-ward"},
                             {"quantity", {{"plots", {{"count", 12}}}, {"area", {{"area", 700}}}}},
                             {"rel", json::array({{{"type", "near"}, {"to", "market"}}})},
                             {"children", json::array({{{"id", "ward-1/cottages"}, {"module", "house-plot"}, {"repeat", 8}, {"quality", {{"house-kind", "cottage"}}}},
                                                       {{"id", "ward-1/well"}, {"module", "well-plaza"}}})}},
                            {{"id", "market"}, {"module", "market-ward"}, {"children", json::array()}}})}}}};
}

json Brief() { return {{"scale", {{"buildings", {{"count", json::array({20, 30})}}}}}}; }

const std::map<std::string, int> kCells = {{"ward-1", 1000}, {"market", 600}};

LoweredRevision Lower(const std::string& step, const json& program) {
    return LowerRevision(Library(), Spec(), Brief(), kCells, step, program);
}

const json* Op(const LoweredRevision& r, const std::string& op, const std::string& node) {
    for (const auto& c : r.changes) {
        if (c.value("op", "") == op && c.value("node", json("")).is_string() && c["node"] == node) return &c;
    }
    return nullptr;
}

int RepeatOf(const json& children, const std::string& module) {
    int n = 0;
    for (const auto& c : children) n += c.value("module", "") == module ? c.value("repeat", 1) : 0;
    return n;
}

} // namespace

TEST(ReviseLanguageTest, TheLanguageFileIsWhole) {
    const json& language = ReviseLanguage();
    ASSERT_FALSE(language["instructions"].empty());
    std::set<std::string> names;
    for (const auto& i : language["instructions"]) {
        SCOPED_TRACE(i.dump());
        EXPECT_TRUE(names.insert(i.value("do", "")).second) << "said twice";
        EXPECT_FALSE(i.value("says", "").empty());
        EXPECT_FALSE(i.value("shown", "").empty());
        EXPECT_FALSE(i.value("examples", json::array()).empty());
        EXPECT_FALSE(i.value("steps", json::array()).empty());
        const json params = i.value("params", json::object());
        for (const auto& [name, kind] : params.items()) {
            EXPECT_TRUE(language["params"].contains(kind.get<std::string>())) << name << ": " << kind;
        }
        for (const auto& r : i.value("required", json::array())) EXPECT_TRUE(i["params"].contains(r.get<std::string>())) << r;
    }
    EXPECT_FALSE(ReviseInstructionsFor("layout").empty());
    EXPECT_FALSE(ReviseInstructionsFor("plan").empty());
}

TEST(ReviseLanguageTest, GrowAndShrinkWhereItStands) {
    const auto grown = Lower("layout", json::array({{{"do", "grow"}, {"zone", "ward-1"}, {"by", 2}}}));
    const json* set = Op(grown, "set", "ward-1");
    ASSERT_NE(set, nullptr) << grown.notes.dump();
    EXPECT_EQ((*set)["quantity"]["area"]["area"], 2000); // twice the land it has now
    EXPECT_FALSE(set->contains("rel"));                  // nothing said of where it goes: its own relations stay
    EXPECT_TRUE(grown.inPlace.contains("ward-1"));
    EXPECT_EQ(grown.shown[0], "Grow ward-1 ×2");

    const auto shrunk = Lower("layout", json::array({{{"do", "shrink"}, {"zone", "ward-1"}}})); // the default: ×0.75
    const json* small = Op(shrunk, "set", "ward-1");
    ASSERT_NE(small, nullptr);
    EXPECT_EQ((*small)["quantity"]["area"]["area"], 750);
    EXPECT_EQ((*small)["quantity"]["plots"]["count"], 9); // fewer buildings in proportion
    EXPECT_TRUE(shrunk.inPlace.contains("ward-1"));

    // "Bigger" that isn't: said, not run.
    const auto wrong = Lower("layout", json::array({{{"do", "grow"}, {"zone", "ward-1"}, {"by", 0.5}}}));
    EXPECT_TRUE(wrong.changes.empty());
    ASSERT_EQ(wrong.notes.size(), 1u);
}

TEST(ReviseLanguageTest, AddPutsExactlyThatManyMore) {
    const auto r = Lower("plan", json::array({{{"do", "add"}, {"zone", "ward-1"}, {"kind", "house-plot"}, {"count", 10}, {"look", {{"house-kind", "cottage"}}}}}));
    const json* zone = Op(r, "zone", "ward-1");
    ASSERT_NE(zone, nullptr) << r.notes.dump();
    EXPECT_EQ(RepeatOf((*zone)["children"], "house-plot"), 18); // 8 + 10, in the cottages' own group
    EXPECT_EQ((*zone)["children"].size(), 2u);                  // the well kept
    EXPECT_EQ(r.changes.size(), 1u);                            // nothing else changed
    EXPECT_EQ(r.brief["scale.buildings.count"], json::array({30, 40}));

    // Another look: a group of its own.
    const auto other = Lower("plan", json::array({{{"do", "add"}, {"zone", "ward-1"}, {"kind", "house-plot"}, {"count", 2}, {"look", {{"house-kind", "longhouse"}}}}}));
    EXPECT_EQ((*Op(other, "zone", "ward-1"))["children"].size(), 3u);

    // Two adds to one zone: one change, both counted.
    const auto twice = Lower("plan", json::array({{{"do", "add"}, {"zone", "ward-1"}, {"kind", "house-plot"}, {"count", 3}},
                                                  {{"do", "add"}, {"zone", "ward-1"}, {"kind", "house-plot"}, {"count", 4}}}));
    EXPECT_EQ(twice.changes.size(), 1u);
    EXPECT_EQ(RepeatOf((*Op(twice, "zone", "ward-1"))["children"], "house-plot"), 15);
}

TEST(ReviseLanguageTest, RemoveReplaceAndPlace) {
    const auto removed = Lower("plan", json::array({{{"do", "remove"}, {"zone", "ward-1"}, {"kind", "house-plot"}, {"count", 3}}}));
    EXPECT_EQ(RepeatOf((*Op(removed, "zone", "ward-1"))["children"], "house-plot"), 5);

    const auto one = Lower("plan", json::array({{{"do", "remove"}, {"node", "ward-1/well"}}}));
    EXPECT_NE(Op(one, "remove", "ward-1/well"), nullptr);

    const auto into = Lower("plan", json::array({{{"do", "place"}, {"node", "ward-1/cottages"}, {"side", "north"}}}));
    const json* set = Op(into, "set", "ward-1/cottages");
    ASSERT_NE(set, nullptr) << into.notes.dump();
    EXPECT_EQ((*set)["rel"][0]["type"], "in-north");
    EXPECT_EQ((*set)["rel"][0]["to"], "ward-1");

    const auto replaced = Lower("plan", json::array({{{"do", "replace"}, {"zone", "ward-1"}, {"kind", "house-plot"}, {"with", "well-plaza"}, {"count", 2}}}));
    const json* zone = Op(replaced, "zone", "ward-1");
    if (replaced.notes.empty()) {
        ASSERT_NE(zone, nullptr);
        EXPECT_EQ(RepeatOf((*zone)["children"], "house-plot"), 6);
    } else {
        EXPECT_TRUE(replaced.changes.empty()); // a kind the zone can't hold: said, nothing changed
    }
}

TEST(ReviseLanguageTest, WhatCantRunIsSaidAndLeftOut) {
    const auto r = Lower("plan", json::array({{{"do", "grow"}, {"zone", "ward-1"}},                    // a layout instruction, at the plan
                                              {{"do", "add"}, {"zone", "nowhere"}, {"kind", "house-plot"}, {"count", 2}},
                                              {{"do", "fly"}, {"zone", "ward-1"}},                     // not in the language
                                              {{"do", "add"}, {"zone", "ward-1"}, {"kind", "house-plot"}}})); // no count
    EXPECT_TRUE(r.changes.empty());
    EXPECT_EQ(r.notes.size(), 4u) << r.notes.dump(2);
}

TEST(ReviseLanguageTest, SwapMoveAndKeepAtTheLayout) {
    const auto r = Lower("layout", json::array({{{"do", "swap"}, {"a", "ward-1"}, {"b", "market"}},
                                                {{"do", "move"}, {"zone", "market"}, {"rel", json::array({{{"type", "north-of"}, {"to", "ward-1"}},
                                                                                                          {{"type", "near"}, {"to", "market"}}})}},
                                                {{"do", "keep"}, {"zone", "ward-1"}}}));
    ASSERT_EQ(r.changes.size(), 2u) << r.notes.dump();
    EXPECT_EQ(r.changes[0]["op"], "swap");
    const json* move = Op(r, "move", "market");
    ASSERT_NE(move, nullptr);
    EXPECT_EQ((*move)["rel"].size(), 1u); // never relative to itself
    ASSERT_EQ(r.expect.size(), 1u);
    EXPECT_EQ(r.expect[0]["type"], "north-of");
    EXPECT_TRUE(r.keep.contains("ward-1"));
}
