#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rpa::ai {

struct SseEvent {
    std::string name;
    std::string data;
};

/// Incremental Server-Sent Events reader.
///
/// Deliberately free of Qt so the framing rules -- the part most likely to be
/// wrong against a real gateway -- can be replayed byte by byte in a test.
/// `feed` may be called with arbitrary fragments; a chunk boundary is allowed to
/// land anywhere, including in the middle of a CRLF.
class SseParser {
public:
    /// Append `chunk` and emit every frame that is now complete.
    void feed(std::string_view chunk, std::vector<SseEvent>& out);

    /// Emit the trailing frame of a stream that closed without a blank line.
    /// Servers do this routinely, and the terminal frame is the one carrying the
    /// result -- dropping it turns a good run into "no state events".
    void flush(std::vector<SseEvent>& out);

    void reset();

private:
    void drain(std::vector<SseEvent>& out);

    std::string buffer_;
    /// A chunk ending in CR is ambiguous: lone-CR line break, or the first half
    /// of a CRLF split across chunks. Held back until the next feed decides.
    bool pendingCr_ = false;
};

}  // namespace rpa::ai
