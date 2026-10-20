#include "places/Where.h"

#include <gtest/gtest.h>

// Where things are in a solved layout, and checking a request's spatial facts on it.
using namespace aige;
using json = nlohmann::json;

namespace {

// A 20x20 site: a river down x = 10; A a strip on its west bank (x 5-9, all
// rows); B west of A in the north (x 0-4, y 0-9); C on the east bank in the south.
json Layout() {
    auto zone = [](const std::string& id, int x0, int x1, int y0, int y1) {
        json runs = json::array();
        for (int y = y0; y < y1; ++y) runs.push_back({y, x0, x1});
        return json{{"id", id}, {"statement", "site/zones"}, {"cells", runs}};
    };
    json river = json::array();
    for (int y = 0; y < 20; ++y) river.push_back({10, y});
    return {{"grid", {{"bounds", {0, 0, 20, 20}}}},
            {"regions", json::array({zone("a", 5, 10, 0, 20), zone("b", 0, 5, 0, 10), zone("c", 11, 20, 10, 20)})},
            {"paths", json::array({{{"id", "river"}, {"node", "river"}, {"class", "river"}, {"cells", river}}})}};
}

const json* Find(const json& list, const std::string& key, const std::string& value) {
    for (const auto& e : list) {
        if (e.value(key, "") == value) return &e;
    }
    return nullptr;
}

} // namespace

TEST(WhereTest, SaysWhereEachZoneIs) {
    const json where = LayoutWhere(Layout(), {{"a", "Residential ward"}});
    const json* a = Find(where["zones"], "id", "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ((*a)["name"], "Residential ward");
    EXPECT_EQ((*a)["area"], 100);
    EXPECT_EQ((*a)["river"]["bank"], "west");
    EXPECT_EQ((*a)["river"]["alongRows"], 20); // it meets the river all the way down
    const json* b = Find((*a)["others"], "zone", "b");
    EXPECT_EQ((*b)["lies"], "north-west");
    EXPECT_TRUE((*b)["touching"].get<bool>());
    const json* c = Find((*a)["others"], "zone", "c");
    EXPECT_EQ((*c)["lies"], "south-east");
    EXPECT_TRUE((*c)["touching"].get<bool>()); // across the river
    EXPECT_EQ((*Find(where["zones"], "id", "c"))["river"]["bank"], "east");
    // The map: letters, and the river.
    ASSERT_FALSE(where["map"].empty());
    EXPECT_NE(where["map"][0].get<std::string>().find('~'), std::string::npos);
    EXPECT_EQ(where["legend"]["A"], "a");
}

TEST(WhereTest, ChecksARequestsSpatialFacts) {
    const json checks = CheckExpectations(Layout(), json::array({
                                                        {{"node", "a"}, {"type", "along"}, {"to", "river"}},
                                                        {{"node", "a"}, {"type", "west-of"}, {"to", "river"}},
                                                        {{"node", "b"}, {"type", "west-of"}, {"to", "a"}},
                                                        {{"node", "c"}, {"type", "east-of"}, {"to", "river"}},
                                                        {{"node", "c"}, {"type", "west-of"}, {"to", "a"}},
                                                        {{"node", "b"}, {"type", "south-of"}, {"to", "a"}},
                                                        {{"node", "b"}, {"type", "along"}, {"to", "river"}},
                                                        {{"node", "a"}, {"type", "adjacent"}, {"to", "b"}},
                                                        {{"node", "x"}, {"type", "near"}, {"to", "a"}},
                                                    }));
    std::vector<bool> ok;
    for (const auto& c : checks) ok.push_back(c["ok"].get<bool>());
    EXPECT_EQ(ok, (std::vector<bool>{true, true, true, true, false, false, false, true, false})) << checks.dump(1);
    EXPECT_NE(checks[4]["detail"].get<std::string>().find("south-east"), std::string::npos) << checks[4].dump(); // where c lies instead
    EXPECT_NE(checks[8]["detail"].get<std::string>().find("no x"), std::string::npos);
}
