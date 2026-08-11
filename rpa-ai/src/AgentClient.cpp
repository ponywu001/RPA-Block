#include "rpa/ai/AgentClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <nlohmann/json.hpp>

#include "rpa/ai/OutputSchema.h"
#include "rpa/ai/ReplyDecoder.h"
#include "rpa/core/ScriptIO.h"

namespace rpa::ai {

using json = nlohmann::json;

namespace {

std::string roleLabel(ChatMessage::Role role) {
    switch (role) {
        case ChatMessage::Role::User: return "USER";
        case ChatMessage::Role::Assistant: return "ASSISTANT";
        case ChatMessage::Role::System: return "CONTEXT";
    }
    return "USER";
}

/// The gateway's contract takes only user-role messages, so earlier turns are
/// flattened into a transcript block ahead of the current request.
std::string buildTranscript(const std::vector<ChatMessage>& history) {
    if (history.size() <= 1) return {};

    std::string transcript = "## Conversation so far\n\n";
    for (size_t i = 0; i + 1 < history.size(); ++i) {
        transcript += roleLabel(history[i].role);
        transcript += ": ";
        transcript += history[i].text;
        transcript += "\n\n";
    }
    return transcript;
}

json makeImageBlock(const std::vector<unsigned char>& png) {
    const QByteArray raw(reinterpret_cast<const char*>(png.data()),
                         static_cast<qsizetype>(png.size()));
    const std::string dataUri =
        "data:image/png;base64," + raw.toBase64().toStdString();
    return json{{"type", "image_url"}, {"image_url", json{{"url", dataUri}}}};
}

}  // namespace

AgentClient::AgentClient(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)) {
    qRegisterMetaType<rpa::ai::AgentReply>("rpa::ai::AgentReply");
    qRegisterMetaType<rpa::ai::AgentError>("rpa::ai::AgentError");
}

AgentClient::~AgentClient() {
    cancel();
}

void AgentClient::setSettings(AgentSettings settings) {
    settings_ = std::move(settings);
}

void AgentClient::applyAuthHeaders(QNetworkRequest& request) const {
    if (settings_.authMode == AuthMode::Jwt && !settings_.accessToken.empty()) {
        request.setRawHeader("Authorization",
                             QByteArray("Bearer ") + QByteArray::fromStdString(settings_.accessToken));
    } else if (!settings_.apiKey.empty()) {
        request.setRawHeader("X-API-Key", QByteArray::fromStdString(settings_.apiKey));
    }
}

void AgentClient::cancel() {
    if (!activeReply_) return;
    QNetworkReply* reply = activeReply_;
    activeReply_ = nullptr;
    reply->abort();
    reply->deleteLater();
}

void AgentClient::send(const std::vector<ChatMessage>& history) {
    if (history.empty()) {
        emit failed({"nothing to send", 0});
        return;
    }
    if (activeReply_) {
        emit failed({"a request is already in flight", 0});
        return;
    }

    const ChatMessage& current = history.back();

    std::string text = buildTranscript(history);
    text += "## Current request\n\n";
    text += current.text;

    json content = json::array();
    content.push_back(json{{"type", "text"}, {"text", text}});
    if (!current.imagePng.empty()) {
        content.push_back(makeImageBlock(current.imagePng));
    }

    json input;
    input["messages"] = json::array({json{{"role", "user"}, {"content", content}}});
    input["output_schema"] = json::parse(rpaOutputSchemaJson());
    input["system_prompt"] = rpaSystemPrompt();
    if (settings_.maxOutputTokens > 0) input["max_output_tokens"] = settings_.maxOutputTokens;
    if (!settings_.provider.empty()) input["provider"] = settings_.provider;
    if (!settings_.model.empty()) input["model"] = settings_.model;

    json body;
    body["assistant_id"] = settings_.assistantId;
    body["input"] = std::move(input);
    body["stream_mode"] = json::array({"values"});

    startStream(QByteArray::fromStdString(body.dump()), false);
}

void AgentClient::testConnection() {
    if (activeReply_) {
        emit connectionTested(false, tr("A request is already in flight."));
        return;
    }

    json input;
    input["messages"] = json::array(
        {json{{"role", "user"}, {"content", "Reply with the single word OK."}}});
    input["output_schema"] =
        json{{"version", 1},
             {"fields", json::array({json{{"name", "word"}, {"type", "str"}, {"max_length", 32}}})}};
    input["max_output_tokens"] = 64;
    if (!settings_.provider.empty()) input["provider"] = settings_.provider;
    if (!settings_.model.empty()) input["model"] = settings_.model;

    json body;
    body["assistant_id"] = settings_.assistantId;
    body["input"] = std::move(input);
    body["stream_mode"] = json::array({"values"});

    startStream(QByteArray::fromStdString(body.dump()), true);
}

void AgentClient::setRawStreamTap(std::function<void(const QByteArray&)> tap) {
    rawStreamTap_ = std::move(tap);
}

