#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rpa::core {

/// Flat string variable scope with `{{name}}` interpolation. Values written by
/// steps (OCR hits, HTTP responses) live here alongside the script's declared
/// variables.
class VariableScope {
public:
    VariableScope() = default;
    explicit VariableScope(std::map<std::string, std::string> initial);

    void set(const std::string& name, const std::string& value);
    std::optional<std::string> get(const std::string& name) const;
    bool has(const std::string& name) const;
    void clear();

    const std::map<std::string, std::string>& all() const { return values_; }

    /// Replace every `{{name}}` occurrence. Unknown names expand to an empty
    /// string and are reported through `missing` so the executor can log them.
    std::string expand(const std::string& input, std::vector<std::string>* missing = nullptr) const;

private:
    std::map<std::string, std::string> values_;
};

}  // namespace rpa::core
