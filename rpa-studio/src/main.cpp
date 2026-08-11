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
#include "UiaTreeDump.h"

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
    const QCommandLineOption dumpUiaOption(
        QStringLiteral("dump-uia"),
        QStringLiteral("把某個視窗的 UI Automation 控制項樹寫到指定檔案後結束。"
                       "要用 UIA 定位欄位前，先看這份輸出確認那支程式暴露了什麼。"),
        QStringLiteral("檔案"));
    parser.addOption(dumpUiaOption);
    const QCommandLineOption uiaWindowOption(
        QStringLiteral("uia-window"),
        QStringLiteral("--dump-uia 的對象視窗，比對標題（包含即可）。省略則用最前景的視窗。"),
        QStringLiteral("標題"));
    parser.addOption(uiaWindowOption);
    const QCommandLineOption uiaDelayOption(
        QStringLiteral("uia-delay"),
        QStringLiteral("--dump-uia 開始前等待的秒數，讓你有時間把目標視窗切到最前面。"),
        QStringLiteral("秒數"), QStringLiteral("0"));
    parser.addOption(uiaDelayOption);
    const QCommandLineOption probeRelativeOption(
        QStringLiteral("probe-relative"),
        QStringLiteral("測試「某個標籤旁邊的控制項」定位得到嗎，只印結果不點擊。"
                       "搭配 --uia-direction / --uia-element / --uia-delay。"),
        QStringLiteral("標籤文字"));
    parser.addOption(probeRelativeOption);
    const QCommandLineOption uiaDirectionOption(
        QStringLiteral("uia-direction"),
        QStringLiteral("--probe-relative 的方位：right / left / above / below。"),
        QStringLiteral("方位"), QStringLiteral("right"));
    parser.addOption(uiaDirectionOption);
    const QCommandLineOption uiaElementOption(
        QStringLiteral("uia-element"),
        QStringLiteral("--probe-relative 要找的控制項：any / input / button / checkbox。"),
        QStringLiteral("種類"), QStringLiteral("input"));
    parser.addOption(uiaElementOption);
    parser.process(app);

    // Coordinate bugs in this app are all about the gap between Qt's logical
    // pixels and the physical pixels the executor clicks in, and that gap is
    // invisible in a screenshot. Dumping both views makes it measurable.
    if (parser.isSet(dumpGeometryOption)) {
        return rpa::studio::dumpScreenGeometry(parser.value(dumpGeometryOption)) ? 0 : 1;
    }

    // What an application exposes to automation cannot be seen from a
    // screenshot, and a locator written against a guess about it works only on
    // the machine it was written on.
    if (parser.isSet(dumpUiaOption)) {
        return rpa::studio::dumpUiaTree(parser.value(dumpUiaOption),
                                        parser.value(uiaWindowOption),
                                        parser.value(uiaDelayOption).toInt())
                   ? 0
                   : 1;
    }

    if (parser.isSet(probeRelativeOption)) {
        return rpa::studio::probeRelativeTarget(parser.value(probeRelativeOption),
                                                parser.value(uiaDirectionOption),
                                                parser.value(uiaElementOption), 400,
                                                parser.value(uiaDelayOption).toInt(),
                                                parser.value(uiaWindowOption))
                   ? 0
                   : 1;
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
