#include "BlockStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>

#include "Theme.h"

namespace rpa::studio {

BlockCategory categoryOf(core::StepType type) {
    switch (type) {
        case core::StepType::Click:
        case core::StepType::DoubleClick:
            return BlockCategory::Mouse;
        case core::StepType::TypeText:
        case core::StepType::KeyPress:
            return BlockCategory::Keyboard;
        case core::StepType::Wait:
            return BlockCategory::Timing;
        case core::StepType::OcrFind:
        case core::StepType::ImageFind:
            return BlockCategory::Vision;
        case core::StepType::WindowActivate:
        case core::StepType::Screenshot:
            return BlockCategory::Window;
        case core::StepType::If:
        case core::StepType::Loop:
            return BlockCategory::Control;
        case core::StepType::HttpRequest:
            return BlockCategory::Network;
    }
    return BlockCategory::Timing;
}

QString categoryName(BlockCategory category) {
    switch (category) {
        case BlockCategory::Mouse: return QStringLiteral("滑鼠");
        case BlockCategory::Keyboard: return QStringLiteral("鍵盤");
        case BlockCategory::Timing: return QStringLiteral("等待");
        case BlockCategory::Vision: return QStringLiteral("找畫面");
        case BlockCategory::Window: return QStringLiteral("視窗");
        case BlockCategory::Control: return QStringLiteral("流程控制");
        case BlockCategory::Network: return QStringLiteral("網路");
    }
    return {};
}

namespace {

/// The hue set, tuned for a light canvas.
QColor baseCategoryColor(BlockCategory category) {
    switch (category) {
        case BlockCategory::Mouse: return QColor(0x4C, 0x97, 0xFF);
        case BlockCategory::Keyboard: return QColor(0x99, 0x66, 0xFF);
        case BlockCategory::Timing: return QColor(0xFF, 0xAB, 0x19);
        case BlockCategory::Vision: return QColor(0x40, 0xBF, 0x4A);
        case BlockCategory::Window: return QColor(0x4C, 0xBF, 0xE6);
        case BlockCategory::Control: return QColor(0xFF, 0x8C, 0x1A);
        case BlockCategory::Network: return QColor(0xFF, 0x66, 0x80);
    }
    return QColor(0x8B, 0x93, 0xA3);
}

}  // namespace

QColor categoryColor(BlockCategory category) {
    const QColor base = baseCategoryColor(category);
    if (!theme().dark) return base;

    // Same hues on a dark canvas, held back a little. At full brightness these
    // saturated fills glare against a near-black background and the white block
    // text starts to bloom; dropping lightness and saturation keeps each category
    // just as identifiable while letting the canvas recede.
    float h = 0.0f;
    float s = 0.0f;
    float l = 0.0f;
    base.getHslF(&h, &s, &l);
    return QColor::fromHslF(h, std::min(1.0f, s * 0.80f), std::min(1.0f, l * 0.78f));
}

QColor categoryEdge(BlockCategory category) {
    // Lighter rather than darker on a dark canvas: an edge darker than the fill
    // disappears into the background instead of defining the block's silhouette.
    const QColor fill = categoryColor(category);
    return theme().dark ? fill.lighter(132) : fill.darker(125);
}

namespace {

/// WCAG 2.1 relative luminance.
double relativeLuminance(const QColor& color) {
    auto channel = [](double v) {
        v /= 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) +
           0.0722 * channel(color.blue());
}

double contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace

QColor inkOn(const QColor& fill) {
    // Not pure black: a very dark tint of the fill's own hue sits on the block
    // without looking like a hole punched in it.
    float h = 0.0f;
    float s = 0.0f;
    float l = 0.0f;
    fill.getHslF(&h, &s, &l);
    const QColor dark = QColor::fromHslF(h, std::min(1.0f, s * 0.55f), 0.11f);
    const QColor light(0xFF, 0xFF, 0xFF);

    return contrastRatio(light, fill) >= contrastRatio(dark, fill) ? light : dark;
}

QColor inkOn(const QColor& fill, int alpha) {
    QColor ink = inkOn(fill);
    ink.setAlpha(alpha);
    return ink;
}

QColor colorOf(core::StepType type) {
    return categoryColor(categoryOf(type));
}

QColor edgeOf(core::StepType type) {
    return categoryEdge(categoryOf(type));
}

QString stepLabel(core::StepType type) {
    switch (type) {
        case core::StepType::Click: return QStringLiteral("點擊");
        case core::StepType::DoubleClick: return QStringLiteral("雙擊");
        case core::StepType::TypeText: return QStringLiteral("輸入文字");
        case core::StepType::KeyPress: return QStringLiteral("按快速鍵");
        case core::StepType::Wait: return QStringLiteral("等待");
        case core::StepType::OcrFind: return QStringLiteral("找文字");
        case core::StepType::ImageFind: return QStringLiteral("找圖片");
        case core::StepType::WindowActivate: return QStringLiteral("切換視窗");
        case core::StepType::Screenshot: return QStringLiteral("截圖");
        case core::StepType::If: return QStringLiteral("如果");
        case core::StepType::Loop: return QStringLiteral("重複");
        case core::StepType::HttpRequest: return QStringLiteral("呼叫 API");
    }
    return {};
}

bool isContainer(core::StepType type) {
    return type == core::StepType::If || type == core::StepType::Loop;
}

namespace {

QString describeTarget(const core::Target& target) {
    switch (target.kind) {
        case core::TargetKind::Ocr:
            return target.text.empty()
                       ? QStringLiteral("（尚未設定文字）")
                       : QStringLiteral("文字「%1」").arg(QString::fromStdString(target.text));
        case core::TargetKind::Image:
            return target.templatePath.empty()
                       ? QStringLiteral("（尚未選圖）")
                       : QStringLiteral("圖片 %1")
                             .arg(QString::fromStdString(target.templatePath));
        case core::TargetKind::Point:
            return QStringLiteral("座標 (%1, %2)").arg(target.point.x).arg(target.point.y);
    }
    return {};
}

QString describeCondition(const core::Condition& condition) {
    switch (condition.kind) {
        case core::Condition::Kind::OcrFound:
            return QStringLiteral("畫面上有 %1").arg(describeTarget(condition.target));
        case core::Condition::Kind::ImageFound:
            return QStringLiteral("畫面上有 %1").arg(describeTarget(condition.target));
        case core::Condition::Kind::VarEquals:
            return QStringLiteral("變數 %1 等於「%2」")
                .arg(QString::fromStdString(condition.variable),
                     QString::fromStdString(condition.value));
        case core::Condition::Kind::VarContains:
            return QStringLiteral("變數 %1 含有「%2」")
                .arg(QString::fromStdString(condition.variable),
                     QString::fromStdString(condition.value));
    }
    return {};
}

}  // namespace

QString stepSummary(const core::Step& step) {
    switch (step.type) {
        case core::StepType::Click:
        case core::StepType::DoubleClick:
            return describeTarget(step.target);
        case core::StepType::TypeText:
            return step.text.empty()
                       ? QStringLiteral("（尚未設定內容）")
                       : QStringLiteral("「%1」").arg(QString::fromStdString(step.text));
        case core::StepType::KeyPress:
            return step.keys.empty() ? QStringLiteral("（尚未設定按鍵）")
                                     : QString::fromStdString(step.keys);
        case core::StepType::Wait:
            return QStringLiteral("%1 毫秒").arg(step.waitMs);
        case core::StepType::OcrFind:
            return step.target.text.empty()
                       ? QStringLiteral("（尚未設定文字）")
                       : QStringLiteral("「%1」→ %2")
                             .arg(QString::fromStdString(step.target.text),
                                  QString::fromStdString(step.saveToVar));
        case core::StepType::ImageFind:
            return step.target.templatePath.empty()
                       ? QStringLiteral("（尚未選圖）")
                       : QStringLiteral("%1 → %2")
                             .arg(QString::fromStdString(step.target.templatePath),
                                  QString::fromStdString(step.saveToVar));
        case core::StepType::WindowActivate:
            return step.titleMatch.empty()
                       ? QStringLiteral("（尚未設定標題）")
                       : QStringLiteral("標題含「%1」")
                             .arg(QString::fromStdString(step.titleMatch));
        case core::StepType::Screenshot:
            return step.path.empty() ? QStringLiteral("（尚未設定路徑）")
                                     : QString::fromStdString(step.path);
        case core::StepType::If:
            return step.condition ? describeCondition(*step.condition)
                                  : QStringLiteral("（尚未設定條件）");
        case core::StepType::Loop:
            return step.whileCondition
                       ? QStringLiteral("當 %1").arg(describeCondition(*step.whileCondition))
                       : QStringLiteral("%1 次").arg(step.loopCount);
        case core::StepType::HttpRequest:
            return step.url.empty()
                       ? QStringLiteral("（尚未設定網址）")
                       : QStringLiteral("%1 %2").arg(QString::fromStdString(step.httpMethod),
                                                     QString::fromStdString(step.url));
    }
    return {};
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

namespace {

/// All icons are drawn inside a normalised unit box so one set of coordinates
/// works at any size, then scaled into `box`.
struct IconPainter {
    QPainter& p;
    QRectF unit;

    QPointF at(double x, double y) const {
        return QPointF(unit.left() + x * unit.width(), unit.top() + y * unit.height());
    }
    QRectF rectAt(double x, double y, double w, double h) const {
        return QRectF(at(x, y), QSizeF(w * unit.width(), h * unit.height()));
    }
    double scaled(double v) const { return v * unit.width(); }
};

void paintMouse(IconPainter g, bool doubleClick) {
    // Cursor arrow.
    QPolygonF arrow;
    arrow << g.at(0.28, 0.14) << g.at(0.28, 0.78) << g.at(0.45, 0.62) << g.at(0.56, 0.88)
          << g.at(0.68, 0.82) << g.at(0.57, 0.57) << g.at(0.78, 0.52);
    g.p.drawPolygon(arrow);

    // Click ripples; two arcs for a double click.
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.07));
    pen.setCapStyle(Qt::RoundCap);
    g.p.setPen(pen);
    g.p.setBrush(Qt::NoBrush);
    g.p.drawArc(g.rectAt(0.02, 0.04, 0.30, 0.30).toRect(), 30 * 16, 90 * 16);
    if (doubleClick) {
        g.p.drawArc(g.rectAt(-0.10, -0.08, 0.50, 0.50).toRect(), 30 * 16, 90 * 16);
    }
}

void paintKeyboard(IconPainter g) {
    g.p.drawRoundedRect(g.rectAt(0.08, 0.28, 0.84, 0.46), g.scaled(0.08), g.scaled(0.08));
    g.p.setBrush(Qt::NoBrush);
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.06));
    g.p.setPen(pen);
    for (int row = 0; row < 2; ++row) {
        const double y = 0.42 + row * 0.16;
        for (int col = 0; col < 4; ++col) {
            const double x = 0.19 + col * 0.17;
            g.p.drawPoint(g.at(x, y));
        }
    }
}

