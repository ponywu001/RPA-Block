#pragma once

#include <QTranslator>

namespace rpa::studio {

/// Traditional Chinese for the button text Qt supplies itself.
///
/// QDialogButtonBox and QMessageBox do not take their labels from us -- they ask
/// QPlatformTheme, which resolves them through Qt's own translation catalogue.
/// This build has no catalogue: aqtinstall fetches qtbase without the
/// translations module, so every dialog in an otherwise all-Chinese interface
/// ends in "OK" and "Cancel".
///
/// Shipping qtbase_zh_TW.qm would fix it too, but this is a closed set of about
/// twenty strings, and doing it here keeps the package self-contained and lets
/// the wording match the rest of the app (確定 rather than 好).
class StandardButtonTranslator : public QTranslator {
    Q_OBJECT

public:
    using QTranslator::QTranslator;

    QString translate(const char* context, const char* sourceText, const char* disambiguation,
                      int n) const override;
    bool isEmpty() const override { return false; }
};

}  // namespace rpa::studio
