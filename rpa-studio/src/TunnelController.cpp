#include "TunnelController.h"

#include <algorithm>
#include <iterator>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rpa::studio {

namespace {

using json = nlohmann::json;

/// How long to let the agent shut down politely before killing it. It is a
/// console process, so terminate() is not something it necessarily acts on.
constexpr int kShutdownGraceMs = 2000;

constexpr int kLogTailChars = 1200;

/// Backoff for reconnects, in seconds. The last value repeats forever: a
/// machine that is supposed to stay reachable should keep trying rather than
/// give up and go quiet, but not hammer the service either.
constexpr int kReconnectDelaysSec[] = {5, 15, 30, 60, 120};

/// Grace before checking the public address answers. The edge needs a moment to
/// start routing after the agent reports a connection.
constexpr int kReachabilityDelayMs = 3000;
constexpr int kReachabilityTimeoutMs = 15000;

/// Turn the agent's own diagnosis into something the person in front of the app
/// can act on. Both agents print an error code and a documentation link, which
/// is the right thing for a developer at a terminal and no help at all here.
QString explainAgentError(TunnelProvider provider, const QString& log) {
    if (provider == TunnelProvider::Ngrok) {
        if (log.contains(QLatin1String("ERR_NGROK_334"))) {
            // Seen for real: killing the agent leaves the endpoint registered on
            // ngrok's side for a while, so the next attempt is refused as a
            // duplicate. Reads as "the feature is broken" without this.
            return QStringLiteral(
                "上一條通道還沒完全關閉，ngrok 那邊仍記著它。等十來秒再開一次；"
                "如果還是不行，看看是不是有其他 ngrok 視窗還開著。");
        }
        if (log.contains(QLatin1String("ERR_NGROK_105")) ||
            log.contains(QLatin1String("ERR_NGROK_107")) ||
            log.contains(QLatin1String("authentication failed"))) {
            return QStringLiteral(
                "ngrok 不接受這個 authtoken。請到 ngrok 網站複製一份新的貼進來。");
        }
        if (log.contains(QLatin1String("ERR_NGROK_108")) ||
            log.contains(QLatin1String("simultaneous"))) {
            return QStringLiteral("這個 ngrok 帳號同時開著的通道數已達方案上限，請先關掉其他的。");
        }
        if (log.contains(QLatin1String("ERR_NGROK_324")) ||
            log.contains(QLatin1String("not authorized")) ||
            log.contains(QLatin1String("reserve"))) {
            return QStringLiteral(
                "這個 ngrok 帳號沒有這個固定網域。請先在 ngrok 後台保留它，"
                "或把「固定網域」欄位清空改用隨機網址。");
        }
        return {};
    }

    if (log.contains(QLatin1String("Unauthorized")) ||
        log.contains(QLatin1String("invalid tunnel credentials")) ||
        log.contains(QLatin1String("Provided Tunnel token is not valid")) ||
        log.contains(QLatin1String("token is invalid"))) {
        return QStringLiteral(
            "Cloudflare 不接受這個通道 token。請到 Zero Trust 後台該通道的頁面重新複製，"
            "整串 eyJ… 都要。");
    }
    if (log.contains(QLatin1String("tunnel not found")) ||
        log.contains(QLatin1String("deleted"))) {
        return QStringLiteral("這條 Cloudflare 通道在後台已經不存在了，請重新建立並取得新 token。");
    }
    return {};
}

}  // namespace

QString toString(TunnelProvider provider) {
    return provider == TunnelProvider::Cloudflare ? QStringLiteral("cloudflare")
                                                  : QStringLiteral("ngrok");
}

QString agentName(TunnelProvider provider) {
    return provider == TunnelProvider::Cloudflare ? QStringLiteral("cloudflared")
                                                  : QStringLiteral("ngrok");
}

TunnelController::TunnelController(QObject* parent) : QObject(parent) {}

TunnelController::~TunnelController() {
    stop();
}

