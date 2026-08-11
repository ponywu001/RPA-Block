#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

namespace rpa::studio {

enum class TunnelProvider {
    Ngrok,
    Cloudflare,
};

QString toString(TunnelProvider provider);
/// Product name for messages, so errors read "ngrok ..." / "cloudflared ..."
/// rather than naming a class the user has never heard of.
QString agentName(TunnelProvider provider);

struct TunnelConfig {
    TunnelProvider provider = TunnelProvider::Ngrok;
    QString binaryPath;
    /// The local API port. Only ngrok is told this: a token-based Cloudflare
    /// tunnel takes its service address from the dashboard instead, which is
    /// the one thing about that provider you cannot configure from here.
    int port = 0;
    /// Required. Both providers refuse to run without one, and so do we --
    /// opening a tunnel is not something to do on a half-filled form.
    QString authToken;
    /// ngrok: an optional reserved domain. Cloudflare: the public hostname set
    /// up in the dashboard, which is required because the agent never reports
    /// it and there would otherwise be no address to show or copy.
    QString hostname;
};

/// Runs a tunnel agent alongside the REST server so the API can be reached from
/// outside this machine without touching a router or a firewall.
///
/// Neither agent is shipped with this application. ngrok's terms only allow
/// redistributing it when *we* hold the account rather than the user, and the
/// design here is that the user brings their own token -- so both binaries are
/// located on the machine instead of bundled.
class TunnelController : public QObject {
    Q_OBJECT

public:
    explicit TunnelController(QObject* parent = nullptr);
    ~TunnelController() override;

    void start(const TunnelConfig& config);
    void stop();

    /// Bring the tunnel back by itself after the agent drops.
    ///
    /// Needed for a machine that is meant to stay reachable: without this, a
    /// dropped connection means the endpoint silently stops answering until
    /// somebody notices and clicks the button. Reconnects only follow an
    /// unexpected exit -- stopping on purpose stays stopped.
    void setAutoReconnect(bool on) { autoReconnect_ = on; }
    bool autoReconnect() const { return autoReconnect_; }

    bool isRunning() const;
    /// True while waiting to retry after a drop.
    bool isReconnecting() const;
    QString publicUrl() const { return publicUrl_; }
    TunnelProvider provider() const { return config_.provider; }

    /// The agent's executable beside this application or on PATH; empty when
    /// absent.
    static QString findBinary(TunnelProvider provider);

signals:
    void opened(const QString& publicUrl);
    /// The agent dropped and a retry is scheduled `seconds` from now.
    void reconnecting(int seconds, int attempt);
    /// The tunnel went away -- stopped on request, or the agent exited.
    void closed(const QString& reason);
    void failed(const QString& reason);
    /// The tunnel is up but a request to it did not come back to this server.
    /// Its own address is the only thing that can tell us, so it is reported
    /// separately from failed(): the tunnel is running, it just is not useful.
    void unreachable(const QString& detail);

private:
    void consumeOutput();
    /// Whether this line says the tunnel is now serving, and through what URL.
    bool readyFromLogLine(const QString& line, QString& url) const;
    void reportFailure(const QString& reason);
    void scheduleReconnect();
    void verifyReachable();
    QStringList agentArguments() const;

    /// Kills the agent *and* anything it started. Package managers install
    /// these agents as a small shim that launches the real binary as a child,
    /// so terminating the process we launched leaves the tunnel wide open while
    /// the UI reports it closed. A Windows job object closes the whole tree,
    /// and because it is set to kill on close it also takes the tunnel down if
    /// this application dies without getting to stop().
    ///
    /// Typed as void* to keep <windows.h> out of this header.
    void* jobHandle_ = nullptr;
    void closeJob();

    QProcess* process_ = nullptr;
    QTimer* reconnectTimer_ = nullptr;
    bool autoReconnect_ = false;
    int reconnectAttempt_ = 0;

    /// The last start()'s settings, so a reconnect can repeat it without the
    /// caller having to stay alive to feed them back in.
    TunnelConfig config_;

    QString publicUrl_;
    QString buffer_;
    /// The agent's last few lines, so an exit without a tunnel can quote a
    /// reason instead of just saying it failed.
    QString logTail_;
    /// Set once opened() or failed() has fired, so the exit handler does not
    /// report a second outcome for the same run.
    bool settled_ = false;
    bool stopping_ = false;
};

}  // namespace rpa::studio