void paintText(IconPainter g) {
    // A capital "A" plus a caret, for text entry.
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.10));
    pen.setCapStyle(Qt::RoundCap);
    g.p.setPen(pen);
    g.p.setBrush(Qt::NoBrush);
    g.p.drawLine(g.at(0.14, 0.82), g.at(0.38, 0.16));
    g.p.drawLine(g.at(0.38, 0.16), g.at(0.62, 0.82));
    g.p.drawLine(g.at(0.24, 0.58), g.at(0.52, 0.58));
    g.p.drawLine(g.at(0.80, 0.14), g.at(0.80, 0.86));
}

void paintClock(IconPainter g) {
    g.p.setBrush(Qt::NoBrush);
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.09));
    pen.setCapStyle(Qt::RoundCap);
    g.p.setPen(pen);
    g.p.drawEllipse(g.rectAt(0.10, 0.10, 0.80, 0.80));
    g.p.drawLine(g.at(0.50, 0.50), g.at(0.50, 0.26));
    g.p.drawLine(g.at(0.50, 0.50), g.at(0.70, 0.58));
}

void paintMagnifier(IconPainter g) {
    g.p.setBrush(Qt::NoBrush);
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.10));
    pen.setCapStyle(Qt::RoundCap);
    g.p.setPen(pen);
    g.p.drawEllipse(g.rectAt(0.10, 0.10, 0.56, 0.56));
    g.p.drawLine(g.at(0.62, 0.62), g.at(0.88, 0.88));
}

