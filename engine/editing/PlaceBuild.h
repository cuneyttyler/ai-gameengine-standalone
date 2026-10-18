#pragma once

#include "assets/Catalog.h"
#include "places/Brief.h"
#include "places/Ontology.h"
#include "places/PlaceValidate.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace aige {

// One build, from the request to the confirmed Brief (development_plan.md
// 7.4; the Plan, Build and Result steps follow in 7.5-7.7). The editor's
// Build panel shows its State(); the AI panel shows its Card().
//
//   Start(query)  -> interpreting: the AI drafts Briefs from several readings
//                 -> each draft is validated; one with errors is repaired once
//                 -> merged (disputed and guessed fields marked), completed
//   request       ① Request: round 1 of the window; Edit() changes fields
//   assets        ② Assets: round 2 and the asset plan
//   Confirm()     -> planning: the Brief is saved, its answers become the
//                    project's defaults
//
// Everything the AI was asked and answered (its thinking too) goes to
// places/<id>/log.jsonl; the user never sees the thinking.
class PlaceBuild {
public:
    struct Host {
        std::function<const Ontology&()> ontology;
        std::function<Catalog()> catalog;
        std::function<bool()> isoGrid;
        // Sends a request to the AI service; the answer comes to OnResponse
        // with the id returned. 0: the AI isn't available.
        std::function<int(const std::string& path, const nlohmann::json& body, int timeoutSeconds)> request;
        std::filesystem::path projectRoot;
    };

    explicit PlaceBuild(Host host) : host_(std::move(host)) {}

    // Starts interpreting `query`. Returns why it can't, or "".
    std::string Start(const std::string& query);
    // Whether the AI answer `requestId` is this build's.
    bool Owns(int requestId) const { return pending_.contains(requestId); }
    void OnResponse(int requestId, const nlohmann::json& body);

    // A field changed in the window.
    void Edit(const std::string& path, const nlohmann::json& value);
    // Moves to "request" or "assets". Returns why not (errors to fix), or "".
    std::string Step(const std::string& step);
    // The Assets step's Build: saves the Brief. Returns why not, or "".
    std::string Confirm();
    void Cancel();

    const std::string& Id() const { return id_; }
    const std::string& Stage() const { return stage_; }
    const nlohmann::json& Brief() const { return brief_; }
    const BriefMerge& Merge() const { return merge_; }
    bool Active() const { return stage_ != "cancelled" && stage_ != "failed" && !stage_.empty(); }
    std::filesystem::path Folder() const { return host_.projectRoot / "places" / id_; }

    // For the Build panel: {"type": "build:state", ...}.
    nlohmann::json State();
    // For the AI panel: {"type": "build:card", ...}.
    nlohmann::json Card() const;

private:
    struct Pending {
        std::string kind; // interpret, repair
        int draft = -1;
    };
    void Log(nlohmann::json entry) const;
    void FinishInterpreting();
    nlohmann::json Defaults() const;

    Host host_;
    std::string id_, query_, stage_, status_;
    int round_ = 1;
    std::vector<nlohmann::json> drafts_;
    std::vector<std::vector<PlaceProblem>> draftProblems_;
    std::map<int, Pending> pending_;
    nlohmann::json context_;
    nlohmann::json brief_;
    BriefMerge merge_;
    std::vector<std::string> errors_;
};

// "Build a large village by a river" -> "large-village-by-river", unique in places/.
std::string PlaceIdFor(const std::filesystem::path& projectRoot, const std::string& query);

} // namespace aige
