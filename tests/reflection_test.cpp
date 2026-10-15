#include <gtest/gtest.h>

#include "ecs/Registry.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Settings.h"
#include "reflection/Reflection.h"

#include <set>

namespace {

// A component and settings object defined only for these tests, so the
// contract is checked independently of the engine's own registrations.
enum class Mood { Calm, Angry, Asleep };

struct Golem {
    float weight = 100.0f;
    int level = 1;
    bool awake = false;
    std::string title = "Golem";
    Mood mood = Mood::Calm;
    aige::Entity master = entt::null;
};

struct WeatherSettings {
    float rain = 0.0f;
    bool thunder = false;
};

aige::ReflectionRegistry MakeRegistry(WeatherSettings* weather) {
    aige::ReflectionRegistry registry;
    registry.Component<Golem>("Golem", "A stone creature.")
        .Field("weight", &Golem::weight, "Kilograms.").Range(1, 1000)
        .Field("level", &Golem::level, "Experience level.").Min(1)
        .Field("awake", &Golem::awake, "Whether it moves.")
        .Field("title", &Golem::title, "Display title.")
        .Field("mood", &Golem::mood, "Temperament.").Options({"Calm", "Angry", "Asleep"})
        .Field("master", &Golem::master, "Who it obeys.");
    registry.Settings("Weather", "Sky conditions.", weather)
        .Field("rain", &WeatherSettings::rain, "Rain intensity (0-1).").Range(0, 1)
        .Field("thunder", &WeatherSettings::thunder, "Thunderstorms.");
    registry.Command("golem.wake", "Wakes a golem up.")
        .Param("golem", aige::FieldType::EntityRef, "The golem.")
        .Param("level", aige::FieldType::Int, "Level to wake at.").Range(1, 10)
        .Run([](aige::CommandContext& context, const std::vector<aige::FieldValue>& args) -> aige::CommandResult {
            aige::Entity golem = aige::ToEntity(args[0]);
            if (!context.registry.valid(golem) || !context.registry.all_of<Golem>(golem)) {
                return {false, "not a golem"};
            }
            context.registry.get<Golem>(golem).awake = true;
            context.registry.get<Golem>(golem).level = aige::ToInt(args[1]);
            return {};
        });
    return registry;
}

} // namespace

TEST(ReflectionTest, RegisteredTypeQueriesBackWithMetadata) {
    WeatherSettings weather;
    auto registry = MakeRegistry(&weather);

    const aige::ComponentInfo* golem = registry.FindComponent("Golem");
    ASSERT_NE(golem, nullptr);
    EXPECT_EQ(golem->description, "A stone creature.");
    ASSERT_EQ(golem->fields.size(), 6u);

    const aige::FieldInfo* weight = golem->Field("weight");
    ASSERT_NE(weight, nullptr);
    EXPECT_EQ(weight->type, aige::FieldType::Float);
    EXPECT_EQ(weight->description, "Kilograms.");
    EXPECT_EQ(weight->min, 1.0);
    EXPECT_EQ(weight->max, 1000.0);

    // Types are inferred from the C++ members.
    EXPECT_EQ(golem->Field("level")->type, aige::FieldType::Int);
    EXPECT_EQ(golem->Field("awake")->type, aige::FieldType::Bool);
    EXPECT_EQ(golem->Field("title")->type, aige::FieldType::String);
    EXPECT_EQ(golem->Field("mood")->type, aige::FieldType::Enum);
    EXPECT_EQ(golem->Field("master")->type, aige::FieldType::EntityRef); // not mistaken for an enum
    EXPECT_EQ(golem->Field("nope"), nullptr);
    EXPECT_EQ(registry.FindComponent("Dragon"), nullptr);
}

