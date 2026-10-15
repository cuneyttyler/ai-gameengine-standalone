#pragma once

#include "ai_bridge/Proposal.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace aige {

// Which parts of an AI proposal apply without review (development_plan.md
// 2.8). A per-user preference; the default reviews everything.
//
// Only "small tweaks" can ever skip review: a new value for an existing
// field, setting or script property — and only when it can be put back
// exactly (InverseOperation). Anything structural or file-writing (adding or
// removing components, spawning, painting, deleting, scripts, prefab edits)
// is always reviewed.
enum class ReviewPolicy { ReviewAll, AutoApplyTweaks };

ReviewPolicy ParseReviewPolicy(const std::string& name); // unknown -> ReviewAll
const char* ReviewPolicyName(ReviewPolicy policy);       // "reviewAll" | "autoApplyTweaks"

bool AppliesWithoutReview(const ChangeItem& item, ReviewPolicy policy);

// The operation that restores what `item` changes, built from its resolved
// before-value; nullopt when the change can't be reversed by an operation
// (e.g. a script property that didn't exist before).
std::optional<nlohmann::json> InverseOperation(const ChangeItem& item);

} // namespace aige
