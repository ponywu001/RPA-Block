#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "rpa/core/Script.h"

namespace rpa::server {

struct PublishedScript {
    std::string id;
    core::Script script;
    std::string sourcePath;
    std::string publishedAt;
};

/// The set of flows the REST API is allowed to run. A flow must be explicitly
/// published; anything else is invisible to the API even if it sits in the
/// same folder.
class ScriptRepository {
public:
    /// Directory holding `<id>.rpa.json` files. Created if absent.
    void setDirectory(const std::string& directory);
    std::string directory() const;

    /// Re-read every published flow from disk. Returns the number loaded.
    size_t reload(std::vector<std::string>* errors = nullptr);

    std::optional<PublishedScript> find(const std::string& id) const;
    std::vector<PublishedScript> list() const;

    /// Publish (or replace) a flow. `id` is derived from the script name when
    /// empty. Returns the id on success.
    std::optional<std::string> publish(const core::Script& script,
                                       const std::string& id,
                                       std::string& error);

    bool unpublish(const std::string& id, std::string& error);

    /// Slugify a name into a filesystem- and URL-safe id.
    static std::string makeId(const std::string& name);

private:
    mutable std::mutex mutex_;
    std::string directory_;
    std::map<std::string, PublishedScript> scripts_;
};

}  // namespace rpa::server
