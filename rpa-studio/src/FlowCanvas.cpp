#include "FlowCanvas.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QToolTip>
#include <algorithm>

#include "BlockStyle.h"
#include "Theme.h"

namespace rpa::studio {

namespace {

// Block geometry. Chosen so a nested block still reads clearly at three levels
// deep without the canvas needing horizontal scrolling on a 1080p window.
constexpr int kMargin = 18;
constexpr int kBlockHeight = 46;
constexpr int kFooterHeight = 26;
/// Breathing room between the last child and the closing foot, so the "結束…"
/// label is not jammed against the block above it.
constexpr int kMouthBottomPad = 6;
constexpr int kIndent = 26;
constexpr int kBlockWidth = 430;
constexpr int kCorner = 8;
constexpr int kNotchWidth = 26;
constexpr int kNotchHeight = 5;
constexpr int kNotchInset = 20;
constexpr int kIconSize = 22;
constexpr int kMinMouth = 22;   ///< an empty C-block still shows a usable gap
constexpr int kDragThreshold = 5;


}  // namespace

FlowCanvas::FlowCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    // Exactly what one top-level block needs, with no slack: any extra here
    // becomes a permanent horizontal scrollbar at the default window size.
    setMinimumWidth(kBlockWidth + kMargin * 2);

    // Every colour here is read from the theme at paint time, so a repaint is all
    // a theme switch needs.
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, [this] { update(); });
}

// ---------------------------------------------------------------------------
// Tree access
// ---------------------------------------------------------------------------

core::StepList* FlowCanvas::childListOf(core::Step& step) {
    switch (step.type) {
        case core::StepType::If: return &step.thenSteps;
        case core::StepType::Loop: return &step.loopSteps;
        default: return nullptr;
    }
}

core::StepList* FlowCanvas::listAt(const StepPath& parent) {
    core::StepList* list = &script_.steps;
    for (int index : parent) {
        if (index < 0 || index >= static_cast<int>(list->size())) return nullptr;
        core::StepList* child = childListOf((*list)[static_cast<size_t>(index)]);
        if (!child) return nullptr;
        list = child;
    }
    return list;
}

const core::StepList* FlowCanvas::listAt(const StepPath& parent) const {
    return const_cast<FlowCanvas*>(this)->listAt(parent);
}

core::Step* FlowCanvas::stepAt(const StepPath& path) {
    if (path.empty()) return nullptr;
    StepPath parent(path.begin(), path.end() - 1);
    core::StepList* list = listAt(parent);
    if (!list) return nullptr;
    const int index = path.back();
    if (index < 0 || index >= static_cast<int>(list->size())) return nullptr;
    return &(*list)[static_cast<size_t>(index)];
}

const core::Step* FlowCanvas::stepAt(const StepPath& path) const {
    return const_cast<FlowCanvas*>(this)->stepAt(path);
}

bool FlowCanvas::isPrefixOf(const StepPath& prefix, const StepPath& path) {
    if (prefix.size() > path.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), path.begin());
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

int FlowCanvas::layoutList(const core::StepList& steps, const StepPath& parent, int x, int y,
                           int depth) {
    const int startY = y;

    for (size_t i = 0; i < steps.size(); ++i) {
        const core::Step& step = steps[i];

        StepPath path = parent;
        path.push_back(static_cast<int>(i));

        Placed placed;
        placed.path = path;
        placed.type = step.type;
        placed.container = isContainer(step.type);
        placed.enabled = step.enabled;
        placed.depth = depth;
        placed.header = QRect(x, y, kBlockWidth - depth * kIndent, kBlockHeight);

        if (placed.container) {
            const core::StepList* children =
                step.type == core::StepType::If ? &step.thenSteps : &step.loopSteps;

            const int mouthTop = y + kBlockHeight;
            int mouthHeight = layoutList(*children, path, x + kIndent, mouthTop, depth + 1);
            mouthHeight = std::max(mouthHeight + kMouthBottomPad, kMinMouth);

            const int total = kBlockHeight + mouthHeight + kFooterHeight;
            placed.whole = QRect(x, y, placed.header.width(), total);
            placed_.push_back(placed);
            y += total;
        } else {
            placed.whole = placed.header;
            placed_.push_back(placed);
            y += kBlockHeight;
        }
    }

    return y - startY;
}

