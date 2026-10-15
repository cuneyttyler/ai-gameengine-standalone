#include <gtest/gtest.h>

#include "ai_bridge/AiClient.h"
#include "ai_bridge/ContextBuilder.h"
#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "editing/Commands.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"

#include <httplib.h>

#include <chrono>
#include <thread>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace {

// Waits (briefly) for the client to report something.
std::optional<aige::AiClient::Result> WaitForResult(aige::AiClient& client, int timeoutMs = 5000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = client.Poll()) {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

// A stand-in for the Python service: same endpoints, canned answers.
class FakeService {
public:
    explicit FakeService(bool withProvider = true) {
        server_.Get("/health", [withProvider](const httplib::Request&, httplib::Response& response) {
            nlohmann::json body = {{"ok", true}, {"schemaLoaded", false}};
            body["provider"] = withProvider ? nlohmann::json("fake") : nlohmann::json(nullptr);
            body["model"] = withProvider ? nlohmann::json("fake-model") : nlohmann::json(nullptr);
            response.set_content(body.dump(), "application/json");
        });
        server_.Put("/schema", [this](const httplib::Request& request, httplib::Response& response) {
            receivedSchema = nlohmann::json::parse(request.body);
            response.set_content(R"({"ok":true})", "application/json");
        });
        server_.Post("/propose", [this](const httplib::Request& request, httplib::Response& response) {
            lastRequest = nlohmann::json::parse(request.body);
            nlohmann::json body = {{"ok", true},
                                   {"proposal",
                                    {{"summary", "echo: " + lastRequest["instruction"].get<std::string>()},
                                     {"operations", nlohmann::json::array()}}}};
            response.set_content(body.dump(), "application/json");
        });
        port = server_.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }
    ~FakeService() {
        server_.stop();
        thread_.join();
    }

    int port = 0;
    nlohmann::json receivedSchema;
    nlohmann::json lastRequest;

private:
    httplib::Server server_;
    std::thread thread_;
};

} // namespace

TEST(AiClientTest, ConnectsSendsSchemaAndProposes) {
    FakeService service;
    aige::AiClient::Options options;
    options.port = service.port; // already running: nothing to launch
    aige::AiClient client(options);

    client.Start({{"version", 1}, {"components", nlohmann::json::array()}});
    auto status = WaitForResult(client);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->kind, aige::AiClient::Result::Kind::Status);
    EXPECT_EQ(client.GetState(), aige::AiClient::State::Ready) << client.StatusText();
    EXPECT_NE(client.StatusText().find("fake-model"), std::string::npos);
    EXPECT_EQ(service.receivedSchema["version"], 1);

    int id = client.Propose("make it rain", {{"entities", nlohmann::json::array()}});
    auto answer = WaitForResult(client);
    ASSERT_TRUE(answer.has_value());
    EXPECT_EQ(answer->kind, aige::AiClient::Result::Kind::Proposal);
    EXPECT_EQ(answer->requestId, id);
    EXPECT_EQ(answer->body["proposal"]["summary"], "echo: make it rain");
    EXPECT_EQ(service.lastRequest["context"]["entities"], nlohmann::json::array());
}

TEST(AiClientTest, ReportsAMissingProvider) {
    FakeService service(/*withProvider=*/false);
    aige::AiClient::Options options;
    options.port = service.port;
    aige::AiClient client(options);
    client.Start({{"version", 1}});
    ASSERT_TRUE(WaitForResult(client).has_value());
    EXPECT_EQ(client.GetState(), aige::AiClient::State::Failed);
    EXPECT_NE(client.StatusText().find("MISTRAL_API_KEY"), std::string::npos);

    // Requests while not ready fail politely instead of hanging.
    client.Propose("anything", {});
    auto answer = WaitForResult(client);
    ASSERT_TRUE(answer.has_value());
    EXPECT_FALSE(answer->body["ok"].get<bool>());
}

TEST(AiClientTest, UnreachableServiceWithNothingToLaunchFails) {
    aige::AiClient::Options options;
    options.port = 1; // nothing listens here
    aige::AiClient client(options);
    client.Start({{"version", 1}});
    ASSERT_TRUE(WaitForResult(client).has_value());
    EXPECT_EQ(client.GetState(), aige::AiClient::State::Failed);
}

TEST(ContextBuilderTest, DescribesTheDemoProject) {
    aige::Project project{std::filesystem::path(AIGE_SOURCE_DIR) / "projects" / "demo"};
    aige::AssetDatabase assets(project);
    assets.Refresh();
    aige::PrefabLibrary prefabs(project);
    prefabs.Refresh();
    aige::Registry registry;
    ASSERT_TRUE(aige::LoadScene(registry, project.MainScenePath().string()));
    auto player = *aige::FindPlayer(registry);

    nlohmann::json context = aige::BuildEditorContext(registry, aige::EngineReflection(), prefabs, assets, project,
                                                      {player, "Hero.Health.max"});

    // Tiles are summarized, everything else listed with its values.
    EXPECT_EQ(context["tiles"]["count"], 280);
    EXPECT_EQ(context["tiles"]["bounds"], nlohmann::json({0, 0, 19, 13}));
    EXPECT_EQ(context["tiles"]["byPrefab"]["prefabs/hedge.prefab"].get<int>() > 0, true);
    const auto& entities = context["entities"];
    auto hero = std::find_if(entities.begin(), entities.end(), [](const auto& e) { return e.value("name", "") == "Hero"; });
    ASSERT_NE(hero, entities.end());
    EXPECT_EQ((*hero)["prefab"], "prefabs/hero.prefab");
    EXPECT_EQ((*hero)["components"]["Faction"]["team"], "Player"); // enums by name
    EXPECT_FALSE((*hero)["components"].contains("ScreenTransform2D")); // derived noise left out
    EXPECT_EQ(context["selection"], static_cast<uint32_t>(player));
    EXPECT_EQ(context["focus"], "Hero.Health.max");

    // Prefabs carry their values (minus per-copy fields), scripts and settings are listed.
    auto skeleton = std::find_if(context["prefabs"].begin(), context["prefabs"].end(),
                                 [](const auto& p) { return p["name"] == "skeleton"; });
    ASSERT_NE(skeleton, context["prefabs"].end());
    EXPECT_TRUE((*skeleton)["components"].contains("MeleeAttack"));
    EXPECT_TRUE((*skeleton)["components"]["WorldTransform2D"].empty()); // has one, but position is per copy
    EXPECT_NE(std::find(context["scripts"].begin(), context["scripts"].end(), "scripts/enemy_melee.lua"),
              context["scripts"].end());
    EXPECT_TRUE(context["settings"]["Combat"].contains("knockbackScale"));
    EXPECT_GE(context["assets"].size(), 13u);
}