void AgentClient::startStream(const QByteArray& payload, bool isProbe) {
    probeRequest_ = isProbe;
    parser_.reset();
    lastValuesPayload_.clear();
    lastStructuredPayload_.clear();
    rawBody_.clear();

    QUrl url(QString::fromStdString(settings_.gatewayUrl));
    url.setPath(url.path() + QStringLiteral("/runs/stream"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "text/event-stream");
    applyAuthHeaders(request);
    request.setTransferTimeout(settings_.timeoutMs);
    // A redirect would be replayed as a GET with the body dropped, which reaches
    // the gateway as an empty request and fails in a way that points nowhere.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);

    activeReply_ = network_->post(request, payload);

    connect(activeReply_, &QNetworkReply::readyRead, this, &AgentClient::consumeStreamChunk);
    connect(activeReply_, &QNetworkReply::finished, this, &AgentClient::handleStreamFinished);
}

void AgentClient::consumeStreamChunk() {
    if (!activeReply_) return;

    const QByteArray chunk = activeReply_->readAll();
    if (chunk.isEmpty()) return;
    if (rawStreamTap_) rawStreamTap_(chunk);
    rawBody_.append(chunk.constData(), static_cast<size_t>(chunk.size()));

    std::vector<SseEvent> events;
    parser_.feed(std::string_view(chunk.constData(), static_cast<size_t>(chunk.size())), events);
    dispatchEvents(events);
}

void AgentClient::dispatchEvents(const std::vector<SseEvent>& events) {
    for (const SseEvent& event : events) {
        if (event.name == "values") {
            lastValuesPayload_ = event.data;
            if (payloadHasStructuredOutput(event.data)) lastStructuredPayload_ = event.data;
            emit progress(tr("Receiving agent state…"));
        } else if (event.name == "error") {
            emit progress(tr("Agent reported an error."));
            lastValuesPayload_ = event.data;
        } else if (event.name == "metadata") {
            emit progress(tr("Run started."));
        }
    }
}

void AgentClient::reportFailure(const QString& detail, int status) {
    if (probeRequest_) {
        emit connectionTested(false, detail);
    } else {
        emit failed({detail.toStdString(), status});
    }
}

void AgentClient::handleStreamFinished() {
    QNetworkReply* reply = activeReply_;
    if (!reply) return;

    // Take whatever is still unread, then flush the tail. A server that closes
    // without a trailing blank line leaves the terminal `values` frame — the one
    // carrying structured_output — sitting in the buffer, so skipping this makes
    // a perfectly good run look like "the gateway returned no state events".
    const QByteArray tail = reply->readAll();
    if (!tail.isEmpty()) {
        if (rawStreamTap_) rawStreamTap_(tail);
        rawBody_.append(tail.constData(), static_cast<size_t>(tail.size()));
    }

    std::vector<SseEvent> events;
    parser_.feed(std::string_view(tail.constData(), static_cast<size_t>(tail.size())), events);
    parser_.flush(events);
    dispatchEvents(events);

    activeReply_ = nullptr;

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    // Prefer the last snapshot that actually carried a result. Falling back to
    // the last `values` of any shape keeps error events reportable.
    const std::string& payload =
        lastStructuredPayload_.empty() ? lastValuesPayload_ : lastStructuredPayload_;

    // Gated on the *structured* payload, not on any payload: the gateway emits
    // intermediate `values` snapshots that carry no result, so a stream cut off
    // mid-run has something buffered and would otherwise be reported as "the
    // gateway returned no structured_output" — sending the user to look at the
    // gateway when what actually happened was a timeout.
    if (networkError != QNetworkReply::NoError && lastStructuredPayload_.empty()) {
        // An HTTP error answers with a JSON body, not SSE, so nothing above
        // parsed it. That body says whether the key or the payload was wrong;
        // Qt's errorString only says "the host requires authentication".
        const std::string serverDetail = extractErrorDetail(rawBody_);

        // A stream cut off mid-run still carries the 200 from its headers.
        // Quoting it reads as "the request succeeded", so the status is only
        // named when it is itself the complaint.
        const bool statusIsTheProblem = status >= 400;
        QString detail = statusIsTheProblem
            ? tr("HTTP %1: %2").arg(status).arg(networkErrorText)
            : networkErrorText;
        if (!serverDetail.empty()) {
            detail += QStringLiteral(" — ") + QString::fromStdString(serverDetail);
        }
        reportFailure(detail, statusIsTheProblem ? status : 0);
        return;
    }

    if (payload.empty()) {
        reportFailure(tr("The gateway returned no state events."), status);
        return;
    }

    if (probeRequest_) {
        const bool ok = payloadHasStructuredOutput(payload);
        emit connectionTested(ok, ok ? tr("Connected. The gateway answered with a valid "
                                          "structured result.")
                                     : tr("Reached the gateway, but it returned no "
                                          "structured_output."));
        return;
    }

    AgentReply parsed;
    std::string error;
    if (!decodeAgentState(payload, parsed, error)) {
        reportFailure(QString::fromStdString(error), status);
        return;
    }

    emit finished(parsed);
}

void AgentClient::signIn(const QString& username, const QString& password) {
    if (activeReply_) {
        emit failed({"a request is already in flight", 0});
        return;
    }

    QNetworkRequest request{QUrl(QString::fromStdString(settings_.signInUrl))};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(settings_.timeoutMs);

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("username"), username);
    form.addQueryItem(QStringLiteral("password"), password);

    // Tracked in activeReply_ so isBusy() and cancel() cover the sign-in too.
    activeReply_ = network_->post(request, form.toString(QUrl::FullyEncoded).toUtf8());

    connect(activeReply_, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = activeReply_;
        if (!reply) return;
        activeReply_ = nullptr;

        const QByteArray payload = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkErrorText = reply->errorString();
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError) {
            emit failed({networkErrorText.toStdString(), status});
            return;
        }

        json doc = json::parse(payload.toStdString(), nullptr, false);
        // Check the type, not just presence: a non-string access_token would
        // make .get<std::string>() throw a json::type_error out of a Qt slot,
        // where nothing is positioned to catch it.
        if (doc.is_discarded() || !doc.is_object() || !doc.contains("access_token") ||
            !doc["access_token"].is_string()) {
            emit failed({"sign-in response did not contain a string access_token", status});
            return;
        }

        settings_.accessToken = doc["access_token"].get<std::string>();
        settings_.authMode = AuthMode::Jwt;
        emit signedIn();
    });
}

}  // namespace rpa::ai
