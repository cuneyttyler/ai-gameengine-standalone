#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "modules/Modules.h"
#include "records/Records.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md 12.1: records, typed game data in records/<type>/<id>.json.
namespace {

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

nlohmann::json Read(const fs::path& path) {
    std::ifstream in(path);
    return nlohmann::json::parse(in);
}

// A registry with the engine's types and a test type that uses every kind of field.
aige::ReflectionRegistry MakeRegistry() {
    aige::ReflectionRegistry r;
    aige::RegisterGameplayTypes(r);
    r.RecordType("droptable", "A weighted list of items")
        .Field("name", aige::FieldType::String, "label")
        .Required()
        .Object("entries",
                {aige::MakeRecordRef("item", "item", "what drops"),
                 aige::MakeRecordValue("weight", aige::FieldType::Float, "how likely")},
                "the entries")
        .List()
        .Ref("fallback", "droptable", "another table")
        .Field("rolls", aige::FieldType::Int, "how many")
        .Min(1)
        .Max(10);
    r.FindRecordType("droptable");
    return r;
}

std::vector<std::string> Messages(const aige::RecordDatabase& db, const std::string& key) {
    std::vector<std::string> out;
    for (const auto& p : db.Problems()) {
        if (p.key == key) out.push_back(p.message);
    }
    return out;
}

bool Has(const std::vector<std::string>& messages, const std::string& part) {
    for (const auto& m : messages) {
        if (m.find(part) != std::string::npos) return true;
    }
    return false;
}

class RecordsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_records_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }
    void TearDown() override { fs::remove_all(root_); }
    fs::path root_;
};

} // namespace

TEST(RecordTypesTest, TheEngineRegistersItemAndTheSchemaCarriesRecordTypes) {
    const aige::RecordTypeInfo* item = aige::EngineReflection().FindRecordType("item");
    ASSERT_NE(item, nullptr);
    ASSERT_NE(item->Field("name"), nullptr);
    EXPECT_TRUE(item->Field("name")->required);
    EXPECT_TRUE(item->Field("tags")->list);
    EXPECT_EQ(aige::DefaultRecord(*item)["stack"], 1);

    auto schema = MakeRegistry().ExportSchema();
    ASSERT_TRUE(schema.contains("records"));
    nlohmann::json loot;
    for (const auto& t : schema["records"]) {
        if (t["name"] == "droptable") loot = t;
    }
    ASSERT_FALSE(loot.is_null());
    EXPECT_EQ(loot["fields"][1]["type"], "object");
    EXPECT_TRUE(loot["fields"][1]["list"].get<bool>());
    EXPECT_EQ(loot["fields"][1]["fields"][0]["type"], "record");
    EXPECT_EQ(loot["fields"][1]["fields"][0]["recordType"], "item");
    EXPECT_EQ(loot["fields"][2]["recordType"], "droptable");
}

TEST(RecordTypesTest, ValidationNamesTheFieldPath) {
    auto r = MakeRegistry();
    const auto& loot = *r.FindRecordType("droptable");
    auto exists = [](const std::string& type, const std::string& id) { return type == "item" && id == "sword"; };
    auto problems = aige::ValidateRecord(
        loot,
        nlohmann::json::parse(R"({"entries": [{"item": "sword", "weight": 1}, {"item": "axe", "weight": "lots"}],
                                  "rolls": 11, "colour": "red"})"),
        exists);
    EXPECT_TRUE(Has(problems, "entries[1].item: no item 'axe'"));
    EXPECT_TRUE(Has(problems, "entries[1].weight: expected a number"));
    EXPECT_TRUE(Has(problems, "rolls: 11 is above the maximum 10"));
    EXPECT_TRUE(Has(problems, "colour: unknown field"));
    EXPECT_TRUE(Has(problems, "name: required"));
    EXPECT_FALSE(Has(problems, "entries[0]"));
    // Enums by option name, whole numbers for ints.
    const auto& item = *r.FindRecordType("item");
    problems = aige::ValidateRecord(item, nlohmann::json::parse(R"({"name": "x", "kind": "spoon", "stack": 1.5})"), nullptr);
    EXPECT_TRUE(Has(problems, "kind: \"spoon\" isn't one of misc"));
    EXPECT_TRUE(Has(problems, "stack: expected a whole number"));
}

