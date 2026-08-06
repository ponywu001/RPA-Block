#include "StandardButtons.h"

#include <QHash>
#include <QString>

namespace rpa::studio {

namespace {

/// Keyed on the exact source strings Qt passes to the translator. The ampersand
/// forms are the ones QPlatformTheme actually uses for most buttons; the plain
/// forms appear in a few places, so both are listed rather than stripped, which
/// would lose the mnemonic Qt expects to find.
const QHash<QString, QString>& platformThemeStrings() {
    static const QHash<QString, QString> strings = {
        {QStringLiteral("OK"), QStringLiteral("確定")},
        {QStringLiteral("&OK"), QStringLiteral("確定(&O)")},
        {QStringLiteral("Cancel"), QStringLiteral("取消")},
        {QStringLiteral("&Cancel"), QStringLiteral("取消(&C)")},
        {QStringLiteral("Yes"), QStringLiteral("是")},
        {QStringLiteral("&Yes"), QStringLiteral("是(&Y)")},
        {QStringLiteral("Yes to &All"), QStringLiteral("全部皆是(&A)")},
        {QStringLiteral("No"), QStringLiteral("否")},
        {QStringLiteral("&No"), QStringLiteral("否(&N)")},
        {QStringLiteral("N&o to All"), QStringLiteral("全部皆否(&O)")},
        {QStringLiteral("Save"), QStringLiteral("儲存")},
        {QStringLiteral("&Save"), QStringLiteral("儲存(&S)")},
        {QStringLiteral("Save All"), QStringLiteral("全部儲存")},
        {QStringLiteral("Open"), QStringLiteral("開啟")},
        {QStringLiteral("&Open"), QStringLiteral("開啟(&O)")},
        {QStringLiteral("Close"), QStringLiteral("關閉")},
        {QStringLiteral("&Close"), QStringLiteral("關閉(&C)")},
        {QStringLiteral("Close without Saving"), QStringLiteral("不儲存就關閉")},
        {QStringLiteral("Discard"), QStringLiteral("捨棄")},
        {QStringLiteral("&Discard"), QStringLiteral("捨棄(&D)")},
        {QStringLiteral("Apply"), QStringLiteral("套用")},
        {QStringLiteral("&Apply"), QStringLiteral("套用(&A)")},
        {QStringLiteral("Reset"), QStringLiteral("重設")},
        {QStringLiteral("&Reset"), QStringLiteral("重設(&R)")},
        {QStringLiteral("Restore Defaults"), QStringLiteral("回復預設值")},
        {QStringLiteral("Help"), QStringLiteral("說明")},
        {QStringLiteral("&Help"), QStringLiteral("說明(&H)")},
        {QStringLiteral("Retry"), QStringLiteral("重試")},
        {QStringLiteral("&Retry"), QStringLiteral("重試(&R)")},
        {QStringLiteral("Ignore"), QStringLiteral("忽略")},
        {QStringLiteral("&Ignore"), QStringLiteral("忽略(&I)")},
        {QStringLiteral("Abort"), QStringLiteral("中止")},
    };
    return strings;
}

}  // namespace

QString StandardButtonTranslator::translate(const char* context, const char* sourceText,
                                            const char* disambiguation, int n) const {
    Q_UNUSED(disambiguation);
    Q_UNUSED(n);

    // Only the platform theme's own strings. Anything else falls through to the
    // empty QString, which tells Qt to use the source text unchanged -- so this
    // cannot accidentally rewrite text the app itself supplies.
    if (qstrcmp(context, "QPlatformTheme") != 0) return {};

    const auto& strings = platformThemeStrings();
    const auto it = strings.constFind(QString::fromUtf8(sourceText));
    return it == strings.constEnd() ? QString{} : it.value();
}

}  // namespace rpa::studio
