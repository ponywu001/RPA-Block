#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace rpa::studio {

namespace {

constexpr const char* kOrganisation = "SuChenAI";
constexpr const char* kApplication = "RPA-Block";

/// What the application was called before. Kept so a rename does not silently
/// orphan an existing install's API keys, port, theme and project folder -- from
/// the user's side that looks like the app forgot everything.
constexpr const char* kLegacyApplication = "PRA-compiler";

#ifdef _WIN32
const wchar_t* kCredentialTarget = L"RPA-Block/ai-gateway";
const wchar_t* kLegacyCredentialTarget = L"PRA-compiler/ai-gateway";
#endif

/// Copy every key from the pre-rename settings, once.
///
/// Runs only when the new location is empty, so it can never overwrite settings
/// the user has since changed, and it is a no-op on a fresh install.
void migrateLegacySettings() {
    QSettings current(QString::fromLatin1(kOrganisation), QString::fromLatin1(kApplication));
    if (!current.allKeys().isEmpty()) return;

    QSettings legacy(QString::fromLatin1(kOrganisation),
                     QString::fromLatin1(kLegacyApplication));
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty()) return;

    for (const QString& key : keys) current.setValue(key, legacy.value(key));
    current.sync();
}

}  // namespace

QString AppSettings::defaultProjectDirectory() {
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(documents).filePath(QStringLiteral("RPA-Block"));
}

void AppSettings::load() {
    migrateLegacySettings();

    QSettings store(QString::fromLatin1(kOrganisation), QString::fromLatin1(kApplication));

    store.beginGroup(QStringLiteral("ai"));
    gatewayUrl = store.value(QStringLiteral("gatewayUrl"), gatewayUrl).toString();
    assistantId = store.value(QStringLiteral("assistantId"), assistantId).toString();
    signInUrl = store.value(QStringLiteral("signInUrl"), signInUrl).toString();
    authMode = store.value(QStringLiteral("authMode"), 0).toInt() == 1 ? ai::AuthMode::Jwt
                                                                      : ai::AuthMode::ApiKey;
    provider = store.value(QStringLiteral("provider"), provider).toString();
    model = store.value(QStringLiteral("model"), model).toString();
    aiTimeoutMs = store.value(QStringLiteral("timeoutMs"), aiTimeoutMs).toInt();
    store.endGroup();

    store.beginGroup(QStringLiteral("appearance"));
    themeMode = store.value(QStringLiteral("theme"), themeMode).toString();
    windowGeometry = store.value(QStringLiteral("windowGeometry")).toByteArray();
    windowState = store.value(QStringLiteral("windowState")).toByteArray();
    store.endGroup();

    store.beginGroup(QStringLiteral("vision"));
    ocrModelDirectory = store.value(QStringLiteral("modelDirectory"), ocrModelDirectory).toString();
    ocrLanguage = store.value(QStringLiteral("language"), ocrLanguage).toString();
    templateThreshold = store.value(QStringLiteral("threshold"), templateThreshold).toDouble();
    loadOcrOnStartup = store.value(QStringLiteral("loadOnStartup"), loadOcrOnStartup).toBool();
    store.endGroup();

    store.beginGroup(QStringLiteral("api"));
    apiBindAddress = store.value(QStringLiteral("bindAddress"), apiBindAddress).toString();
    apiPort = store.value(QStringLiteral("port"), apiPort).toInt();
    apiAutoStart = store.value(QStringLiteral("autoStart"), apiAutoStart).toBool();
    apiKeys = store.value(QStringLiteral("keys"), apiKeys).toStringList();
    publishDirectory = store.value(QStringLiteral("publishDirectory"), publishDirectory).toString();
    runHistoryPath = store.value(QStringLiteral("runHistoryPath"), runHistoryPath).toString();
    store.endGroup();

    store.beginGroup(QStringLiteral("recorder"));
    clickCaptureRadius = store.value(QStringLiteral("clickRadius"), clickCaptureRadius).toInt();
    captureElementInfo =
        store.value(QStringLiteral("captureElementInfo"), captureElementInfo).toBool();
    store.endGroup();

    store.beginGroup(QStringLiteral("shortcuts"));
    recordShortcut = store.value(QStringLiteral("record"), recordShortcut).toString();
    abortShortcut = store.value(QStringLiteral("abort"), abortShortcut).toString();
    store.endGroup();

    store.beginGroup(QStringLiteral("workspace"));
    projectDirectory = store.value(QStringLiteral("projectDirectory"), projectDirectory).toString();
    recentFiles = store.value(QStringLiteral("recentFiles"), recentFiles).toStringList();
    store.endGroup();

    if (projectDirectory.isEmpty()) projectDirectory = defaultProjectDirectory();
    if (publishDirectory.isEmpty()) {
        publishDirectory = QDir(projectDirectory).filePath(QStringLiteral("published"));
    }
    if (runHistoryPath.isEmpty()) {
        runHistoryPath = QDir(projectDirectory).filePath(QStringLiteral("runs.jsonl"));
    }
}

