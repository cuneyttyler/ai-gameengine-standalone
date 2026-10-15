#include "ai_bridge/ReviewPolicy.h"

namespace aige {

ReviewPolicy ParseReviewPolicy(const std::string& name) {
    return name == "autoApplyTweaks" ? ReviewPolicy::AutoApplyTweaks : ReviewPolicy::ReviewAll;
}

const char* ReviewPolicyName(ReviewPolicy policy) {
    return policy == ReviewPolicy::AutoApplyTweaks ? "autoApplyTweaks" : "reviewAll";
}

std::optional<nlohmann::json> InverseOperation(const ChangeItem& item) {
    if (!item.error.empty() || item.before.is_null()) {
        return std::nullopt;
    }
    const nlohmann::json& op = item.op;
    if (item.kind == "field") {
        return nlohmann::json{{"op", "set_field"},
                              {"entity", op["entity"]},
                              {"component", op["component"]},
                              {"field", op["field"]},
                              {"value", item.before}};
    }
    if (item.kind == "setting") {
        return nlohmann::json{
            {"op", "set_setting"}, {"section", op["section"]}, {"field", op["field"]}, {"value", item.before}};
    }
    if (item.kind == "script_property") {
        return nlohmann::json{
            {"op", "set_script_property"}, {"entity", op["entity"]}, {"name", op["name"]}, {"value", item.before}};
    }
    return std::nullopt;
}

bool AppliesWithoutReview(const ChangeItem& item, ReviewPolicy policy) {
    if (policy != ReviewPolicy::AutoApplyTweaks || !item.error.empty()) {
        return false;
    }
    if (item.kind != "field" && item.kind != "setting" && item.kind != "script_property") {
        return false;
    }
    // Which script runs, or which prefab a copy belongs to, isn't a tweak.
    const std::string component = item.op.value("component", "");
    if (item.kind == "field" && (component == "LuaScript" || component == "PrefabInstance")) {
        return false;
    }
    return InverseOperation(item).has_value();
}

} // namespace aige
