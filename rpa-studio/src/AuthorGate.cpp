#include "AuthorGate.h"

#include <QCryptographicHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>

namespace rpa::studio {

namespace {

constexpr const char* kOrganisation = "SuChenAI";
constexpr const char* kApplication = "RPA-Block";

/// Salted so the digest cannot be looked up in a table of common-password
/// hashes, and so a digest lifted from this binary is useless against anything
/// else.
constexpr const char* kSalt = "RPA-Block/author-gate/v1";
constexpr const char* kExpectedDigest =
    "2098187a6df3ed73f50608db1d20d0aabb2a399cf54dcfa750c5f1bdfff1eb6d";

/// Survives a restart on purpose. Re-typing the password at every launch would
/// train an author to keep it somewhere convenient, which is worse than storing
/// the fact that it was once entered correctly.
constexpr const char* kUnlockedKey = "authoring/unlocked";

QString digestOf(const QString& password) {
    QByteArray input(kSalt);
    input += password.toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

QSettings store() {
    return QSettings(QString::fromLatin1(kOrganisation), QString::fromLatin1(kApplication));
}

}  // namespace

bool AuthorGate::unlocked() {
    return store().value(QString::fromLatin1(kUnlockedKey), false).toBool();
}

void AuthorGate::lock() {
    QSettings settings = store();
    settings.setValue(QString::fromLatin1(kUnlockedKey), false);
}

bool AuthorGate::promptToUnlock(QWidget* parent, const QString& reason) {
    if (unlocked()) return true;

    bool accepted = false;
    const QString entered = QInputDialog::getText(
        parent, QStringLiteral("需要編輯密碼"),
        QStringLiteral("%1需要編輯密碼。\n\n這份安裝目前是唯讀的：可以執行與發佈既有流程，"
                       "但不能新增或修改。")
            .arg(reason),
        QLineEdit::Password, QString(), &accepted);

    if (!accepted) return false;

    if (digestOf(entered) != QString::fromLatin1(kExpectedDigest)) {
        QMessageBox::warning(parent, QStringLiteral("密碼不對"),
                             QStringLiteral("這份安裝維持唯讀。"));
        return false;
    }

    QSettings settings = store();
    settings.setValue(QString::fromLatin1(kUnlockedKey), true);
    return true;
}

bool AuthorGate::require(QWidget* parent, const QString& reason) {
    return unlocked() || promptToUnlock(parent, reason);
}

}  // namespace rpa::studio