void AppSettings::save() const {
    QSettings store(QString::fromLatin1(kOrganisation), QString::fromLatin1(kApplication));

    store.beginGroup(QStringLiteral("ai"));
    store.setValue(QStringLiteral("gatewayUrl"), gatewayUrl);
    store.setValue(QStringLiteral("assistantId"), assistantId);
    store.setValue(QStringLiteral("signInUrl"), signInUrl);
    store.setValue(QStringLiteral("authMode"), authMode == ai::AuthMode::Jwt ? 1 : 0);
    store.setValue(QStringLiteral("provider"), provider);
    store.setValue(QStringLiteral("model"), model);
    store.setValue(QStringLiteral("timeoutMs"), aiTimeoutMs);
    store.endGroup();

    store.beginGroup(QStringLiteral("appearance"));
    store.setValue(QStringLiteral("theme"), themeMode);
    store.setValue(QStringLiteral("windowGeometry"), windowGeometry);
    store.setValue(QStringLiteral("windowState"), windowState);
    store.endGroup();

    store.beginGroup(QStringLiteral("vision"));
    store.setValue(QStringLiteral("modelDirectory"), ocrModelDirectory);
    store.setValue(QStringLiteral("language"), ocrLanguage);
    store.setValue(QStringLiteral("threshold"), templateThreshold);
    store.setValue(QStringLiteral("loadOnStartup"), loadOcrOnStartup);
    store.endGroup();

    store.beginGroup(QStringLiteral("api"));
    store.setValue(QStringLiteral("bindAddress"), apiBindAddress);
    store.setValue(QStringLiteral("port"), apiPort);
    store.setValue(QStringLiteral("autoStart"), apiAutoStart);
    store.setValue(QStringLiteral("keys"), apiKeys);
    store.setValue(QStringLiteral("publishDirectory"), publishDirectory);
    store.setValue(QStringLiteral("runHistoryPath"), runHistoryPath);
    store.endGroup();

    store.beginGroup(QStringLiteral("recorder"));
    store.setValue(QStringLiteral("clickRadius"), clickCaptureRadius);
    store.setValue(QStringLiteral("captureElementInfo"), captureElementInfo);
    store.endGroup();

    store.beginGroup(QStringLiteral("shortcuts"));
    store.setValue(QStringLiteral("record"), recordShortcut);
    store.setValue(QStringLiteral("abort"), abortShortcut);
    store.endGroup();

    store.beginGroup(QStringLiteral("workspace"));
    store.setValue(QStringLiteral("projectDirectory"), projectDirectory);
    store.setValue(QStringLiteral("recentFiles"), recentFiles);
    store.endGroup();
}

#ifdef _WIN32

QString AppSettings::aiSecret() const {
    // Falls back to the pre-rename target, so an existing install keeps its
    // gateway key instead of silently coming up unauthenticated. Nothing is
    // rewritten here -- the next real Settings commit moves it across, and until
    // then the old entry is left where its owner can still see it.
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(kCredentialTarget, CRED_TYPE_GENERIC, 0, &credential) &&
        !CredReadW(kLegacyCredentialTarget, CRED_TYPE_GENERIC, 0, &credential)) {
        return {};
    }

    QString secret;
    if (credential->CredentialBlob && credential->CredentialBlobSize > 0) {
        secret = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                   static_cast<int>(credential->CredentialBlobSize));
    }
    CredFree(credential);
    return secret;
}

void AppSettings::setAiSecret(const QString& secret) const {
    if (secret.isEmpty()) {
        CredDeleteW(kCredentialTarget, CRED_TYPE_GENERIC, 0);
        return;
    }

    const QByteArray utf8 = secret.toUtf8();

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(kCredentialTarget);
    credential.CredentialBlobSize = static_cast<DWORD>(utf8.size());
    credential.CredentialBlob =
        reinterpret_cast<LPBYTE>(const_cast<char*>(utf8.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;

    CredWriteW(&credential, 0);
}

#else

QString AppSettings::aiSecret() const {
    QSettings store(QString::fromLatin1(kOrganisation), QString::fromLatin1(kApplication));
    return store.value(QStringLiteral("ai/secret")).toString();
}

void AppSettings::setAiSecret(const QString& secret) const {
    QSettings store(QString::fromLatin1(kOrganisation), QString::fromLatin1(kApplication));
    store.setValue(QStringLiteral("ai/secret"), secret);
}

#endif  // _WIN32

ai::AgentSettings AppSettings::toAgentSettings() const {
    ai::AgentSettings settings;
    settings.gatewayUrl = gatewayUrl.toStdString();
    settings.assistantId = assistantId.toStdString();
    settings.signInUrl = signInUrl.toStdString();
    settings.authMode = authMode;
    settings.provider = provider.toStdString();
    settings.model = model.toStdString();
    settings.timeoutMs = aiTimeoutMs;

    const QString secret = aiSecret();
    if (authMode == ai::AuthMode::Jwt) {
        settings.accessToken = secret.toStdString();
    } else {
        settings.apiKey = secret.toStdString();
    }
    return settings;
}

QString AppSettings::bundledOcrModelDirectory() {
    const QDir directory(QCoreApplication::applicationDirPath() + QStringLiteral("/models"));
    if (!directory.exists()) return {};

    // All three or nothing: a partial folder would fail at load with a confusing
    // "missing file" error while looking like a configured setup.
    for (const char* required : {"det.onnx", "rec.onnx", "keys.txt"}) {
        if (!directory.exists(QString::fromLatin1(required))) return {};
    }
    return directory.absolutePath();
}

QString AppSettings::resolvedOcrModelDirectory() const {
    if (!ocrModelDirectory.isEmpty()) return ocrModelDirectory;
    return bundledOcrModelDirectory();
}

vision::OcrConfig AppSettings::toOcrConfig() const {
    vision::OcrConfig config;
    config.modelDirectory = resolvedOcrModelDirectory().toStdString();
    return config;
}

server::ApiServerConfig AppSettings::toServerConfig() const {
    server::ApiServerConfig config;
    config.bindAddress = apiBindAddress.toStdString();
    config.port = apiPort;
    for (const QString& key : apiKeys) config.apiKeys.insert(key.toStdString());
    return config;
}

}  // namespace rpa::studio
