#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "rpa/ai/AgentTypes.h"

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
    /// Parse every complete frame sitting in `streamBuffer_`. Split out from
    /// consumeStreamChunk so the finish handler can flush the tail too.
    void drainStreamBuffer();
    void handleStreamFinished();

    QNetworkAccessManager* network_;
    QNetworkReply* activeReply_ = nullptr;
    AgentSettings settings_;

    QByteArray streamBuffer_;
    /// Latest `values` payload seen; the terminal one wins.
    QByteArray lastValuesPayload_;
    bool probeRequest_ = false;
};

}  // namespace rpa::ai

Q_DECLARE_METATYPE(rpa::ai::AgentReply)
Q_DECLARE_METATYPE(rpa::ai::AgentError)
