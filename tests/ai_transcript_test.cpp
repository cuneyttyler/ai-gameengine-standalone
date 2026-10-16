#include <gtest/gtest.h>

#include "ai_bridge/AiTranscript.h"
#include "ai_bridge/ArtHistory.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md 4.5: AI work survives saving, quitting and reopening.
namespace {

fs::path TempFile(const std::string& name) {
    fs::path dir = fs::temp_directory_path() /
                   ("aige_keep_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(dir);
    return dir / ".cache" / name;
}

} // namespace

TEST(AiTranscriptTest, TheConversationComesBackAfterARestart) {
    const fs::path file = TempFile("ai_conversation.json");
    {
        aige::AiTranscript transcript(file);
        transcript.Load();
        EXPECT_TRUE(transcript.Empty());
        transcript.Record({{"type", "ai:message"}, {"role", "user"}, {"text", "make skeletons faster"}},
                          {{"user", "make skeletons faster"}});
        transcript.Record({{"type", "ai:busy"}, {"busy", true}}, {}); // not part of the chat
        transcript.Record({{"type", "ai:proposal"}, {"id", 1}, {"summary", "Faster skeletons"}, {"items", nlohmann::json::array()}},
                          {{"user", "make skeletons faster"}, {"assistant", "Faster skeletons"}});
        transcript.Record({{"type", "ai:decided"}, {"id", 1}, {"status", "applied"}},
                          {{"user", "make skeletons faster"}, {"assistant", "Faster skeletons"}});
        transcript.Record({{"type", "ai:proposal"}, {"id", 2}, {"summary", "Undecided"}, {"items", nlohmann::json::array()}},
                          {{"user", "make skeletons faster"}, {"assistant", "Faster skeletons"}});

        // Within the same run the ids are live.
        auto live = transcript.RestoreEvents();
        ASSERT_EQ(live.size(), 4u);
        EXPECT_EQ(live[1]["id"], 1);
    }

    aige::AiTranscript reopened(file);
    reopened.Load();
    auto events = reopened.RestoreEvents();
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0]["text"], "make skeletons faster");
    // Earlier runs' ids can't clash with this run's (which restart at 1), and a
    // proposal keeps the same id as its decision.
    const int proposal = events[1]["id"].get<int>();
    EXPECT_LT(proposal, 0);
    EXPECT_EQ(events[2]["id"], proposal);
    EXPECT_NE(events[3]["id"], proposal);
    // The model's context too, so follow-ups still make sense.
    ASSERT_EQ(reopened.Context().size(), 2u);
    EXPECT_EQ(reopened.Context()[1].second, "Faster skeletons");

    // A third run tells the second run's id 1 from the first run's.
    reopened.Record({{"type", "ai:proposal"}, {"id", 1}, {"summary", "Second run"}, {"items", nlohmann::json::array()}}, {});
    aige::AiTranscript third(file);
    third.Load();
    auto all = third.RestoreEvents();
    ASSERT_EQ(all.size(), 5u);
    EXPECT_NE(all[4]["id"], all[1]["id"]);

    third.Clear();
    EXPECT_FALSE(fs::exists(file));
    aige::AiTranscript cleared(file);
    cleared.Load();
    EXPECT_TRUE(cleared.Empty());
    EXPECT_TRUE(cleared.Context().empty());
}

TEST(AiTranscriptTest, KeepsOnlyTheNewestEventsAndSurvivesABrokenFile) {
    const fs::path file = TempFile("ai_conversation.json");
    aige::AiTranscript transcript(file, 3);
    transcript.Load();
    for (int i = 0; i < 5; ++i) {
        transcript.Record({{"type", "ai:message"}, {"role", "user"}, {"text", std::to_string(i)}}, {});
    }
    auto events = transcript.RestoreEvents();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0]["text"], "2");

    std::ofstream(file) << "{ not json";
    aige::AiTranscript broken(file);
    broken.Load();
    EXPECT_TRUE(broken.Empty());
}

TEST(ArtHistoryTest, RequestsResultsAndDiscardsAreKept) {
    const fs::path file = TempFile("art_history.json");
    const nlohmann::json request = {{"name", "barrel"}, {"description", "a wooden barrel"}, {"kind", "object"},
                                    {"height", 48}, {"style", "pixel"}, {"variants", {"mossy", "broken"}}};
    {
        aige::ArtHistory history(file);
        history.Load();
        int id = history.Begin(request);
        history.Finish(id, {{"ok", true},
                            {"assets",
                             {{{"path", "assets/textures/generated/barrel-mossy.png"}},
                              {{"path", "assets/textures/generated/barrel-broken.png"}}}}});
        history.Discarded("assets/textures/generated/barrel-broken.png");
        int failed = history.Begin({{"description", "a dragon"}});
        history.Finish(failed, {{"ok", false}, {"errors", {"the image model didn't return an image"}}});
        history.Moved("assets/textures/generated/barrel-mossy.png", "assets/props/barrel.png");
        history.Begin({{"description", "still drawing"}}); // the editor closes now
    }

    aige::ArtHistory reopened(file);
    reopened.Load();
    auto entries = reopened.Entries(); // newest first
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0]["state"], "failed"); // never came back
    EXPECT_EQ(entries[1]["state"], "failed");
    EXPECT_EQ(entries[1]["errors"][0], "the image model didn't return an image");
    const auto& barrel = entries[2];
    EXPECT_EQ(barrel["state"], "done");
    EXPECT_EQ(barrel["request"], request); // everything needed to use it again
    EXPECT_FALSE(barrel["created"].get<std::string>().empty());
    ASSERT_EQ(barrel["results"].size(), 2u);
    EXPECT_EQ(barrel["results"][0]["path"], "assets/props/barrel.png");
    EXPECT_FALSE(barrel["results"][0]["discarded"].get<bool>());
    EXPECT_TRUE(barrel["results"][1]["discarded"].get<bool>());

    // New ids continue after the old ones.
    int next = reopened.Begin(request);
    EXPECT_GT(next, entries[0]["id"].get<int>());
}