void FlowCanvas::relayout() {
    placed_.clear();
    const int height = layoutList(script_.steps, {}, kMargin, kMargin, 0);

    contentSize_ = QSize(kBlockWidth + kMargin * 2,
                         std::max(height + kMargin * 2 + 80, 200));

    // Only the height is ours to declare. The canvas lives in a scroll area with
    // setWidgetResizable(true), which owns the width and matches it to the
    // viewport; the resize() that used to be here fought that, and because it
    // took max(width(), ...) it could only ever grow the widget -- leaving a
    // horizontal scrollbar over a canvas that had plenty of room.
    setMinimumHeight(contentSize_.height());
    update();
}

QSize FlowCanvas::measure() const {
    return contentSize_;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void FlowCanvas::setScript(const core::Script& script) {
    script_ = script;
    selected_.clear();
    hovered_.clear();
    outcomes_.clear();
    runningStepId_.clear();
    relayout();
    emit selectionChanged();
}

void FlowCanvas::setEmptyHint(const QString& hint) {
    if (emptyHint_ == hint) return;
    emptyHint_ = hint;
    update();
}

void FlowCanvas::setSteps(const core::StepList& steps) {
    script_.steps = steps;
    selected_.clear();
    relayout();
    emit scriptModified();
    emit selectionChanged();
}

void FlowCanvas::selectPath(const StepPath& path) {
    selected_ = stepAt(path) ? path : StepPath{};
    update();
    emit selectionChanged();
}

int FlowCanvas::selectedTopLevelIndex() const {
    if (selected_.size() != 1) return -1;
    return selected_.front();
}

const core::Step* FlowCanvas::selectedStep() const {
    return stepAt(selected_);
}

void FlowCanvas::replaceSelected(const core::Step& step) {
    core::Step* target = stepAt(selected_);
    if (!target) return;
    *target = step;
    relayout();
    emit scriptModified();
}

void FlowCanvas::addStep(const core::Step& step) {
    // Dropping a block while a container is selected puts it inside that
    // container — the same "add into the thing I'm looking at" behaviour a
    // Scratch user expects.
    StepPath parent;
    if (const core::Step* current = stepAt(selected_); current && isContainer(current->type)) {
        parent = selected_;
    }

    core::StepList* list = listAt(parent);
    if (!list) {
        parent.clear();
        list = &script_.steps;
    }

    list->push_back(step);

    StepPath path = parent;
    path.push_back(static_cast<int>(list->size()) - 1);
    selected_ = path;

    relayout();
    emit scriptModified();
    emit selectionChanged();
}

void FlowCanvas::removeSelected() {
    if (selected_.empty()) return;

    StepPath parent(selected_.begin(), selected_.end() - 1);
    core::StepList* list = listAt(parent);
    if (!list) return;

    const int index = selected_.back();
    if (index < 0 || index >= static_cast<int>(list->size())) return;

    list->erase(list->begin() + index);

    // Keep the selection somewhere sensible: the next sibling, else the
    // previous one, else the parent.
    if (!list->empty()) {
        StepPath next = parent;
        next.push_back(std::min(index, static_cast<int>(list->size()) - 1));
        selected_ = next;
    } else {
        selected_ = parent;
    }

    relayout();
    emit scriptModified();
    emit selectionChanged();
}

void FlowCanvas::toggleSelectedEnabled() {
    core::Step* step = stepAt(selected_);
    if (!step) return;
    step->enabled = !step->enabled;
    relayout();
    emit scriptModified();
}

void FlowCanvas::setRunningStep(const QString& stepId) {
    runningStepId_ = stepId;
    update();
}

void FlowCanvas::setStepOutcome(const QString& stepId, bool ok) {
    outcomes_.insert(stepId, ok);
    if (runningStepId_ == stepId) runningStepId_.clear();
    update();
}

void FlowCanvas::clearRunDecorations() {
    runningStepId_.clear();
    outcomes_.clear();
    update();
}

QString FlowCanvas::suggestStepId(core::StepType type) const {
    QSet<QString> taken;
    core::forEachStep(script_.steps, [&](const core::Step& step) {
        taken.insert(QString::fromStdString(step.id));
    });

    const QString base = QString::fromStdString(core::toString(type));
    for (int i = 1; i < 10000; ++i) {
        const QString candidate = QStringLiteral("%1_%2").arg(base).arg(i);
        if (!taken.contains(candidate)) return candidate;
    }
    return base;
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

const FlowCanvas::Placed* FlowCanvas::placedAt(const QPoint& point) const {
    // Deepest match wins: a nested block's header sits inside its parent's
    // `whole` rect, and the child is what the user is pointing at.
    const Placed* best = nullptr;
    for (const auto& placed : placed_) {
        if (!placed.header.contains(point)) continue;
        if (!best || placed.depth > best->depth) best = &placed;
    }
    return best;
}

std::optional<FlowCanvas::DropSlot> FlowCanvas::dropSlotAt(const QPoint& point,
                                                           const StepPath& dragged) const {
    std::optional<DropSlot> best;
    int bestDistance = 90;  // ignore drops far from any seam

    auto consider = [&](const StepPath& parent, int index, const QRect& marker) {
        const int distance = std::abs(marker.center().y() - point.y());
        if (distance >= bestDistance) return;
        // Refuse to drop a block inside itself.
        if (!dragged.empty() && isPrefixOf(dragged, parent)) return;
        bestDistance = distance;
        best = DropSlot{parent, index, marker};
    };

    // Seams around every block, at that block's own level.
    for (const auto& placed : placed_) {
        StepPath parent(placed.path.begin(), placed.path.end() - 1);
        const int index = placed.path.back();
        const int left = placed.header.left();
        const int w = placed.header.width();

        consider(parent, index, QRect(left, placed.whole.top() - 2, w, 4));
        consider(parent, index + 1, QRect(left, placed.whole.bottom() - 2, w, 4));

        // Inside a container's mouth, as the first child.
        if (placed.container) {
            const core::Step* step = stepAt(placed.path);
            const core::StepList* children =
                step ? (step->type == core::StepType::If ? &step->thenSteps : &step->loopSteps)
                     : nullptr;
            if (children && children->empty()) {
                consider(placed.path, 0,
                         QRect(left + kIndent, placed.header.bottom() + 6, w - kIndent, 4));
            }
        }
    }

    // The very first slot on an empty canvas.
    if (placed_.empty()) {
        consider({}, 0, QRect(kMargin, kMargin, kBlockWidth, 4));
    }

    return best;
}

void FlowCanvas::performDrop(const StepPath& from, const DropSlot& to) {
    StepPath fromParent(from.begin(), from.end() - 1);
    core::StepList* source = listAt(fromParent);
    if (!source) return;

    const int fromIndex = from.back();
    if (fromIndex < 0 || fromIndex >= static_cast<int>(source->size())) return;

    const core::Step moved = (*source)[static_cast<size_t>(fromIndex)];

    // Erase first, then adjust the target index if the removal shifted it. Doing
    // it in this order avoids ever holding two copies of the subtree.
    source->erase(source->begin() + fromIndex);

    int targetIndex = to.index;
    if (to.parent == fromParent && targetIndex > fromIndex) --targetIndex;

    core::StepList* target = listAt(to.parent);
    if (!target) {
        // The destination vanished with the erase; put it back where it was.
        source->insert(source->begin() + fromIndex, moved);
        return;
    }

    targetIndex = std::clamp(targetIndex, 0, static_cast<int>(target->size()));
    target->insert(target->begin() + targetIndex, moved);

    StepPath landed = to.parent;
    landed.push_back(targetIndex);
    selected_ = landed;

    relayout();
    emit scriptModified();
    emit selectionChanged();
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void FlowCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    const Placed* hit = placedAt(event->pos());
    if (!hit) {
        selected_.clear();
        update();
        emit selectionChanged();
        return;
    }

    selected_ = hit->path;
    pressed_ = true;
    dragging_ = false;
    pressPoint_ = event->pos();
    dragPath_ = hit->path;
    dragOffset_ = event->pos() - hit->header.topLeft();

    update();
    emit selectionChanged();
}

void FlowCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (pressed_ && !dragging_ &&
        (event->pos() - pressPoint_).manhattanLength() > kDragThreshold) {
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
    }

    if (dragging_) {
        dropSlot_ = dropSlotAt(event->pos(), dragPath_);
        update();
        return;
    }

    const Placed* hit = placedAt(event->pos());
    const StepPath hovered = hit ? hit->path : StepPath{};
    if (hovered != hovered_) {
        hovered_ = hovered;
        setCursor(hit ? Qt::OpenHandCursor : Qt::ArrowCursor);
        update();
    }
}

void FlowCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    if (dragging_) {
        if (dropSlot_) performDrop(dragPath_, *dropSlot_);
        dragging_ = false;
        dropSlot_.reset();
        setCursor(Qt::OpenHandCursor);
        update();
    }
    pressed_ = false;
}

void FlowCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    if (placedAt(event->pos())) emit blockActivated();
}

void FlowCanvas::leaveEvent(QEvent*) {
    if (!hovered_.empty()) {
        hovered_.clear();
        update();
    }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

QPainterPath FlowCanvas::blockOutline(const QRect& rect, bool topNotch, bool bottomTab) const {
    // The puzzle silhouette: a dent on the top edge and a matching bump on the
    // bottom, which is what makes a stack read as "connected".
    QPainterPath path;
    const int r = kCorner;
    const int nx = rect.left() + kNotchInset;

    path.moveTo(rect.left() + r, rect.top());
    if (topNotch) {
        path.lineTo(nx, rect.top());
        path.lineTo(nx + 4, rect.top() + kNotchHeight);
        path.lineTo(nx + kNotchWidth - 4, rect.top() + kNotchHeight);
        path.lineTo(nx + kNotchWidth, rect.top());
    }
    path.lineTo(rect.right() - r, rect.top());
    path.quadTo(rect.right(), rect.top(), rect.right(), rect.top() + r);
    path.lineTo(rect.right(), rect.bottom() - r);
    path.quadTo(rect.right(), rect.bottom(), rect.right() - r, rect.bottom());

    if (bottomTab) {
        path.lineTo(nx + kNotchWidth, rect.bottom());
        path.lineTo(nx + kNotchWidth - 4, rect.bottom() + kNotchHeight);
        path.lineTo(nx + 4, rect.bottom() + kNotchHeight);
        path.lineTo(nx, rect.bottom());
    }
    path.lineTo(rect.left() + r, rect.bottom());
    path.quadTo(rect.left(), rect.bottom(), rect.left(), rect.bottom() - r);
    path.lineTo(rect.left(), rect.top() + r);
    path.quadTo(rect.left(), rect.top(), rect.left() + r, rect.top());
    path.closeSubpath();
    return path;
}

void FlowCanvas::paintBlock(QPainter& painter, const Placed& placed, const core::Step& step) {
    const bool selected = placed.path == selected_;
    const bool hovered = placed.path == hovered_;
    const QString stepId = QString::fromStdString(step.id);

    QColor fill = colorOf(step.type);
    QColor edge = edgeOf(step.type);

    // A disabled block is drained of colour rather than hidden, so the shape of
    // the flow still reads while making clear it will be skipped.
    if (!placed.enabled) {
        fill = theme().disabledBlock;
        edge = theme().disabledBlockEdge;
    }
    if (hovered && !dragging_) fill = fill.lighter(108);

    painter.setPen(QPen(edge, selected ? 3 : 1.5));
    painter.setBrush(fill);

    const QRect header = placed.header;
    painter.drawPath(blockOutline(header, true, !placed.container));

    // Icon
    const QRect iconBox(header.left() + 14, header.top() + (header.height() - kIconSize) / 2,
                        kIconSize, kIconSize);
    paintStepIcon(painter, iconBox, step.type, inkOn(fill, 235));

    // Label + summary
    const int textLeft = iconBox.right() + 12;
    const int textWidth = header.right() - textLeft - 14;

    QFont labelFont = painter.font();
    labelFont.setBold(true);
    labelFont.setPointSizeF(labelFont.pointSizeF() + 0.5);
    painter.setFont(labelFont);
    painter.setPen(inkOn(fill));

    const QString label = stepLabel(step.type);
    const QFontMetrics labelMetrics(labelFont);
    const int labelWidth = labelMetrics.horizontalAdvance(label);
    painter.drawText(QRect(textLeft, header.top(), labelWidth, header.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, label);

    QFont summaryFont = painter.font();
    summaryFont.setBold(false);
    painter.setFont(summaryFont);
    painter.setPen(inkOn(fill, 220));

    const int summaryLeft = textLeft + labelWidth + 10;
    const QFontMetrics summaryMetrics(summaryFont);
    const QString summary = summaryMetrics.elidedText(
        stepSummary(step), Qt::ElideRight, header.right() - summaryLeft - 14);
    painter.drawText(QRect(summaryLeft, header.top(), header.right() - summaryLeft - 14,
                           header.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, summary);
    Q_UNUSED(textWidth);

    // Run decorations: a bar down the left edge is unambiguous even on a
    // strongly coloured block.
    if (!runningStepId_.isEmpty() && stepId == runningStepId_) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme().breakpoint);
        painter.drawRoundedRect(QRect(header.left() + 3, header.top() + 8, 5,
                                      header.height() - 16), 2, 2);
    } else if (auto outcome = outcomes_.constFind(stepId); outcome != outcomes_.constEnd()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(*outcome ? theme().success : theme().danger);
        painter.drawRoundedRect(QRect(header.left() + 3, header.top() + 8, 5,
                                      header.height() - 16), 2, 2);
    }

    // C-block: left spine down the mouth, plus the closing foot.
    if (placed.container) {
        const QRect whole = placed.whole;
        painter.setPen(QPen(edge, 1.5));
        painter.setBrush(fill);

        const QRect spine(whole.left(), header.bottom(), kIndent,
                          whole.bottom() - kFooterHeight - header.bottom());
        painter.drawRect(spine);

        const QRect foot(whole.left(), whole.bottom() - kFooterHeight, whole.width(),
                         kFooterHeight);
        painter.drawPath(blockOutline(foot, false, true));

        painter.setPen(inkOn(fill, 215));
        QFont footFont = painter.font();
        footFont.setPointSizeF(footFont.pointSizeF() - 0.5);
        painter.setFont(footFont);
        painter.drawText(foot.adjusted(kIndent + 8, 0, -8, 0),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         step.type == core::StepType::If ? QStringLiteral("結束如果")
                                                         : QStringLiteral("結束重複"));

        // An empty mouth needs a prompt, or it looks like a rendering glitch.
        const core::StepList& children =
            step.type == core::StepType::If ? step.thenSteps : step.loopSteps;
        if (children.empty()) {
            painter.setPen(inkOn(fill, 170));
            painter.drawText(QRect(whole.left() + kIndent + 10, header.bottom(),
                                   whole.width() - kIndent - 20,
                                   whole.height() - header.height() - kFooterHeight),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             QStringLiteral("把積木拖進來"));
        }
    }
}

void FlowCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.fillRect(rect(), theme().canvas);

    // Dotted grid, so an empty canvas reads as a workspace rather than a blank
    // panel, and dragging has a visual frame of reference.
    painter.setPen(QPen(theme().canvasDot, 1));
    for (int y = kMargin; y < height(); y += 24) {
        for (int x = kMargin; x < width(); x += 24) {
            painter.drawPoint(x, y);
        }
    }

    if (script_.steps.empty()) {
        painter.setPen(theme().textMuted);
        QFont hintFont = painter.font();
        hintFont.setPointSizeF(hintFont.pointSizeF() + 1.5);
        painter.setFont(hintFont);
        // The palette is not on screen on a run-only install, so the usual hint
        // would be telling the user to drag from a list that is not there.
        painter.drawText(rect().adjusted(40, 40, -40, -40),
                         Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                         emptyHint_);
    }

    // Painted in layout order so a child lands on top of its parent's mouth.
    for (const auto& placed : placed_) {
        const core::Step* step = stepAt(placed.path);
        if (!step) continue;
        if (dragging_ && isPrefixOf(dragPath_, placed.path)) continue;  // drawn as the ghost
        paintBlock(painter, placed, *step);
    }

    // Drop indicator
    if (dragging_ && dropSlot_) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme().dropIndicator);
        QRect marker = dropSlot_->marker;
        marker.setHeight(5);
        painter.drawRoundedRect(marker, 2, 2);
    }

    // Ghost of the dragged subtree
    if (dragging_) {
        const core::Step* step = stepAt(dragPath_);
        auto it = std::find_if(placed_.begin(), placed_.end(),
                               [this](const Placed& p) { return p.path == dragPath_; });
        if (step && it != placed_.end()) {
            painter.setOpacity(0.75);
            Placed ghost = *it;
            const QPoint cursor = mapFromGlobal(QCursor::pos());
            const QPoint origin = cursor - dragOffset_;
            ghost.header.moveTopLeft(origin);
            ghost.whole.moveTopLeft(origin);
            paintBlock(painter, ghost, *step);
            painter.setOpacity(1.0);
        }
    }
}

}  // namespace rpa::studio
