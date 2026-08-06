#pragma once

#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QWidget>
#include <memory>
#include <optional>
#include <vector>

#include "rpa/core/Geometry.h"
#include "rpa/core/Script.h"
#include "rpa/vision/VisionLocator.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace rpa::studio {

class PickerSurface;

/// Screen 3: freeze the desktop, let the user rubber-band a region, then choose
/// how to anchor it -- OCR text or a saved image template. Running OCR live on
/// the selection is what makes that choice informed rather than a guess.
///
/// This class is the settings panel and the controller; the dimmed screen covers
/// are separate `PickerSurface` windows, one per monitor. One window stretched
/// across the whole virtual desktop cannot work on a mixed-DPI setup, because Qt
/// gives a window a single device pixel ratio while each monitor has its own --
/// the frozen image ends up scaled by the desktop-wide average and every pick
/// lands somewhere the user did not point.
class TargetPickerOverlay : public QWidget {
    Q_OBJECT

public:
    /// `locator` supplies OCR; it may have OCR unavailable, in which case the
    /// dialog falls back to template-only mode and says so.
    TargetPickerOverlay(vision::VisionLocator* locator,
                        const QString& templateDirectory,
                        QWidget* parent = nullptr);
    ~TargetPickerOverlay() override;

    /// Freeze the screen and show the overlay. Returns false when the capture
    /// failed, leaving `error` set.
    bool beginPick(QString& error);

    /// Where new template crops are written. Must be updated when the project
    /// folder changes, or templates land beside the old project while the
    /// locator resolves `assets/...` against the new one.
    void setTemplateDirectory(const QString& directory);

    // --- called by PickerSurface -------------------------------------------
    void surfacePressed(PickerSurface* surface, const QPoint& local);
    void surfaceDragged(PickerSurface* surface, const QPoint& local);
    void surfaceReleased(PickerSurface* surface, const QPoint& local);
    void surfaceKeyPressed(int key);

signals:
    /// The user confirmed a target. `target` is ready to drop into a step.
    void targetPicked(const rpa::core::Target& target);
    void cancelled();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildPanel();
    void placePanel();
    void dismiss();
    void runOcrOnSelection();
    void confirm();
    void cancel();

    /// Rect of the rubber band, local to the surface it was drawn on.
    QRect selectionRect() const;
    /// Selection -> absolute physical screen rect, via the owning surface's own
    /// mapping.
    core::Rect toScreenRect() const;
    /// Selection -> rect within the owning surface's slice of the capture.
    QRect toPixmapRect() const;

    vision::VisionLocator* locator_;
    QString templateDirectory_;

    std::vector<std::unique_ptr<PickerSurface>> surfaces_;

    /// The surface the current rubber band belongs to. A selection never spans
    /// monitors: the two halves would need different scale factors, and a target
    /// straddling a screen edge is not something the executor could click anyway.
    PickerSurface* activeSurface_ = nullptr;
    bool dragging_ = false;
    QPoint dragStart_;
    QPoint dragEnd_;
    bool hasSelection_ = false;

    QRadioButton* ocrRadio_;
    QRadioButton* templateRadio_;
    QLabel* ocrResultLabel_;
    QComboBox* matchCombo_;
    QLabel* templatePreview_;
    QDoubleSpinBox* thresholdSpin_;
    QSpinBox* offsetXSpin_;
    QSpinBox* offsetYSpin_;
    QPushButton* confirmButton_;
    QPushButton* cancelButton_;

    QString recognisedText_;
    double recognisedConfidence_ = 0.0;
    /// Screen rect of the OCR line that was actually matched, which can be
    /// tighter than the user's rubber band.
    std::optional<core::Rect> recognisedBox_;
};

}  // namespace rpa::studio