TEST_F(RecordsTest, LoadsResolvesBasesAndReportsProblems) {
    auto r = MakeRegistry();
    Write(root_ / "records/item/sword.json", R"({"name": "Sword", "kind": "weapon", "value": 10, "tags": ["blade"]})");
    Write(root_ / "records/item/fine-sword.json", R"({"base": "sword", "name": "Fine sword", "value": 25})");
    Write(root_ / "records/item/loop-a.json", R"({"base": "loop-b", "name": "A"})");
    Write(root_ / "records/item/loop-b.json", R"({"base": "loop-a", "name": "B"})");
    Write(root_ / "records/item/broken.json", "{ not json");
    Write(root_ / "records/item/iconic.json", R"({"name": "Iconic", "icon": "assets/missing.png"})");
    Write(root_ / "records/droptable/chest.json", R"({"name": "Chest", "entries": [{"item": "sword", "weight": 2},
                                                {"item": "ghost", "weight": 1}], "fallback": "nope"})");
    Write(root_ / "records/spells/fire.json", R"({"name": "Fire"})");
    Write(root_ / "records/item/Bad Name.json", R"({"name": "x"})");

    aige::RecordDatabase db;
    db.Open(r, root_);
    const aige::Record* fine = db.Find("item", "fine-sword");
    ASSERT_NE(fine, nullptr);
    EXPECT_EQ(fine->resolved["name"], "Fine sword");
    EXPECT_EQ(fine->resolved["value"], 25);
    EXPECT_EQ(fine->resolved["kind"], "weapon");             // from the base
    EXPECT_EQ(fine->resolved["tags"][0], "blade");           // from the base
    EXPECT_EQ(fine->resolved["stack"], 1);                   // the default
    EXPECT_FALSE(fine->data.contains("kind"));               // the file is untouched
    EXPECT_EQ(db.OfType("item").front()->id, "fine-sword");  // sorted by id

    EXPECT_TRUE(Has(Messages(db, "item/loop-a"), "leads back to this record"));
    EXPECT_TRUE(Has(Messages(db, "item/broken"), "isn't valid JSON"));
    EXPECT_TRUE(Has(Messages(db, "item/iconic"), "icon: assets/missing.png doesn't exist"));
    auto chest = Messages(db, "droptable/chest");
    EXPECT_TRUE(Has(chest, "entries[1].item: no item 'ghost'"));
    EXPECT_TRUE(Has(chest, "fallback: no droptable 'nope'"));
    EXPECT_TRUE(Has(Messages(db, "records/spells/fire.json"), "isn't a known record type"));
    EXPECT_TRUE(Has(Messages(db, "records/item/Bad Name.json"), "isn't a valid record id"));
    EXPECT_TRUE(Messages(db, "item/sword").empty());

    auto users = db.ReferencesTo("item", "sword");
    EXPECT_EQ(users, (std::vector<std::string>{"droptable/chest", "item/fine-sword"}));
}

TEST_F(RecordsTest, WriteRefusesSchemaErrorsButAllowsForwardReferences) {
    auto r = MakeRegistry();
    aige::RecordDatabase db;
    db.Open(r, root_);
    EXPECT_TRUE(db.Write("item", "x", nlohmann::json::parse(R"({"value": -1})")).has_value()); // name missing, value < 0
    EXPECT_TRUE(db.Write("nothing", "x", nlohmann::json::object()).has_value());
    EXPECT_TRUE(db.Write("item", "Bad Id", nlohmann::json::parse(R"({"name": "x"})")).has_value());
    EXPECT_FALSE(fs::exists(root_ / "records/item/x.json"));

    // A loot table may name an item that's written afterwards.
    EXPECT_FALSE(db.Write("droptable", "chest", nlohmann::json::parse(R"({"name": "Chest", "entries": [{"item": "gem", "weight": 1}]})")));
    EXPECT_TRUE(Has(Messages(db, "droptable/chest"), "no item 'gem'"));
    EXPECT_FALSE(db.Write("item", "gem", nlohmann::json::parse(R"({"name": "Gem", "value": 50})")));
    EXPECT_TRUE(Messages(db, "droptable/chest").empty());
    // A required field may come from the base.
    EXPECT_FALSE(db.Write("item", "big-gem", nlohmann::json::parse(R"({"base": "gem", "value": 90})")));
    EXPECT_EQ(db.Find("item", "big-gem")->resolved["name"], "Gem");
    EXPECT_EQ(Read(root_ / "records/item/big-gem.json")["value"], 90);
}