TEST(ReflectionTest, GettersAndSettersReachTheComponent) {
    WeatherSettings weather;
    auto reflection = MakeRegistry(&weather);
    aige::Registry registry;
    auto entity = registry.create();
    auto master = registry.create();
    ASSERT_TRUE(aige::AddComponent(reflection, registry, entity, "Golem"));
    const aige::ComponentInfo* golem = reflection.FindComponent("Golem");

    golem->Field("weight")->set(registry, entity, 250.0f);
    golem->Field("mood")->set(registry, entity, 1);
    golem->Field("master")->set(registry, entity, master);
    EXPECT_FLOAT_EQ(registry.get<Golem>(entity).weight, 250.0f);
    EXPECT_EQ(registry.get<Golem>(entity).mood, Mood::Angry);
    EXPECT_EQ(registry.get<Golem>(entity).master, master);
    EXPECT_EQ(std::get<int>(golem->Field("mood")->get(registry, entity)), 1);
}

TEST(ReflectionTest, ValidationEnforcesTypesAndConstraints) {
    WeatherSettings weather;
    auto registry = MakeRegistry(&weather);
    const aige::ComponentInfo* golem = registry.FindComponent("Golem");

    EXPECT_FALSE(aige::Validate(*golem->Field("weight"), 500.0f));
    EXPECT_FALSE(aige::Validate(*golem->Field("weight"), 500)); // ints are fine for floats
    EXPECT_TRUE(aige::Validate(*golem->Field("weight"), 0.5f)); // below min
    EXPECT_TRUE(aige::Validate(*golem->Field("weight"), 5000.0f)); // above max
    EXPECT_TRUE(aige::Validate(*golem->Field("weight"), std::string("heavy")));
    EXPECT_TRUE(aige::Validate(*golem->Field("level"), 2.5f));  // not whole
    EXPECT_FALSE(aige::Validate(*golem->Field("level"), 3.0f)); // whole: fine
    EXPECT_TRUE(aige::Validate(*golem->Field("level"), 0));     // below min
    EXPECT_TRUE(aige::Validate(*golem->Field("awake"), 1));     // bools must be bools
    EXPECT_TRUE(aige::Validate(*golem->Field("mood"), 7));      // no such option
    EXPECT_FALSE(aige::Validate(*golem->Field("mood"), 2));

    auto error = aige::Validate(*golem->Field("weight"), 0.5f);
    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->find("at least 1"), std::string::npos); // readable messages
}

TEST(ReflectionTest, JsonValuesRoundTrip) {
    WeatherSettings weather;
    auto registry = MakeRegistry(&weather);
    const aige::ComponentInfo* golem = registry.FindComponent("Golem");

    EXPECT_EQ(aige::ValueToJson(*golem->Field("mood"), 1), "Angry"); // enums by name
    auto mood = aige::ValueFromJson(*golem->Field("mood"), "Asleep");
    ASSERT_TRUE(mood.has_value());
    EXPECT_EQ(std::get<int>(*mood), 2);
    EXPECT_FALSE(aige::ValueFromJson(*golem->Field("mood"), "Sad").has_value());
    EXPECT_FALSE(aige::ValueFromJson(*golem->Field("weight"), "heavy").has_value());
    EXPECT_TRUE(aige::ValueToJson(*golem->Field("master"), aige::Entity{entt::null}).is_null());
    EXPECT_EQ(std::get<int>(*aige::ValueFromJson(*golem->Field("level"), 4.0)), 4); // 4.0 is a whole number
    EXPECT_FALSE(aige::ValueFromJson(*golem->Field("level"), 4.5).has_value());
}

TEST(ReflectionTest, SettingsBindToTheirInstance) {
    WeatherSettings weather;
    auto registry = MakeRegistry(&weather);
    const aige::SettingsInfo* section = registry.FindSettings("Weather");
    ASSERT_NE(section, nullptr);
    section->Field("rain")->set(0.75f);
    section->Field("thunder")->set(true);
    EXPECT_FLOAT_EQ(weather.rain, 0.75f);
    EXPECT_TRUE(weather.thunder);
    EXPECT_FLOAT_EQ(std::get<float>(section->Field("rain")->get()), 0.75f);
    EXPECT_TRUE(aige::Validate(*section->Field("rain"), 2.0f));
}

