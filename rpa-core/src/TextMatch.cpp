#include "rpa/core/TextMatch.h"

#include <algorithm>
#include <regex>

namespace rpa::core {

bool textMatches(const std::string& candidate, const std::string& needle, MatchMode mode) {
    switch (mode) {
        case MatchMode::Exact:
            return candidate == needle;
        case MatchMode::Contains:
            return candidate.find(needle) != std::string::npos;
        case MatchMode::Regex:
            try {
                return std::regex_search(candidate, std::regex(needle));
            } catch (const std::regex_error&) {
                return false;
            }
    }
    return false;
}

std::vector<std::string> nearestTexts(const std::vector<std::string>& candidates,
                                      const std::string& needle, std::size_t limit) {
    if (needle.empty() || limit == 0) return {};

    struct Scored {
        double score;
        const std::string* text;
    };
    std::vector<Scored> scored;

    for (const std::string& candidate : candidates) {
        if (candidate.empty()) continue;

        double score = 0.0;
        if (candidate.find(needle) != std::string::npos) {
            // Carries the whole needle, so the only reason it did not match is
            // the mode. These come first, shortest first: the shortest such line
            // is the closest thing to what was actually asked for.
            score = 1000.0 - static_cast<double>(candidate.size());
        } else {
            // Otherwise, the longest prefix of the needle that appears at all.
            // Cutting `needle` by bytes is safe here because a partial UTF-8
            // sequence simply will not be found, so it scores no better than the
            // shorter prefix that ends on a character boundary.
            std::size_t best = 0;
            for (std::size_t length = needle.size(); length > 0; --length) {
                if (candidate.find(needle.substr(0, length)) != std::string::npos) {
                    best = length;
                    break;
                }
            }
            if (best == 0) continue;
            score = static_cast<double>(best) / static_cast<double>(needle.size());
        }
        scored.push_back(Scored{score, &candidate});
    }

    // Stable so equally-scored candidates keep the order they were read in,
    // which for OCR is roughly top-to-bottom on screen.
    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b) { return a.score > b.score; });

    std::vector<std::string> out;
    for (const Scored& entry : scored) {
        if (out.size() >= limit) break;
        out.push_back(*entry.text);
    }
    return out;
}

}  // namespace rpa::core
