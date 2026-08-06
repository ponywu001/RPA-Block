#include "rpa/server/ScriptRepository.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "rpa/core/ScriptIO.h"
#include "rpa/server/RunStore.h"

namespace rpa::server {

namespace fs = std::filesystem;

namespace {
constexpr const char* kSuffix = ".rpa.json";
}

std::string ScriptRepository::makeId(const std::string& name) {
    std::string id;
    id.reserve(name.size());
    bool droppedCharacters = false;

    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            id += static_cast<char>(std::tolower(c));
        } else if (c == '-' || c == '_' || c == ' ' || c == '.') {
            // Collapse runs of separators into a single dash.
            if (!id.empty() && id.back() != '-') id += '-';
        } else if (c >= 0x80) {
            // Non-ASCII is dropped: the id has to be safe in both a URL path and
            // a filename, and the display name keeps the original text.
            droppedCharacters = true;
        }
    }

    while (!id.empty() && id.back() == '-') id.pop_back();
    if (id.size() > 64) id.resize(64);
    while (!id.empty() && id.back() == '-') id.pop_back();

    // Dropping characters can make two different names produce the same id --
    // and for a Chinese-language flow name it drops *everything*, so every such
    // flow used to become "flow" and silently overwrite the last one. A short
    // digest of the original name keeps distinct names distinct while staying
    // stable across restarts, so a published URL does not change under the
    // caller. Pure-ASCII names are untouched and keep their readable ids.
    if (droppedCharacters || id.empty()) {
        uint32_t hash = 2166136261u;  // FNV-1a
        for (unsigned char c : name) {
            hash ^= c;
            hash *= 16777619u;
        }
        char suffix[9];
        std::snprintf(suffix, sizeof(suffix), "%08x", hash);

        if (id.empty()) id = "flow";
        id += '-';
        id += suffix;
    }

    if (id.empty()) id = "flow";
    return id;
}

void ScriptRepository::setDirectory(const std::string& directory) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        directory_ = directory;
    }
    std::error_code ec;
    fs::create_directories(directory, ec);
    reload();
}

std::string ScriptRepository::directory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return directory_;
}

size_t ScriptRepository::reload(std::vector<std::string>* errors) {
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dir = directory_;
    }
    if (dir.empty()) return 0;

    std::map<std::string, PublishedScript> loaded;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        const std::string filename = entry.path().filename().string();
        if (filename.size() <= std::strlen(kSuffix)) continue;
        if (filename.compare(filename.size() - std::strlen(kSuffix), std::strlen(kSuffix),
                             kSuffix) != 0) {
            continue;
        }

        const core::ParseResult parsed = core::loadScriptFile(entry.path().string());
        if (!parsed.ok) {
            if (errors) errors->push_back(filename + ": " + parsed.error);
            continue;
        }
        // A flow with validation issues is still published — the API surfaces
        // the issues on run, which beats silently hiding the flow.
        if (!parsed.issues.empty() && errors) {
            for (const auto& issue : parsed.issues) {
                errors->push_back(filename + ": " + issue.message);
            }
        }

        PublishedScript published;
        published.id = filename.substr(0, filename.size() - std::strlen(kSuffix));
        published.script = parsed.script;
        published.sourcePath = entry.path().string();

        const auto writeTime = fs::last_write_time(entry.path(), ec);
        if (!ec) {
            const auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(writeTime);
            published.publishedAt = toIso8601(systemTime);
        }

        loaded[published.id] = std::move(published);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    scripts_ = std::move(loaded);
    return scripts_.size();
}

std::optional<PublishedScript> ScriptRepository::find(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = scripts_.find(id);
    if (it == scripts_.end()) return std::nullopt;
    return it->second;
}

std::vector<PublishedScript> ScriptRepository::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PublishedScript> all;
    all.reserve(scripts_.size());
    for (const auto& [id, script] : scripts_) all.push_back(script);
    return all;
}

std::optional<std::string> ScriptRepository::publish(const core::Script& script,
                                                     const std::string& id,
                                                     std::string& error) {
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dir = directory_;
    }
    if (dir.empty()) {
        error = "no publish directory configured";
        return std::nullopt;
    }

    const std::string resolvedId = id.empty() ? makeId(script.name) : makeId(id);
    const fs::path path = fs::path(dir) / (resolvedId + kSuffix);

    std::error_code ec;
    fs::create_directories(dir, ec);

    if (!core::saveScriptFile(script, path.string(), error)) return std::nullopt;

    PublishedScript published;
    published.id = resolvedId;
    published.script = script;
    published.sourcePath = path.string();
    published.publishedAt = toIso8601(std::chrono::system_clock::now());

    std::lock_guard<std::mutex> lock(mutex_);
    scripts_[resolvedId] = std::move(published);
    return resolvedId;
}

bool ScriptRepository::unpublish(const std::string& id, std::string& error) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = scripts_.find(id);
        if (it == scripts_.end()) {
            error = "no published flow with id: " + id;
            return false;
        }
        path = it->second.sourcePath;
        scripts_.erase(it);
    }

    std::error_code ec;
    if (!path.empty()) fs::remove(path, ec);
    if (ec) {
        error = "removed from the published set, but deleting the file failed: " + ec.message();
        return false;
    }
    return true;
}

}  // namespace rpa::server
