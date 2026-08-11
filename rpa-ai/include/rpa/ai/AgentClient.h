#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <vector>

#include "rpa/ai/AgentTypes.h"
#include "rpa/ai/SseParser.h"

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace rpa::ai {

/// Talks to the structured-multimodal-agent LangGraph gateway.
///
/// The gateway streams Server-Sent Events; only the *last* `values` event holds
/// the terminal state, so the client accumulates and keeps replacing its
/// snapshot until the stream ends.
class AgentClient : public QObject {
    Q_OBJECT

public:
    explicit AgentClient(QObject* parent = nullptr);
    ~AgentClient() override;

    void setSettings(AgentSettings settings);
    const AgentSettings& settings() const { return settings_; }

    /// Send the conversation. `history` may include assistant turns; they are
    /// folded into the outgoing user message since the contract accepts only
    /// user-role messages.
    void send(const std::vector<ChatMessage>& history);

    /// Exchange username/password for an access token, storing it in settings.
    void signIn(const QString& username, const QString& password);

    /// Cheap round trip that verifies the gateway URL and credentials.
    void testConnection();

    bool isBusy() const { return activeReply_ != nullptr; }
    void cancel();

    /// Receives every byte of the response body as it arrives. Set by the probe
    /// tool to record a real stream to disk: the recording becomes the fixture
    /// the offline parser tests replay, which is the only way this client's
    /// framing gets held to what the gateway actually sends.
    void setRawStreamTap(std::function<void(const QByteArray&)> tap);

signals:
    /// Progress text extracted from intermediate stream events.
    void progress(const QString& note);
    void finished(const rpa::ai::AgentReply& reply);
    void failed(const rpa::ai::AgentError& error);
    void signedIn();
    void connectionTested(bool ok, const QString& detail);

private:
    void startStream(const QByteArray& payload, bool isProbe);
    void applyAuthHeaders(QNetworkRequest& request) const;
    void consumeStreamChunk();
    void dispatchEvents(const std::vector<SseEvent>& events);
    void handleStreamFinished();
    /// Report a terminal problem down whichever channel this request came in on.
    void reportFailure(const QString& detail, int status);

    QNetworkAccessManager* network_;
    QNetworkReply* activeReply_ = nullptr;
    AgentSettings settings_;
    std::function<void(const QByteArray&)> rawStreamTap_;

    SseParser parser_;
    /// Latest `values` payload seen; the terminal one wins.
    std::string lastValuesPayload_;
    /// The latest payload that actually carried `structured_output`. Preferred
    /// over lastValuesPayload_, because a gateway that emits a thinner snapshot
    /// after the terminal one would otherwise erase the result.
    std::string lastStructuredPayload_;
    /// Raw body of a response that never parsed as SSE -- an error page or a
    /// plain JSON error object. Kept so the failure can quote the server.
    std::string rawBody_;
    bool probeRequest_ = false;
};

}  // namespace rpa::ai

Q_DECLARE_METATYPE(rpa::ai::AgentReply)
Q_DECLARE_METATYPE(rpa::ai::AgentError)