void paintImage(IconPainter g) {
    g.p.setBrush(Qt::NoBrush);
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.08));
    g.p.setPen(pen);
    g.p.drawRoundedRect(g.rectAt(0.10, 0.18, 0.80, 0.64), g.scaled(0.08), g.scaled(0.08));
    // Mountain + sun, the universal "picture" glyph.
    QPolygonF hill;
    hill << g.at(0.18, 0.72) << g.at(0.40, 0.44) << g.at(0.60, 0.72);
    g.p.drawPolyline(hill);
    g.p.setBrush(g.p.pen().color());
    g.p.drawEllipse(g.rectAt(0.64, 0.30, 0.14, 0.14));
}

void paintWindow(IconPainter g) {
    g.p.drawRoundedRect(g.rectAt(0.10, 0.16, 0.80, 0.68), g.scaled(0.07), g.scaled(0.07));
    // Title bar cut-out.
    QColor hole = g.p.brush().color();
    hole.setAlpha(70);
    g.p.setBrush(hole);
    g.p.setPen(Qt::NoPen);
    g.p.drawRect(g.rectAt(0.10, 0.16, 0.80, 0.16));
}

void paintCamera(IconPainter g) {
    g.p.drawRoundedRect(g.rectAt(0.08, 0.28, 0.84, 0.50), g.scaled(0.09), g.scaled(0.09));
    g.p.drawRect(g.rectAt(0.34, 0.18, 0.24, 0.12));
    g.p.setBrush(Qt::NoBrush);
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.08));
    g.p.setPen(pen);
    g.p.drawEllipse(g.rectAt(0.38, 0.38, 0.28, 0.28));
}

