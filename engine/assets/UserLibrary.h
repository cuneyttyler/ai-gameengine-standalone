#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace aige {

// The user's own asset library (development_plan.md 10, docs/asset_management.md): art kept
// outside any project, in folders the user makes, reachable from every project and session.
// Everything the Art tool generates is kept here too (it's never lost with a project), and
// can be offered to other users through the AIGE library once its web service exists.
//
//   <dir>/library.json   {"schema": "aige.user-library/1", "folders": [{id, name, parent}],
//                         "items": [{id, name, file, folder, source, licence, author, url,
//                         origin, created, shared, hash, meta}], "sharing": "unset" | "ask" |
//                         "always" | "never"}
//   <dir>/files/         the images (with their .meta and .gen.json beside them)
//
// An item's source: generated, web, aige (the AIGE library), project (added from a project).
// Its shared: "unasked", "yes" (offered: uploaded when the service exists), "no".
class UserLibrary {
public:
    explicit UserLibrary(std::filesystem::path dir);

    // The whole, for the Library tab: folders, items (with their file's path under the
    // library's host), the sharing choice and whether to ask it now.
    nlohmann::json State() const;

    // A file into the library (copied, with its .meta and .gen.json): the same picture twice is
    // one item (by its content). Returns the item's id, or "" if the file can't be read.
    std::string Add(const std::filesystem::path& file, const nlohmann::json& info, const std::string& folder = {});

    // Folders: made, renamed, removed (their items and folders go to the parent).
    std::string NewFolder(const std::string& name, const std::string& parent = {});
    std::string RenameFolder(const std::string& id, const std::string& name);
    std::string DeleteFolder(const std::string& id);
    // Items: moved to a folder ("" the top), renamed, removed (the file too), shared or not.
    std::string Move(const std::string& item, const std::string& folder);
    std::string Rename(const std::string& item, const std::string& name);
    std::string Remove(const std::string& item);
    std::string SetShared(const std::string& item, const std::string& shared);
    // The default for generated art: "always" (offered), "ask" (each decided), "never".
    std::string SetSharing(const std::string& choice);

    // An item into a project (assets/library/<file>, with its .meta): the project path, or "".
    std::string CopyToProject(const std::string& item, const std::filesystem::path& projectRoot) const;

    // The project's generated art (assets/textures/generated) that isn't in the library yet,
    // kept in the "Generated" folder; how many were added.
    int KeepGenerated(const std::filesystem::path& projectRoot);

    // A folder of it (or all, `folder` empty) written out as <to>/<name>/ (library.json and its
    // files), and such a folder read back in (into a folder named after it). "" when done.
    std::string Export(const std::string& folder, const std::filesystem::path& to) const;
    std::string Import(const std::filesystem::path& from);

    const std::filesystem::path& Dir() const { return dir_; }

private:
    nlohmann::json Load() const;
    void Save(const nlohmann::json& library) const;
    std::filesystem::path dir_;
};

} // namespace aige