QString TunnelController::findBinary(TunnelProvider provider) {
    const QString executable = provider == TunnelProvider::Cloudflare
                                   ? QStringLiteral("cloudflared")
                                   : QStringLiteral("ngrok");

    const QString beside = QDir(QCoreApplication::applicationDirPath())
                               .filePath(executable + QStringLiteral(".exe"));
    if (QFileInfo::exists(beside)) return beside;

    return QStandardPaths::findExecutable(executable);
}

bool TunnelController::isRunning() const {
    return process_ != nullptr;
}

bool TunnelController::isReconnecting() const {
    return reconnectTimer_ != nullptr && reconnectTimer_->isActive();
}

void TunnelController::scheduleReconnect() {
    constexpr int kSteps = static_cast<int>(std::size(kReconnectDelaysSec));
    const int delay = kReconnectDelaysSec[std::min(reconnectAttempt_, kSteps - 1)];
    ++reconnectAttempt_;

    if (!reconnectTimer_) {
        reconnectTimer_ = new QTimer(this);
        reconnectTimer_->setSingleShot(true);
        connect(reconnectTimer_, &QTimer::timeout, this, [this] { start(config_); });
    }
    reconnectTimer_->start(delay * 1000);
    emit reconnecting(delay, reconnectAttempt_);
}

QStringList TunnelController::agentArguments() const {
    if (config_.provider == TunnelProvider::Cloudflare) {
        // The token carries the tunnel's identity and the dashboard carries its
        // routing, so there is nothing else to pass -- not even the port.
        // --no-autoupdate because an agent that restarts itself underneath a
        // machine that is meant to stay reachable is not a favour.
        return {
            QStringLiteral("tunnel"),
            QStringLiteral("--no-autoupdate"),
            QStringLiteral("run"),
        };
    }

    QStringList arguments{
        QStringLiteral("http"),
        // Loopback on purpose: the tunnel is the only way in, so the API never
        // has to be opened to the local network at the same time.
        QStringLiteral("127.0.0.1:%1").arg(config_.port),
        QStringLiteral("--log"),
        QStringLiteral("stdout"),
        QStringLiteral("--log-format"),
        QStringLiteral("json"),
    };
    if (!config_.hostname.isEmpty()) {
        arguments << QStringLiteral("--url=%1").arg(config_.hostname);
    }
    return arguments;
}