void paintBranch(IconPainter g) {
    g.p.setBrush(Qt::NoBrush);
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.10));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    g.p.setPen(pen);
    // A stem that forks two ways.
    g.p.drawLine(g.at(0.50, 0.86), g.at(0.50, 0.48));
    QPolygonF left;
    left << g.at(0.50, 0.48) << g.at(0.18, 0.48) << g.at(0.18, 0.16);
    g.p.drawPolyline(left);
    QPolygonF right;
    right << g.at(0.50, 0.48) << g.at(0.82, 0.48) << g.at(0.82, 0.16);
    g.p.drawPolyline(right);
}

void paintLoop(IconPainter g) {
    g.p.setBrush(Qt::NoBrush);
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.10));
    pen.setCapStyle(Qt::RoundCap);
    g.p.setPen(pen);
    g.p.drawArc(g.rectAt(0.12, 0.12, 0.76, 0.76).toRect(), 40 * 16, 280 * 16);
    // Arrow head closing the loop.
    g.p.setBrush(pen.color());
    g.p.setPen(Qt::NoPen);
    QPolygonF head;
    head << g.at(0.72, 0.10) << g.at(0.96, 0.28) << g.at(0.68, 0.38);
    g.p.drawPolygon(head);
}

void paintGlobe(IconPainter g) {
    g.p.setBrush(Qt::NoBrush);
    QPen pen = g.p.pen();
    pen.setWidthF(g.scaled(0.08));
    g.p.setPen(pen);
    const QRectF circle = g.rectAt(0.10, 0.10, 0.80, 0.80);
    g.p.drawEllipse(circle);
    g.p.drawLine(g.at(0.10, 0.50), g.at(0.90, 0.50));
    g.p.drawEllipse(g.rectAt(0.34, 0.10, 0.32, 0.80));
}

}  // namespace

void paintStepIcon(QPainter& painter, const QRect& box, core::StepType type, const QColor& ink) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(ink);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(ink);

    IconPainter g{painter, QRectF(box)};

    switch (type) {
        case core::StepType::Click: paintMouse(g, false); break;
        case core::StepType::DoubleClick: paintMouse(g, true); break;
        case core::StepType::TypeText: paintText(g); break;
        case core::StepType::KeyPress: paintKeyboard(g); break;
        case core::StepType::Wait: paintClock(g); break;
        case core::StepType::OcrFind: paintMagnifier(g); break;
        case core::StepType::ImageFind: paintImage(g); break;
        case core::StepType::WindowActivate: paintWindow(g); break;
        case core::StepType::Screenshot: paintCamera(g); break;
        case core::StepType::If: paintBranch(g); break;
        case core::StepType::Loop: paintLoop(g); break;
        case core::StepType::HttpRequest: paintGlobe(g); break;
    }

    painter.restore();
}

}  // namespace rpa::studio
