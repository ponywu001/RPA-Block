#pragma once

#include <QHash>
#include <QPoint>
#include <QRect>
#include <QWidget>
#include <optional>
#include <vector>

#include "rpa/core/Script.h"

namespace rpa::studio {

/// A step's location in the tree: indices from the root down. `{0, 2, 1}` means
/// steps[0] → thenSteps[2] → loopSteps[1]. Using a path rather than a pointer
/// keeps selection and drag state valid across the rebuilds that follow an edit.
using StepPath = std::vector<int>;

/// Scratch-style block canvas.
///
/// Blocks stack vertically and snap together; `if` and `loop` render as C-shaped
/// blocks whose mouth holds their children, so nesting is visible instead of
/// being hidden behind a property field. Dragging a block carries its whole
/// subtree, and the drop indicator shows exactly where it will land — including
/// inside an empty C-block mouth.
class FlowCanvas : public QWidget {
    Q_OBJECT

public:
    explicit FlowCanvas(QWidget* parent = nullptr);

    void setScript(const core::Script& script);
    const core::Script& script() const { return script_; }
    void setSteps(const core::StepList& steps);

    /// What an empty canvas says. The default points at the block palette, which
    /// is not on screen when this install is run-only.
    void setEmptyHint(const QString& hint);

    /// Selection, as a path. Empty when nothing is selected.
    const StepPath& selectedPath() const { return selected_; }
    void selectPath(const StepPath& path);
    /// Index into the *top-level* list, or -1 when the selection is nested or
    /// absent. Single-stepping only makes sense for top-level blocks.
    int selectedTopLevelIndex() const;

    const core::Step* selectedStep() const;
    void replaceSelected(const core::Step& step);

    /// Append at the end of the top level, or inside the selected container.
    void addStep(const core::Step& step);
    void removeSelected();
    void toggleSelectedEnabled();

    void setRunningStep(const QString& stepId);
    void setStepOutcome(const QString& stepId, bool ok);
    void clearRunDecorations();

    QString suggestStepId(core::StepType type) const;

signals:
    void selectionChanged();
    void scriptModified();
    /// A block was double-clicked — the host opens the target picker for it.
    void blockActivated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QString emptyHint_ = QStringLiteral("從左邊的積木清單點兩下，或直接拖過來，\n"
                                        "就能開始組流程。");

    struct Placed {
        StepPath path;
        core::StepType type;
        bool container = false;
        bool enabled = true;
        int depth = 0;
        QRect header;  ///< the clickable/draggable strip
        QRect whole;   ///< header plus mouth plus footer, for hit-testing a subtree
    };

    /// Where a dragged block would be inserted: into `parent`'s child list at
    /// `index`. An empty parent path means the top level.
    struct DropSlot {
        StepPath parent;
        int index = 0;
        QRect marker;
    };

    // --- layout ---------------------------------------------------------
    void relayout();
    int layoutList(const core::StepList& steps, const StepPath& parent, int x, int y, int depth);
    QSize measure() const;

    // --- tree access ----------------------------------------------------
    core::StepList* listAt(const StepPath& parent);
    const core::StepList* listAt(const StepPath& parent) const;
    core::Step* stepAt(const StepPath& path);
    const core::Step* stepAt(const StepPath& path) const;
    /// Children of a container step, chosen by type (`if` uses thenSteps).
    core::StepList* childListOf(core::Step& step);

    static bool isPrefixOf(const StepPath& prefix, const StepPath& path);

    // --- interaction ----------------------------------------------------
    const Placed* placedAt(const QPoint& point) const;
    std::optional<DropSlot> dropSlotAt(const QPoint& point, const StepPath& dragged) const;
    void performDrop(const StepPath& from, const DropSlot& to);

    // --- painting -------------------------------------------------------
    void paintBlock(QPainter& painter, const Placed& placed, const core::Step& step);
    QPainterPath blockOutline(const QRect& rect, bool topNotch, bool bottomTab) const;

    core::Script script_;
    std::vector<Placed> placed_;
    QSize contentSize_;

    StepPath selected_;
    StepPath hovered_;

    // Drag state
    bool pressed_ = false;
    bool dragging_ = false;
    QPoint pressPoint_;
    StepPath dragPath_;
    QPoint dragOffset_;
    std::optional<DropSlot> dropSlot_;

    QString runningStepId_;
    QHash<QString, bool> outcomes_;
};

}  // namespace rpa::studio
