#include "ScreenGeometryDump.h"

#include <QFile>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QTextStream>

#include "rpa/core/Geometry.h"
#include "rpa/vision/ScreenCapture.h"

namespace rpa::studio {

bool dumpScreenGeometry(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);

    out << "=== Qt logical view (what setGeometry and mouse events use) ===\n";
    QRect logicalUnion;
    std::vector<core::Rect> logicalScreens;
    for (const QScreen* screen : QGuiApplication::screens()) {
        const QRect geometry = screen->geometry();
        logicalUnion = logicalUnion.united(geometry);
        logicalScreens.push_back(
            core::Rect{geometry.x(), geometry.y(), geometry.width(), geometry.height()});
        out << "  screen '" << screen->name() << "'\n"
            << "    geometry            " << geometry.width() << "x" << geometry.height() << " at ("
            << geometry.x() << "," << geometry.y() << ")\n"
            << "    devicePixelRatio    " << screen->devicePixelRatio() << "\n"
            << "    logicalDotsPerInch  " << screen->logicalDotsPerInch() << "\n"
            << "    physicalDotsPerInch " << screen->physicalDotsPerInch() << "\n";
    }
    out << "  union                 " << logicalUnion.width() << "x" << logicalUnion.height()
        << " at (" << logicalUnion.x() << "," << logicalUnion.y() << ")\n";

    out << "\n=== physical view (what ScreenCapture and the executor use) ===\n";
    const core::Rect desktop = vision::ScreenCapture::virtualDesktopBounds();
    const std::vector<core::Rect> monitors = vision::ScreenCapture::monitors();
    out << "  virtualDesktopBounds  " << desktop.width << "x" << desktop.height << " at ("
        << desktop.x << "," << desktop.y << ")\n";

    std::string captureError;
    const cv::Mat capture = vision::ScreenCapture::grab(std::nullopt, captureError);
    if (capture.empty()) {
        out << "  grab()                FAILED: " << QString::fromStdString(captureError) << "\n";
    } else {
        out << "  grab() size           " << capture.cols << "x" << capture.rows << "\n";
    }

    out << "  monitors()            " << monitors.size() << " found\n";
    for (const core::Rect& monitor : monitors) {
        out << "    " << monitor.width << "x" << monitor.height << " at (" << monitor.x << ","
            << monitor.y << ")\n";
    }

    // The capture must match the physical bounds, or drawing it across the
    // overlay stretches it and every pick is off by that ratio.
    if (!capture.empty()) {
        const bool consistent = capture.cols == desktop.width && capture.rows == desktop.height;
        out << "  capture matches bounds " << (consistent ? "yes" : "NO -- the frozen image will "
                                                                   "be scaled and misregistered")
            << "\n";
    }

    out << "\n=== per-screen mapping (what the target picker uses) ===\n";
    const std::vector<core::ScreenPairing> pairings = core::pairScreens(logicalScreens, monitors);
    if (pairings.empty()) {
        out << "  PAIRING FAILED -- Qt and Windows disagree on the monitor list, so the\n"
               "  picker falls back to one cover over the whole desktop. On a mixed-DPI\n"
               "  setup that cover is imprecise.\n";
    } else {
        for (const core::ScreenPairing& pairing : pairings) {
            const core::ScaleMapping mapping = pairing.mapping();
            out << "  logical " << pairing.logical.width << "x" << pairing.logical.height << " at ("
                << pairing.logical.x << "," << pairing.logical.y << ")"
                << "  ->  physical " << pairing.physical.width << "x" << pairing.physical.height
                << " at (" << pairing.physical.x << "," << pairing.physical.y << ")\n"
                << "    scale               " << mapping.scaleX() << " x " << mapping.scaleY()
                << "\n";
            const core::Rect probe = mapping.toPhysical(100, 100, 50, 50);
            out << "    local (100,100 50x50) -> physical (" << probe.x << "," << probe.y << " "
                << probe.width << "x" << probe.height << ")\n";
        }
    }

    out << "\n=== for comparison: one mapping across the whole desktop ===\n";
    const core::ScaleMapping wholeDesktop{desktop, logicalUnion.width(), logicalUnion.height()};
    out << "  scale                 " << wholeDesktop.scaleX() << " x " << wholeDesktop.scaleY()
        << "\n";
    if (pairings.size() > 1) {
        out << "  This is the average, and it matches no individual monitor above. Using it\n"
               "  for the whole desktop is what misregistered the picker; the per-screen\n"
               "  mappings replaced it.\n";
    }

    file.close();
    return true;
}

}  // namespace rpa::studio
