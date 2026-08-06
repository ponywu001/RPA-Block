#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "rpa/ai/AgentTypes.h"
#include "rpa/server/ApiServer.h"
#include "rpa/vision/OcrEngine.h"

namespace rpa::studio {

/// Everything the settings page edits, persisted through QSettings.
///
/// Secrets are the exception: the API key and JWT go to the Windows Credential
/// Manager instead of the registry, so they are not sitting in plain text next
/// to the rest of the configuration.
struct AppSettings {
    // AI gateway
    QString gatewayUrl = QStringLiteral("https://agents.scfg.io/structured-multimodal-agent");
    QString assistantId = QStringLiteral("structured-multimodal-agent");
    QString signInUrl = QStringLiteral("https://api.cluster.scfg.io/public/auth/signin");
    ai::AuthMode authMode = ai::AuthMode::ApiKey;
    QString provider = QStringLiteral("claude");
    QString model;  // empty means "use the gateway default"
    int aiTimeoutMs = 120000;

    // Appearance
    QString themeMode = QStringLiteral("system");

    // Vision
    QString ocrModelDirectory;
    QString ocrLanguage = QStringLiteral("chinese_traditional+english");
    double templateThreshold = 0.85;
    bool loadOcrOnStartup = true;

    // REST API
    QString apiBindAddress = QStringLiteral("127.0.0.1");
    int apiPort = 8420;
    bool apiAutoStart = false;
    QStringList apiKeys;
    QString publishDirectory;
    QString runHistoryPath;

    // Recorder
    int clickCaptureRadius = 120;
    bool captureElementInfo = true;

    // Shortcuts
    QString recordShortcut = QStringLiteral("Ctrl+Shift+R");
    QString abortShortcut = QStringLiteral("F12");

    // Window layout, so a deliberate arrangement survives a restart.
    QByteArray windowGeometry;
    QByteArray windowState;

    // Workspace
    QString projectDirectory;
    QStringList recentFiles;

    void load();
    void save() const;

    /// Credentials, read from and written to the OS credential store.
    QString aiSecret() const;
    void setAiSecret(const QString& secret) const;

    ai::AgentSettings toAgentSettings() const;
    vision::OcrConfig toOcrConfig() const;
    server::ApiServerConfig toServerConfig() const;

    /// Default project folder under the user's Documents directory.
    static QString defaultProjectDirectory();

    /// The `models/` folder shipped beside the executable, or empty when it is
    /// absent or incomplete. Lets OCR work straight out of the packaged exe with
    /// nothing to download and nothing to configure.
    static QString bundledOcrModelDirectory();

    /// Where OCR models are actually loaded from: the configured folder when set,
    /// otherwise the bundled one. Keeping this in one place stops the settings
    /// page and the loader from disagreeing about which models are in use.
    QString resolvedOcrModelDirectory() const;
};

}  // namespace rpa::studio