void TunnelController::start(const TunnelConfig& config) {
    if (process_) {
        emit failed(QStringLiteral("對外通道已經開著了。"));
        return;
    }

    config_ = config;
    const QString agent = agentName(config_.provider);

    if (config_.binaryPath.isEmpty() || !QFileInfo::exists(config_.binaryPath)) {
        emit failed(QStringLiteral("找不到 %1 執行檔。請在下方指定它的位置，或先安裝它。")
                        .arg(agent));
        return;
    }
    // Refused rather than passed through to the agent. Both of them would fail
    // anyway, but this way the message names the empty field instead of
    // relaying whatever the agent says about missing credentials.
    if (config_.authToken.isEmpty()) {
        emit failed(
            config_.provider == TunnelProvider::Cloudflare
                ? QStringLiteral("請先填入 Cloudflare 的通道 token（Zero Trust 後台那串 eyJ…）。")
                : QStringLiteral("請先填入 ngrok 帳號的 authtoken。"));
        return;
    }
    if (config_.provider == TunnelProvider::Cloudflare && config_.hostname.isEmpty()) {
        // Without it there is no address to show, copy, or check -- cloudflared
        // itself never says which hostname the tunnel serves.
        emit failed(QStringLiteral(
            "請填入這條 Cloudflare 通道對外的主機名稱，例如 rpa.example.com。"
            "cloudflared 不會自己回報網址，那是在 Cloudflare 後台設定的。"));
        return;
    }

    publicUrl_.clear();
    buffer_.clear();
    logTail_.clear();
    settled_ = false;
    stopping_ = false;

    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);

    // The token goes through the environment, not the argument list. A command
    // line is readable by any other process on the machine, and either token is
    // enough to open tunnels on the user's account.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(config_.provider == TunnelProvider::Cloudflare
                           ? QStringLiteral("TUNNEL_TOKEN")
                           : QStringLiteral("NGROK_AUTHTOKEN"),
                       config_.authToken);
    process_->setProcessEnvironment(environment);

    connect(process_, &QProcess::readyRead, this, &TunnelController::consumeOutput);

    connect(process_, &QProcess::errorOccurred, this, [this, agent](QProcess::ProcessError error) {
        if (stopping_) return;
        if (error == QProcess::FailedToStart) {
            reportFailure(
                QStringLiteral("%1 啟動失敗，請確認那個路徑指到的是可執行檔。").arg(agent));
        }
    });

    connect(process_, &QProcess::finished, this, [this, agent](int exitCode, QProcess::ExitStatus) {
        consumeOutput();

        QProcess* finished = process_;
        process_ = nullptr;
        finished->deleteLater();

        // The agent exiting on its own says nothing about the shim's child, so
        // the job has to come down here too.
        closeJob();

        if (stopping_) {
            emit closed(QStringLiteral("已關閉對外通道。"));
            return;
        }
        if (!settled_) {
            const QString explained = explainAgentError(config_.provider, logTail_);
            if (!explained.isEmpty()) {
                reportFailure(explained);
            } else if (logTail_.isEmpty()) {
                reportFailure(QStringLiteral("%1 沒有建立通道就結束了（結束碼 %2）。")
                                  .arg(agent)
                                  .arg(exitCode));
            } else {
                // Nothing recognised, so quote the agent verbatim rather than
                // inventing a reason -- an unfamiliar error is still a lead.
                reportFailure(QStringLiteral("%1 沒有建立通道就結束了：\n%2")
                                  .arg(agent, logTail_));
            }
            return;
        }
        publicUrl_.clear();

        if (autoReconnect_) {
            scheduleReconnect();
            return;
        }
        emit closed(QStringLiteral("%1 結束了（結束碼 %2），對外通道已中斷。")
                        .arg(agent)
                        .arg(exitCode));
    });

    process_->start(config_.binaryPath, agentArguments());

#ifdef _WIN32
    // Put the agent in a job before it has finished starting up, so whatever it
    // spawns is born inside the job too. Verified necessary: the Chocolatey
    // install of ngrok is a 384 KB shim whose only job is to launch the real
    // binary, and killing the shim alone leaves the tunnel serving traffic.
    if (process_->waitForStarted(5000)) {
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                    sizeof(limits));

            const HANDLE child =
                OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                            static_cast<DWORD>(process_->processId()));
            if (child) {
                if (AssignProcessToJobObject(job, child)) {
                    jobHandle_ = job;
                } else {
                    CloseHandle(job);
                }
                CloseHandle(child);
            } else {
                CloseHandle(job);
            }
        }
    }
#endif
}

void TunnelController::closeJob() {
#ifdef _WIN32
    if (!jobHandle_) return;
    const HANDLE job = static_cast<HANDLE>(jobHandle_);
    jobHandle_ = nullptr;
    // Terminate rather than relying on kill-on-close alone, so the tree is gone
    // by the time this returns and the panel's "closed" is the truth.
    TerminateJobObject(job, 1);
    CloseHandle(job);
#endif
}

void TunnelController::stop() {
    // A deliberate stop cancels any pending retry: otherwise the tunnel the
    // user just closed reopens itself a minute later.
    if (reconnectTimer_) reconnectTimer_->stop();
    reconnectAttempt_ = 0;

    if (!process_) {
        closeJob();
        return;
    }

    stopping_ = true;
    publicUrl_.clear();

    process_->terminate();
    if (!process_->waitForFinished(kShutdownGraceMs)) {
        // Takes the shim's child with it, which process_->kill() would not.
        closeJob();
        process_->kill();
        process_->waitForFinished(kShutdownGraceMs);
    }
    closeJob();
}

