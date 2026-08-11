#include "UiaTreeDump.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QThread>

#include "AppSettings.h"
#include "rpa/core/Locator.h"
#include "rpa/recorder/UiaInspector.h"
#include "rpa/recorder/UiaLocator.h"
#include "rpa/vision/VisionLocator.h"

namespace rpa::studio {

namespace {

constexpr int kMaxDepth = 40;
constexpr int kMaxNodes = 4000;

QString quoted(const std::string& value) {
    return value.empty() ? QStringLiteral("-")
                         : QStringLiteral("\"%1\"").arg(QString::fromStdString(value));
}

}  // namespace

bool dumpUiaTree(const QString& path, const QString& titleFilter, int delaySeconds) {
    if (delaySeconds > 0) {
        // Blocking on purpose: this runs before any window exists, and the
        // point of the wait is that the user goes and focuses another
        // application while nothing of ours is in the way.
        QThread::sleep(static_cast<unsigned long>(delaySeconds));
    }

    rpa::recorder::initializeUiaForThread();

    std::string error;
    const auto nodes = rpa::recorder::dumpWindowTree(titleFilter.toStdString(), kMaxDepth,
                                                     kMaxNodes, error);

    rpa::recorder::uninitializeUiaForThread();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(stderr) << QStringLiteral("cannot write %1\n").arg(path);
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "# UI Automation control tree\n";
    out << "# window filter: " << (titleFilter.isEmpty() ? QStringLiteral("<foreground>")
                                                         : titleFilter)
        << "\n";

    if (!error.empty()) {
        out << "# error: " << QString::fromStdString(error) << "\n";
        QTextStream(stderr) << QStringLiteral("UIA dump failed: %1\n")
                                   .arg(QString::fromStdString(error));
        return false;
    }

    out << "# nodes: " << nodes.size() << "\n";
    out << "#\n";
    out << "# columns: <indent>ControlType  name=  id=  class=  labelled-by=  focusable  "
           "bounds=(x,y,w,h)\n";
    out << "#\n";

    // Counted per control type as well, because the first question when a
    // locator finds nothing is whether the app exposes that kind of control at
    // all -- the same reason the OCR dump reports per-stage counts.
    QMap<QString, int> byType;

    for (const auto& node : nodes) {
        const QString type = QString::fromStdString(node.controlType);
        byType[type] += 1;

        out << QString(node.depth * 2, QLatin1Char(' '));
        out << type;
        out << "  name=" << quoted(node.name);
        if (!node.automationId.empty()) out << "  id=" << quoted(node.automationId);
        if (!node.className.empty()) out << "  class=" << quoted(node.className);
        if (!node.labeledBy.empty()) out << "  labelled-by=" << quoted(node.labeledBy);
        if (node.keyboardFocusable) out << "  focusable";
        if (node.offscreen) out << "  offscreen";
        out << QStringLiteral("  bounds=(%1,%2,%3,%4)")
                   .arg(node.bounds.x)
                   .arg(node.bounds.y)
                   .arg(node.bounds.width)
                   .arg(node.bounds.height);
        out << "\n";
    }

    out << "#\n# control types seen:\n";
    for (auto it = byType.constBegin(); it != byType.constEnd(); ++it) {
        out << "#   " << it.key() << ": " << it.value() << "\n";
    }

    QTextStream(stdout) << QStringLiteral("wrote %1 nodes to %2\n").arg(nodes.size()).arg(path);
    return true;
}

bool probeRelativeTarget(const QString& anchor,
                         const QString& direction,
                         const QString& element,
                         int maxDistance,
                         int delaySeconds,
                         const QString& titleFilter) {
    QTextStream out(stdout);

    core::Direction parsedDirection = core::Direction::Right;
    if (!core::parseDirection(direction.toStdString(), parsedDirection)) {
        out << QStringLiteral("unknown direction: %1 (right/left/above/below)\n").arg(direction);
        return false;
    }
    core::ElementRole parsedRole = core::ElementRole::Input;
    if (!core::parseElementRole(element.toStdString(), parsedRole)) {
        out << QStringLiteral("unknown element: %1 (any/input/button/checkbox)\n").arg(element);
        return false;
    }

    if (delaySeconds > 0) QThread::sleep(static_cast<unsigned long>(delaySeconds));

    // The window filter is a testing convenience; at run time the UIA backend
    // reads the foreground window, which the flow's own window_activate step has
    // already arranged.
    if (!titleFilter.isEmpty()) {
        rpa::recorder::initializeUiaForThread();
        const auto scoped = rpa::recorder::findRelativeElement(
            anchor.toStdString(), core::MatchMode::Contains, parsedDirection, parsedRole,
            maxDistance, titleFilter.toStdString());
        rpa::recorder::uninitializeUiaForThread();

        if (!scoped.found) {
            out << QStringLiteral("not found: %1\n")
                       .arg(QString::fromStdString(scoped.diagnosis));
            return false;
        }
        out << QStringLiteral("found   %1  name=\"%2\"\n")
                   .arg(QString::fromStdString(scoped.controlType),
                        QString::fromStdString(scoped.name));
        out << QStringLiteral("via     %1\n")
                   .arg(scoped.strategy == rpa::recorder::UiaMatchStrategy::ByName
                            ? QStringLiteral("uia by-name (the control carries the label)")
                            : QStringLiteral("uia by-geometry (nearest in that direction)"));
        out << QStringLiteral("bounds  (%1,%2,%3,%4)\n")
                   .arg(scoped.bounds.x)
                   .arg(scoped.bounds.y)
                   .arg(scoped.bounds.width)
                   .arg(scoped.bounds.height);
        out << QStringLiteral("click   (%1,%2)   -- not clicked\n")
                   .arg(scoped.bounds.x + scoped.bounds.width / 2)
                   .arg(scoped.bounds.y + scoped.bounds.height / 2);
        return true;
    }

    // No filter: run the same cascade a flow would, so what this prints is what
    // the step will actually do -- including falling through to pixels when the
    // application exposes nothing.
    core::Target target;
    target.kind = core::TargetKind::Relative;
    target.text = anchor.toStdString();
    target.match = core::MatchMode::Contains;
    target.direction = parsedDirection;
    target.role = parsedRole;
    target.maxDistance = maxDistance;

    rpa::recorder::initializeUiaForThread();

    QString strategy;
    rpa::recorder::UiaLocator uia;
    uia.setStrategyReporter([&strategy](const std::string& note) {
        strategy = QString::fromStdString(note);
    });

    vision::VisionLocator vision;
    std::string ocrError;
    vision::OcrConfig ocrConfig;
    ocrConfig.modelDirectory = AppSettings::bundledOcrModelDirectory().toStdString();
    const bool ocrLoaded = vision.loadOcr(ocrConfig, ocrError);

    core::CompositeLocator cascade;
    cascade.addBackend(&uia);
    cascade.addBackend(&vision);

    const core::LocateResult result = cascade.locate(target);
    rpa::recorder::uninitializeUiaForThread();

    if (!ocrLoaded) {
        out << QStringLiteral("note    OCR unavailable (%1), so only UI Automation was tried\n")
                   .arg(QString::fromStdString(ocrError));
    }

    if (!result.found) {
        out << QStringLiteral("not found: %1\n").arg(QString::fromStdString(result.error));
        return false;
    }

    out << QStringLiteral("found   \"%1\"\n").arg(QString::fromStdString(result.matchedText));
    out << QStringLiteral("via     %1\n")
               .arg(strategy.isEmpty() ? QStringLiteral("vision (OCR anchor + box detection)")
                                       : strategy);
    out << QStringLiteral("bounds  (%1,%2,%3,%4)\n")
               .arg(result.box.x)
               .arg(result.box.y)
               .arg(result.box.width)
               .arg(result.box.height);
    out << QStringLiteral("click   (%1,%2)   -- not clicked\n")
               .arg(result.point.x)
               .arg(result.point.y);
    return true;
}

}  // namespace rpa::studio