TEST_F(RecordsTest, RenameRewritesReferencesAndBases) {
    auto r = MakeRegistry();
    aige::RecordDatabase db;
    db.Open(r, root_);
    ASSERT_FALSE(db.Write("item", "gem", nlohmann::json::parse(R"({"name": "Gem"})")));
    ASSERT_FALSE(db.Write("item", "big-gem", nlohmann::json::parse(R"({"base": "gem"})")));
    ASSERT_FALSE(db.Write("droptable", "chest", nlohmann::json::parse(R"({"name": "Chest", "entries": [{"item": "gem", "weight": 1}]})")));
    EXPECT_TRUE(db.Rename("item", "gem", "big-gem").has_value()); // taken
    EXPECT_FALSE(db.Rename("item", "gem", "ruby"));
    EXPECT_EQ(db.Find("item", "gem"), nullptr);
    ASSERT_NE(db.Find("item", "ruby"), nullptr);
    EXPECT_EQ(Read(root_ / "records/droptable/chest.json")["entries"][0]["item"], "ruby");
    EXPECT_EQ(Read(root_ / "records/item/big-gem.json")["base"], "ruby");
    EXPECT_TRUE(db.Problems().empty());

    EXPECT_FALSE(db.Remove("droptable", "chest"));
    EXPECT_FALSE(fs::exists(root_ / "records/droptable/chest.json"));
    EXPECT_TRUE(db.Remove("droptable", "chest").has_value());
}

TEST_F(RecordsTest, ModulesDeclareRecordTypes) {
    Write(root_ / "modules/magic/module.json", R"({"records": [
        {"name": "spell", "description": "A spell", "fields": [
          {"name": "name", "type": "string", "required": true},
          {"name": "school", "type": "enum", "options": ["fire", "frost"]},
          {"name": "reagent", "type": "record", "recordType": "item"},
          {"name": "effects", "type": "object", "list": true,
           "fields": [{"name": "magnitude", "type": "float", "min": 0}]}]},
        {"name": "Bad Type", "fields": []},
        {"name": "item", "fields": []},
        {"name": "broken", "fields": [{"name": "x", "type": "record"}]}]})");
    aige::ReflectionRegistry r;
    aige::RegisterGameplayTypes(r);
    auto result = aige::LoadModules(r, root_);
    ASSERT_EQ(result.modules.size(), 1u);
    EXPECT_EQ(result.modules[0].records, (std::vector<std::string>{"spell"}));
    EXPECT_EQ(result.errors.size(), 3u);
    const aige::RecordTypeInfo* spell = r.FindRecordType("spell");
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->source, "magic");
    EXPECT_EQ(spell->Field("reagent")->kind, aige::RecordFieldKind::Record);
    EXPECT_EQ(spell->Field("effects")->fields[0].min, 0.0);

    aige::RecordDatabase db;
    db.Open(r, root_);
    EXPECT_FALSE(db.Write("spell", "ignite", nlohmann::json::parse(R"({"name": "Ignite", "school": "fire",
        "effects": [{"magnitude": 3}]})")));
    EXPECT_TRUE(db.Write("spell", "bad", nlohmann::json::parse(R"({"name": "Bad", "effects": [{"magnitude": -1}]})")).has_value());
}

