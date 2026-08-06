#pragma once

#include <QWidget>
#include <optional>
#include <vector>

#include "BlockStyle.h"
#include "rpa/core/Script.h"

namespace rpa::studio {

/// The left-hand block drawer. Draws miniature versions of the real blocks —
/// same colour, same icon, same puzzle silhouette — grouped under category
/// headings, so what you pick visibly matches what lands on the canvas.
class BlockPalette : public QWidget {
    Q_OBJECT

public:
    explicit BlockPalette(QWidget* parent = nullptr);

signals:
    /// Emitted on a click or double-click: the host appends the step.
    void blockChosen(rpa::core::StepType type);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Entry {
        bool isHeading = false;
        BlockCategory category = BlockCategory::Mouse;
        core::StepType type = core::StepType::Wait;
        QRect rect;
    };

    void rebuild();
    int indexAt(const QPoint& point) const;

    std::vector<Entry> entries_;
    int hovered_ = -1;
};

}  // namespace rpa::studio
