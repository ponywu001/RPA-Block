#include "BlockPalette.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "BlockStyle.h"
#include "Theme.h"

namespace rpa::studio {

namespace {

constexpr int kPad = 10;
constexpr int kChipHeight = 38;
constexpr int kChipGap = 6;
constexpr int kHeadingHeight = 28;
constexpr int kIcon = 18;
constexpr int kCorner = 7;

/// Palette order groups by category so the colours form bands, which is how a
/// Scratch user navigates: by colour first, then by label.
const core::StepType kOrder[] = {
    core::StepType::Click,
    core::StepType::DoubleClick,
    core::StepType::TypeText,
    core::StepType::KeyPress,
    core::StepType::Wait,
    core::StepType::OcrFind,
    core::StepType::ImageFind,
    core::StepType::WindowActivate,
    core::StepType::Screenshot,
    core::StepType::If,
    core::StepType::Loop,
    core::StepType::HttpRequest,
};

}  // namespace

BlockPalette::BlockPalette(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumWidth(200);
    rebuild();

    connect(&ThemeManager::instance(), &ThemeManager::changed, this, [this] { update(); });
}

void BlockPalette::rebuild() {
    entries_.clear();

    int y = kPad;
    std::optional<BlockCategory> lastCategory;

    for (core::StepType type : kOrder) {
        const BlockCategory category = categoryOf(type);
        if (!lastCategory || *lastCategory != category) {
            Entry heading;
            heading.isHeading = true;
            heading.category = category;
            heading.rect = QRect(kPad, y, width() - kPad * 2, kHeadingHeight);
            entries_.push_back(heading);
            y += kHeadingHeight;
            lastCategory = category;
        }

        Entry chip;
        chip.type = type;
        chip.category = category;
        chip.rect = QRect(kPad, y, width() - kPad * 2, kChipHeight);
        entries_.push_back(chip);
        y += kChipHeight + kChipGap;
    }

    setMinimumHeight(y + kPad);
}

int BlockPalette::indexAt(const QPoint& point) const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].isHeading) continue;
        if (entries_[i].rect.contains(point)) return static_cast<int>(i);
    }
    return -1;
}

void BlockPalette::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int index = indexAt(event->pos());
    if (index >= 0) emit blockChosen(entries_[static_cast<size_t>(index)].type);
}

void BlockPalette::mouseMoveEvent(QMouseEvent* event) {
    const int index = indexAt(event->pos());
    if (index != hovered_) {
        hovered_ = index;
        setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void BlockPalette::leaveEvent(QEvent*) {
    if (hovered_ != -1) {
        hovered_ = -1;
        update();
    }
}

void BlockPalette::paintEvent(QPaintEvent*) {
    // Widths change with the dock, so the chip rects are recomputed each paint
    // rather than only at construction.
    rebuild();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), theme().surface);

    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];

        if (entry.isHeading) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(categoryColor(entry.category));
            painter.drawEllipse(QRect(entry.rect.left(), entry.rect.center().y() - 4, 8, 8));

            QFont font = painter.font();
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(theme().textMuted);
            painter.drawText(entry.rect.adjusted(16, 0, 0, 0),
                             Qt::AlignVCenter | Qt::AlignLeft, categoryName(entry.category));
            continue;
        }

        const bool hovered = static_cast<int>(i) == hovered_;
        QColor fill = colorOf(entry.type);
        if (hovered) fill = fill.lighter(112);

        // Miniature of the canvas block: same notch and tab, so the palette
        // reads as "these are the same objects", not a separate list widget.
        const QRect r = entry.rect;
        QPainterPath path;
        const int nx = r.left() + 16;
        path.moveTo(r.left() + kCorner, r.top());
        path.lineTo(nx, r.top());
        path.lineTo(nx + 3, r.top() + 4);
        path.lineTo(nx + 17, r.top() + 4);
        path.lineTo(nx + 20, r.top());
        path.lineTo(r.right() - kCorner, r.top());
        path.quadTo(r.right(), r.top(), r.right(), r.top() + kCorner);
        path.lineTo(r.right(), r.bottom() - kCorner);
        path.quadTo(r.right(), r.bottom(), r.right() - kCorner, r.bottom());
        path.lineTo(nx + 20, r.bottom());
        path.lineTo(nx + 17, r.bottom() + 4);
        path.lineTo(nx + 3, r.bottom() + 4);
        path.lineTo(nx, r.bottom());
        path.lineTo(r.left() + kCorner, r.bottom());
        path.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - kCorner);
        path.lineTo(r.left(), r.top() + kCorner);
        path.quadTo(r.left(), r.top(), r.left() + kCorner, r.top());
        path.closeSubpath();

        painter.setPen(QPen(edgeOf(entry.type), 1.4));
        painter.setBrush(fill);
        painter.drawPath(path);

        const QRect iconBox(r.left() + 11, r.top() + (r.height() - kIcon) / 2, kIcon, kIcon);
        paintStepIcon(painter, iconBox, entry.type, inkOn(fill, 235));

        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(inkOn(fill));
        painter.drawText(r.adjusted(iconBox.width() + 20, 0, -10, 0),
                         Qt::AlignVCenter | Qt::AlignLeft, stepLabel(entry.type));
    }
}

}  // namespace rpa::studio
