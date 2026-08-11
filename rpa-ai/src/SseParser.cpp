#include "rpa/ai/SseParser.h"

namespace rpa::ai {

namespace {

constexpr auto kNpos = std::string_view::npos;

std::string_view rstrip(std::string_view text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

void SseParser::feed(std::string_view chunk, std::vector<SseEvent>& out) {
    std::string pending;
    pending.reserve(chunk.size() + 1);
    if (pendingCr_) {
        pending.push_back('\r');
        pendingCr_ = false;
    }
    pending.append(chunk);

    if (!pending.empty() && pending.back() == '\r') {
        pending.pop_back();
        pendingCr_ = true;
    }

    // Normalise CRLF and lone CR to LF as bytes arrive, so the frame scan below
    // only ever has to look for "\n\n".
    for (size_t i = 0; i < pending.size(); ++i) {
        if (pending[i] == '\r') {
            buffer_.push_back('\n');
            if (i + 1 < pending.size() && pending[i + 1] == '\n') ++i;
        } else {
            buffer_.push_back(pending[i]);
        }
    }

    drain(out);
}

void SseParser::flush(std::vector<SseEvent>& out) {
    if (pendingCr_) {
        buffer_.push_back('\n');
        pendingCr_ = false;
    }

    if (buffer_.find_first_not_of(" \t\n") != std::string::npos) {
        buffer_.append("\n\n");
        drain(out);
    }
    buffer_.clear();
}

void SseParser::reset() {
    buffer_.clear();
    pendingCr_ = false;
}

void SseParser::drain(std::vector<SseEvent>& out) {
    size_t separator = 0;
    while ((separator = buffer_.find("\n\n")) != std::string::npos) {
        const std::string frame = buffer_.substr(0, separator);
        buffer_.erase(0, separator + 2);

        SseEvent event;
        bool hasData = false;

        size_t pos = 0;
        while (pos <= frame.size()) {
            const size_t eol = frame.find('\n', pos);
            const size_t end = (eol == std::string::npos) ? frame.size() : eol;
            const std::string_view line(frame.data() + pos, end - pos);
            pos = end + 1;

            // Blank padding and ":" keep-alive comments carry no field.
            if (line.empty() || line.front() == ':') continue;

            const size_t colon = line.find(':');
            const std::string_view name =
                rstrip(colon == kNpos ? line : line.substr(0, colon));
            std::string_view value;
            if (colon != kNpos) {
                value = line.substr(colon + 1);
                // The spec strips exactly one space after the colon -- trimming
                // further would corrupt data fields that begin with whitespace.
                if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
            }

            if (name == "event") {
                event.name.assign(rstrip(value));
            } else if (name == "data") {
                if (hasData) event.data.push_back('\n');
                event.data.append(value);
                hasData = true;
            }
        }

        // A frame with only an event name says nothing this client can act on.
        if (hasData) out.push_back(std::move(event));
    }
}

}  // namespace rpa::ai
