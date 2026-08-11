#pragma once

#include <QDialog>

#include "AppSettings.h"
#include "rpa/server/ApiServer.h"
#include "rpa/server/RunStore.h"
#include "rpa/core/ScriptIO.h"
#include "rpa/server/ScriptRepository.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace rpa::studio {

class TunnelController;

/// Screen 4: start/stop the REST server, manage keys, published flows, history.
class ApiPanelDialog : public QDialog {
    Q_OBJECT

public:
    ApiPanelDialog(AppSettings* settings,
                   server::ApiServer* api,
                   server::ScriptRepository* repository,
                   server::RunStore* runStore,
                   TunnelController* tunnel,
                   QWidget* parent = nullptr);

signals:
    /// Fires when keys or the bind configuration change, so the host can persist.
    void settingsChanged();
    /// The user asked to publish whatever is currently open in the editor.
    void publishCurrentFlowRequested();

public slots:
    void refresh();

    /// Tell the panel what is open on the canvas, so it can flag a published
    /// snapshot that no longer matches.
    void setCurrentFlow(const core::Script& script);

private:
    void buildUi();
    void toggleServer();
    void generateKey();
    void revokeSelectedKey();
    void copySelectedKey();
    void copyCurlForSelectedScript();
    void copyPowerShellForSelectedScript();
    void unpublishSelectedScript();
    void refreshStatus();
    void toggleTunnel();
    void browseForTunnelBinary();
    void refreshTunnel();
    /// Re-label the provider-specific fields and reload their saved values.
    void applyTunnelProvider();
    void refreshKeys();
    void refreshScripts();
    void refreshRuns();
    /// True when the flow open on the canvas would publish under this id but
    /// differs from what is published. Publishing is a snapshot, and a stale
    /// one is invisible otherwise.
    bool isStale(const server::PublishedScript& published) const;
    /// Explains an empty `variables` object or a stale snapshot, or nothing.
    QString parameterNote(const QString& scriptId) const;

    QString curlFor(const QString& scriptId) const;
    QString powerShellFor(const QString& scriptId) const;
    /// The flow's inputs as a JSON object body, defaults filled in.
    QString variablesJson(const QString& scriptId) const;
    QString endpointFor(const QString& scriptId) const;
    QString apiKeyOrPlaceholder() const;

    AppSettings* settings_;
    server::ApiServer* api_;
    server::ScriptRepository* repository_;
    /// The flow on the canvas, for the staleness comparison.
    core::Script currentFlow_;
    server::RunStore* runStore_;
    TunnelController* tunnel_;

    QLabel* statusLabel_;
    QPushButton* toggleButton_;
    QComboBox* bindCombo_;
    QSpinBox* portSpin_;
    QCheckBox* autoStartCheck_;

    QLabel* tunnelStatusLabel_;
    QPushButton* tunnelButton_;
    QComboBox* tunnelProviderCombo_;
    QLineEdit* tunnelTokenEdit_;
    QLabel* tunnelHostnameLabel_;
    QLineEdit* tunnelHostnameEdit_;
    QLineEdit* tunnelBinaryEdit_;
    QCheckBox* tunnelAutoStartCheck_;
    QPushButton* tunnelCopyUrlButton_;
    QPushButton* tunnelDownloadButton_;
    QLabel* tunnelNote_;

    QListWidget* keyList_;
    QListWidget* scriptList_;
    QTableWidget* runTable_;
    QTimer* refreshTimer_;
};

}  // namespace rpa::studio
