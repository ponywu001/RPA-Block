#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QStyleFactory>
#include <QTimer>

#include "MainWindow.h"
#include "StandardButtons.h"
#include "ScreenGeometryDump.h"

int main(int argc, char** argv) {
    // PassThrough keeps the exact fractional scale factor instead of rounding
    // 1.25 up to 2, so the UI matches the display's real scaling. It does NOT
    // disable Qt's high-DPI scaling — nothing does in Qt 6 — so Qt coordinates
    // stay logical while the executor and screen capture work in physical
    // pixels. TargetPickerOverlay converts between the two; anything else that
    // mixes Qt geometry with screen coordinates has to do the same.
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("RPA-Block"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("SuChenAI"));

    // Compiled into the binary, so it survives being unpacked from the packaged
    // single exe with no file to find on disk.
    app.setWindowIcon(QIcon(QStringLiteral(":/app.ico")));

    // Installed before any dialog exists, so Qt's own OK/Cancel/Yes/No come
    // out in Chinese like the rest of the interface.
    static rpa::studio::StandardButtonTranslator buttonTranslator;
    app.installTranslator(&buttonTranslator);

    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"))) {
        app.setStyle(QStringLiteral("Fusion"));
    }

    // Accepting a path lets a .rpa.json be opened by double-click or from a
    // shortcut, instead of only through the in-app file dialog.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("RPA-Block — 積木式 RPA 流程編輯器"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("流程檔"),
                                 QStringLiteral("啟動時要開啟的 .rpa.json（選填）"));
    const QCommandLineOption pickTargetOption(
        QStringLiteral("pick-target"),
        QStringLiteral("啟動後直接開啟畫面目標選取器，用來測試 OCR 與圖片比對的擷取。"));
    parser.addOption(pickTargetOption);
    const QCommandLineOption editVariablesOption(
        QStringLiteral("edit-variables"),
        QStringLiteral("啟動後直接開啟流程變數編輯器。"));
    parser.addOption(editVariablesOption);
    const QCommandLineOption dumpGeometryOption(
        QStringLiteral("dump-geometry"),
        QStringLiteral("把螢幕幾何診斷寫到指定檔案後結束。座標問題請先看這份輸出。"),
        QStringLiteral("檔案"));
    parser.addOption(dumpGeometryOption);
    parser.process(app);

    // Coordinate bugs in this app are all about the gap between Qt's logical
    // pixels and the physical pixels the executor clicks in, and that gap is
    // invisible in a screenshot. Dumping both views makes it measurable.
    if (parser.isSet(dumpGeometryOption)) {
        return rpa::studio::dumpScreenGeometry(parser.value(dumpGeometryOption)) ? 0 : 1;
    }

    rpa::studio::MainWindow window;
    window.show();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        const QFileInfo file(positional.first());
        window.openFlowFile(file.absoluteFilePath());
    }

    if (parser.isSet(editVariablesOption)) {
        QTimer::singleShot(0, &window, [&window] { window.showFlowVariables(); });
    }

    if (parser.isSet(pickTargetOption)) {
        // Deferred so the main window is mapped first: the overlay freezes the
        // desktop, and capturing before the window exists would freeze a screen
        // that does not yet show the app.
        QTimer::singleShot(0, &window, [&window] { window.showTargetPicker(); });
    }

    return app.exec();
}
