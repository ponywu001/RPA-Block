#include "rpa/core/Variables.h"

#include <algorithm>
#include <cctype>

namespace rpa::core {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace

VariableScope::VariableScope(std::map<std::string, std::string> initial)
    : values_(std::move(initial)) {}

void VariableScope::set(const std::string& name, const std::string& value) {
    values_[name] = value;
}

std::optional<std::string> VariableScope::get(const std::string& name) const {
    auto it = values_.find(name);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

bool VariableScope::has(const std::string& name) const {
    return values_.find(name) != values_.end();
}

void VariableScope::clear() {
    values_.clear();
}

std::string VariableScope::expand(const std::string& input, std::vector<std::string>* missing) const {
    std::string out;
    out.reserve(input.size());

    size_t pos = 0;
    while (pos < input.size()) {
        size_t open = input.find("{{", pos);
        if (open == std::string::npos) {
            out.append(input, pos, std::string::npos);
            break;
        }
        size_t close = input.find("}}", open + 2);
        if (close == std::string::npos) {
            // Unterminated placeholder: emit the rest verbatim rather than
            // silently dropping it, so the mistake is visible in the log.
            out.append(input, pos, std::string::npos);
            break;
        }

        out.append(input, pos, open - pos);

        const std::string name = trim(input.substr(open + 2, close - open - 2));
        auto it = values_.find(name);
        if (it != values_.end()) {
            out.append(it->second);
        } else if (missing) {
            if (std::find(missing->begin(), missing->end(), name) == missing->end()) {
                missing->push_back(name);
            }
        }

        pos = close + 2;
    }

    return out;
}

}  // namespace rpa::core
