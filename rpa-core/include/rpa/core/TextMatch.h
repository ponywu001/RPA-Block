#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rpa/core/Script.h"

namespace rpa::core {

/// Does `candidate` satisfy `needle` under `mode`?
///
/// Shared by the OCR locator and window-title matching, which had identical
/// copies of this. An invalid regex matches nothing rather than throwing: a bad
/// pattern in a flow should fail its step, not tear down the run.
bool textMatches(const std::string& candidate, const std::string& needle, MatchMode mode);

/// The candidates worth showing when nothing matched.
///
/// Ranked by how much of `needle` they carry, not by edit distance, because the
/// failure this serves is a line holding the wanted text plus something extra:
/// OCR reads a magnifier icon as "Q", so a search box comes back as "Q 搜尋" and
/// an exact match on "搜尋" fails while the text is plainly on screen. Naming the
/// near miss is what turns that into an obvious fix.
///
/// Candidates containing the whole needle rank first, shortest first.
std::vector<std::string> nearestTexts(const std::vector<std::string>& candidates,
                                      const std::string& needle,
                                      std::size_t limit = 3);

}  // namespace rpa::core