TEST_F(RecordsTest, ProposalsWriteAndDeleteRecords) {
    aige::Project project{root_};
    aige::PrefabLibrary prefabs(project);
    aige::Registry registry;
    auto& reflection = aige::EngineReflection();
    nlohmann::json proposal = {
        {"summary", "items"},
        {"operations",
         {{{"op", "write_record"}, {"type", "item"}, {"id", "potion"}, {"record", {{"name", "Potion"}, {"kind", "consumable"}}}},
          {{"op", "write_record"}, {"type", "item"}, {"id", "big-potion"}, {"record", {{"base", "potion"}, {"value", 20}}}},
          {{"op", "write_record"}, {"type", "item"}, {"id", "junk"}, {"record", {{"name", "Junk"}, {"stack", 0}}}},
          {{"op", "write_record"}, {"type", "spell"}, {"id", "x"}, {"record", nlohmann::json::object()}},
          {{"op", "delete_record"}, {"type", "item"}, {"id", "ghost"}}}}};
    auto resolved = aige::ResolveProposal(proposal, registry, reflection, prefabs, project);
    ASSERT_EQ(resolved.items.size(), 5u);
    EXPECT_EQ(resolved.items[0].error, "");
    EXPECT_EQ(resolved.items[0].kind, "record");
    EXPECT_TRUE(resolved.items[0].touchesFiles);
    EXPECT_EQ(resolved.items[1].error, ""); // its base is written by the same proposal
    EXPECT_NE(resolved.items[2].error.find("stack"), std::string::npos);
    EXPECT_NE(resolved.items[3].error.find("unknown record type"), std::string::npos);
    EXPECT_NE(resolved.items[4].error.find("no item 'ghost'"), std::string::npos);
    EXPECT_FALSE(fs::exists(root_ / "records")); // resolving writes nothing

    std::vector<nlohmann::json> accepted = {proposal["operations"][0], proposal["operations"][1]};
    auto outcome = aige::ApplyOperations(accepted, registry, reflection, prefabs, project);
    EXPECT_EQ(outcome.applied, 2);
    EXPECT_TRUE(outcome.errors.empty());
    aige::RecordDatabase db;
    db.Open(reflection, root_);
    ASSERT_NE(db.Find("item", "big-potion"), nullptr);
    EXPECT_EQ(db.Find("item", "big-potion")->resolved["name"], "Potion");

    outcome = aige::ApplyOperations({{{"op", "delete_record"}, {"type", "item"}, {"id", "potion"}}}, registry, reflection,
                                    prefabs, project);
    EXPECT_EQ(outcome.applied, 1);
    EXPECT_FALSE(fs::exists(root_ / "records/item/potion.json"));
}

TEST_F(RecordsTest, LuaReadsRecords) {
    Write(root_ / "records/item/sword.json", R"({"name": "Sword", "value": 10, "tags": ["blade", "iron"]})");
    Write(root_ / "records/item/fine.json", R"({"base": "sword", "name": "Fine"})");
    aige::RecordDatabase& active = aige::ActiveRecords();
    active.Open(aige::EngineReflection(), root_);

    Write(root_ / "scripts/read.lua", R"(
        local fine = record("item", "fine")
        local text = fine.name .. "|" .. fine.value .. "|" .. fine.tags[2] .. "|" ..
                     tostring(record("item", "nope") == nil) .. "|" .. #records("item")
        set_field(self_id(), "Name", "value", text)
    )");
    aige::LuaVM lua;
    aige::Registry registry;
    auto entity = registry.create();
    registry.emplace<aige::Name>(entity);
    registry.emplace<aige::LuaScript>(entity).path = "scripts/read.lua";
    aige::GameSession session(registry, lua, aige::IsoParams{}, root_);
    session.Advance(aige::GameSession::kFixedDt, [](aige::Key) { return false; });
    EXPECT_TRUE(session.ScriptErrors().empty()) << (session.ScriptErrors().empty() ? "" : session.ScriptErrors()[0].message);
    EXPECT_EQ(registry.get<aige::Name>(entity).value, "Fine|10|iron|true|2");
    active.Close();
}