TEST(ReflectionTest, CommandsValidateArgumentsBeforeRunning) {
    WeatherSettings weather;
    auto reflection = MakeRegistry(&weather);
    aige::Registry registry;
    auto golem = registry.create();
    registry.emplace<Golem>(golem);
    aige::CommandContext context{registry};

    auto ok = reflection.Run(context, "golem.wake", {golem, 5});
    EXPECT_TRUE(ok.ok) << ok.error;
    EXPECT_TRUE(registry.get<Golem>(golem).awake);
    EXPECT_EQ(registry.get<Golem>(golem).level, 5);

    EXPECT_FALSE(reflection.Run(context, "golem.wake", {golem, 50}).ok);      // out of range
    EXPECT_FALSE(reflection.Run(context, "golem.wake", {golem}).ok);          // missing argument
    EXPECT_FALSE(reflection.Run(context, "golem.wake", {std::string("x"), 2}).ok); // wrong type
    EXPECT_FALSE(reflection.Run(context, "golem.dance", {}).ok);              // unknown command
    auto notAGolem = reflection.Run(context, "golem.wake", {registry.create(), 2});
    EXPECT_FALSE(notAGolem.ok);
    EXPECT_EQ(notAGolem.error, "not a golem");
}

TEST(ReflectionTest, SchemaExportDescribesEverything) {
    WeatherSettings weather;
    auto registry = MakeRegistry(&weather);
    nlohmann::json schema = registry.ExportSchema();

    ASSERT_EQ(schema["components"].size(), 1u);
    const auto& golem = schema["components"][0];
    EXPECT_EQ(golem["name"], "Golem");
    EXPECT_EQ(golem["fields"][0]["type"], "float");
    EXPECT_EQ(golem["fields"][0]["min"], 1.0);
    EXPECT_EQ(golem["fields"][4]["options"][1], "Angry");
    EXPECT_EQ(schema["settings"][0]["fields"][1]["type"], "bool");
    EXPECT_EQ(schema["commands"][0]["params"][1]["max"], 10.0);
}

// --- The engine's own registrations --------------------------------------------------

TEST(EngineReflectionTest, EveryComponentAndFieldIsDocumented) {
    for (const auto& component : aige::EngineReflection().Components()) {
        EXPECT_FALSE(component.description.empty()) << component.name;
        for (const auto& field : component.fields) {
            EXPECT_FALSE(field.description.empty()) << component.name << "." << field.name;
            if (field.type == aige::FieldType::Enum) {
                EXPECT_FALSE(field.options.empty()) << component.name << "." << field.name;
            }
        }
    }
    for (const auto& section : aige::EngineReflection().AllSettings()) {
        EXPECT_FALSE(section.description.empty()) << section.name;
        for (const auto& field : section.fields) {
            EXPECT_FALSE(field.description.empty()) << section.name << "." << field.name;
        }
    }
}

TEST(EngineReflectionTest, DefaultValuesAreValid) {
    // A freshly added component must already satisfy its own constraints,
    // or the Inspector would show errors on things the user never touched.
    for (const auto& component : aige::EngineReflection().Components()) {
        aige::Registry registry;
        auto entity = registry.create();
        aige::AddComponent(registry, entity, component.name);
        for (const auto& field : component.fields) {
            auto error = aige::Validate(field, field.get(registry, entity));
            EXPECT_FALSE(error.has_value()) << component.name << "." << field.name << ": " << error.value_or("");
        }
    }
    for (const auto& section : aige::EngineReflection().AllSettings()) {
        for (const auto& field : section.fields) {
            EXPECT_FALSE(aige::Validate(field, field.get()).has_value()) << section.name << "." << field.name;
        }
    }
}

TEST(EngineReflectionTest, SettingsReachTheActiveProjectSettings) {
    const aige::SettingsInfo* combat = aige::EngineReflection().FindSettings("Combat");
    ASSERT_NE(combat, nullptr);
    float before = aige::ActiveSettings().combat.knockbackScale;
    combat->Field("knockbackScale")->set(2.5f);
    EXPECT_FLOAT_EQ(aige::ActiveSettings().combat.knockbackScale, 2.5f);
    combat->Field("knockbackScale")->set(before);
}