/// ngrok's success line, verified against 3.19:
///   {"addr":"http://127.0.0.1:8420","lvl":"info","msg":"started tunnel",
///    "name":"command_line","obj":"tunnels","url":"https://xxx.ngrok-free.dev"}
///
/// cloudflared has no JSON log format, and never names the hostname it serves
/// -- that mapping lives in the dashboard -- so the most it can tell us is that
/// it registered with an edge. The address comes from the config instead.
bool TunnelController::readyFromLogLine(const QString& line, QString& url) const {
    if (config_.provider == TunnelProvider::Cloudflare) {
        if (!line.contains(QLatin1String("Registered tunnel connection")) &&
            !line.contains(QLatin1String("Connection registered"))) {
            return false;
        }
        QString hostname = config_.hostname;
        if (!hostname.startsWith(QLatin1String("http"))) {
            hostname.prepend(QStringLiteral("https://"));
        }
        url = hostname;
        return true;
    }

    const json parsed = json::parse(line.toStdString(), nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
        auto found = parsed.find("url");
        if (found != parsed.end() && found->is_string()) {
            const QString value = QString::fromStdString(found->get<std::string>());
            if (value.startsWith(QLatin1String("https://"))) {
                url = value;
                return true;
            }
        }
    }

    // Fallback for a log that is not the JSON shape we asked for -- an older
    // agent, or a future one that renames the field. Losing the URL would make
    // a working tunnel look broken, so it is worth a second way of finding it.
    static const QRegularExpression pattern(
        QStringLiteral(R"(https://[A-Za-z0-9.\-]+\.ngrok[A-Za-z0-9.\-]*\.(?:app|io|dev)\b)"));
    const QRegularExpressionMatch match = pattern.match(line);
    if (match.hasMatch()) {
        url = match.captured(0);
        return true;
    }
    return false;
}

void TunnelController::consumeOutput() {
    if (!process_) return;

    buffer_ += QString::fromUtf8(process_->readAll());

    int newline = 0;
    while ((newline = buffer_.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = buffer_.left(newline).trimmed();
        buffer_.remove(0, newline + 1);
        if (line.isEmpty()) continue;

        logTail_ += line;
        logTail_ += QLatin1Char('\n');
        if (logTail_.size() > kLogTailChars) {
            logTail_ = logTail_.right(kLogTailChars);
        }

        if (settled_) continue;

        QString url;
        if (readyFromLogLine(line, url) && !url.isEmpty()) {
            publicUrl_ = url;
            settled_ = true;
            // A tunnel that came back resets the backoff, so the next drop is
            // retried promptly rather than at whatever delay the last outage
            // had escalated to.
            reconnectAttempt_ = 0;
            emit opened(url);
            QTimer::singleShot(kReachabilityDelayMs, this, &TunnelController::verifyReachable);
        }
    }
}

void TunnelController::verifyReachable() {
    if (!process_ || publicUrl_.isEmpty()) return;

    // Asks the public address for our own health endpoint. For Cloudflare this
    // is the only way to catch the mistake that costs the most: the tunnel
    // connects, the panel says it is open, and the dashboard is pointed at some
    // other port -- so nothing works and nothing says so until a caller fails
    // at three in the morning.
    auto* network = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(publicUrl_ + QStringLiteral("/api/v1/health")));
    request.setTransferTimeout(kReachabilityTimeoutMs);
    // ngrok interposes a warning page on browser-looking requests; this opts out
    // of it the same way a caller's client would have to.
    request.setRawHeader("ngrok-skip-browser-warning", "1");

    QNetworkReply* reply = network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, network] {
        reply->deleteLater();
        network->deleteLater();

        // Stopped or dropped while the check was in flight: nothing to report.
        if (!process_ || publicUrl_.isEmpty()) return;

        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit unreachable(QStringLiteral("通道開著，但從公開網址連不回這台電腦：%1")
                                 .arg(reply->errorString()));
            return;
        }
        if (!body.contains("\"status\"")) {
            emit unreachable(QStringLiteral(
                "通道開著，但公開網址回應的不是這個 API。"
                "請確認它指向 http://127.0.0.1:%1。")
                                 .arg(config_.port));
        }
    });
}

void TunnelController::reportFailure(const QString& reason) {
    settled_ = true;
    publicUrl_.clear();
    emit failed(reason);
}

}  // namespace rpa::studio
