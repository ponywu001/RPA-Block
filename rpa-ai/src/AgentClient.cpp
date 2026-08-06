#include "rpa/ai/AgentClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <nlohmann/json.hpp>

#include "rpa/ai/OutputSchema.h"
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

/// Rebuild IR steps from the agent's described_object rows. Each row's
/// `params_json` is re-parsed and validated; a bad row is reported rather than
/// discarding the whole draft.
void decodeSteps(const json& steps, AgentReply& reply) {
    if (!steps.is_array()) return;

    int index = 0;
    for (const auto& entry : steps) {
        ++index;
        if (!entry.is_object()) {
            reply.stepIssues.push_back("step " + std::to_string(index) + ": not an object");
            continue;
        }

        const std::string id = entry.value("id", "step_" + std::to_string(index));
        const std::string type = entry.value("type", "");
        const std::string params = entry.value("params_json", "{}");
        const std::string comment = entry.value("comment", "");

        core::Step step;
        std::string error;
        if (!core::parseStepFromParamsJson(id, type, params, comment, step, error)) {
            reply.stepIssues.push_back("step " + std::to_string(index) + " (" + id + "): " + error);
            continue;
        }
        reply.steps.push_back(std::move(step));
    }
}

bool decodeStructuredOutput(const json& state, AgentReply& reply, std::string& error) {
    auto structured = state.find("structured_output");
    if (structured == state.end() || !structured->is_object()) {
        error = "response contained no structured_output";
        return false;
    }

    reply.reply = structured->value("reply", "");
    reply.hasScript = structured->value("has_script", false);
    reply.scriptName = structured->value("script_name", "");

    auto steps = structured->find("steps");
    if (steps != structured->end()) decodeSteps(*steps, reply);

    // A turn that claims a script but yielded no usable step is a failure the
    // user needs to see, not an empty "applied" action.
    if (reply.hasScript && reply.steps.empty() && reply.stepIssues.empty()) {
        reply.stepIssues.push_back("has_script was true but the steps list was empty");
    }

    auto usage = state.find("usage_cost");
    if (usage != state.end() && usage->is_object()) {
        reply.costUsd = usage->value("cost", 0.0);
        reply.inputTokens = usage->value("input_tokens", 0);
        reply.outputTokens = usage->value("output_tokens", 0);
    }

    return true;
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

void AgentClient::startStream(const QByteArray& payload, bool isProbe) {
    probeRequest_ = isProbe;
    streamBuffer_.clear();
    lastValuesPayload_.clear();

    QUrl url(QString::fromStdString(settings_.gatewayUrl));
    url.setPath(url.path() + QStringLiteral("/runs/stream"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "text/event-stream");
    applyAuthHeaders(request);
    request.setTransferTimeout(settings_.timeoutMs);

    activeReply_ = network_->post(request, payload);

    connect(activeReply_, &QNetworkReply::readyRead, this, &AgentClient::consumeStreamChunk);
    connect(activeReply_, &QNetworkReply::finished, this, &AgentClient::handleStreamFinished);
}

void AgentClient::consumeStreamChunk() {
    if (!activeReply_) return;
    streamBuffer_.append(activeReply_->readAll());
    drainStreamBuffer();
}

void AgentClient::drainStreamBuffer() {
    // Normalise line endings first. SSE permits CRLF, and "\r\n\r\n" contains
    // no "\n\n" substring — without this the frame scan below would never match
    // a single frame on a CRLF stream.
    streamBuffer_.replace("\r\n", "\n");

    // Frames are separated by a blank line. Parse whole frames only; a partial
    // tail stays buffered for the next chunk.
    int separator = 0;
    while ((separator = streamBuffer_.indexOf("\n\n")) >= 0) {
        const QByteArray frame = streamBuffer_.left(separator);
        streamBuffer_.remove(0, separator + 2);

        QByteArray eventName;
        QByteArray data;
        for (const QByteArray& rawLine : frame.split('\n')) {
            QByteArray line = rawLine.trimmed();
            if (line.startsWith("event:")) {
                eventName = line.mid(6).trimmed();
            } else if (line.startsWith("data:")) {
                if (!data.isEmpty()) data.append('\n');
                data.append(line.mid(5).trimmed());
            }
        }
        if (data.isEmpty()) continue;

        if (eventName == "values") {
            // Keep only the latest; the terminal snapshot is the one that
            // carries structured_output and usage_cost.
            lastValuesPayload_ = data;
            emit progress(tr("Receiving agent state…"));
        } else if (eventName == "error") {
            emit progress(tr("Agent reported an error."));
            lastValuesPayload_ = data;
        } else if (eventName == "metadata") {
            emit progress(tr("Run started."));
        }
    }
}

void AgentClient::handleStreamFinished() {
    QNetworkReply* reply = activeReply_;
    if (!reply) return;

    // Take whatever is still unread, then flush the tail. A server that closes
    // without a trailing blank line leaves the terminal `values` frame — the one
    // carrying structured_output — sitting in the buffer, so skipping this makes
    // a perfectly good run look like "the gateway returned no state events".
    streamBuffer_.append(reply->readAll());
    if (!streamBuffer_.trimmed().isEmpty()) {
        streamBuffer_.append("\n\n");
        drainStreamBuffer();
    }

    activeReply_ = nullptr;

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError && lastValuesPayload_.isEmpty()) {
        const QString detail = status > 0
            ? tr("HTTP %1: %2").arg(status).arg(networkErrorText)
            : networkErrorText;
        if (probeRequest_) {
            emit connectionTested(false, detail);
        } else {
            emit failed({detail.toStdString(), status});
        }
        return;
    }

    if (lastValuesPayload_.isEmpty()) {
        const QString detail = tr("The gateway returned no state events.");
        if (probeRequest_) {
            emit connectionTested(false, detail);
        } else {
            emit failed({detail.toStdString(), status});
        }
        return;
    }

    json state = json::parse(lastValuesPayload_.toStdString(), nullptr, false);
    if (state.is_discarded()) {
        const QString detail = tr("Could not parse the gateway's final state payload.");
        if (probeRequest_) {
            emit connectionTested(false, detail);
        } else {
            emit failed({detail.toStdString(), status});
        }
        return;
    }

    if (probeRequest_) {
        const bool ok = state.contains("structured_output");
        emit connectionTested(ok, ok ? tr("Connected. The gateway answered with a valid "
                                          "structured result.")
                                     : tr("Reached the gateway, but it returned no "
                                          "structured_output."));
        return;
    }

    AgentReply parsed;
    std::string error;
    if (!decodeStructuredOutput(state, parsed, error)) {
        emit failed({error, status});
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
