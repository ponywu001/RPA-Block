#include "TargetPickerOverlay.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

#include <opencv2/imgcodecs.hpp>

#include "rpa/vision/ScreenCapture.h"

namespace rpa::studio {

namespace {

/// cv::Mat (BGR) -> QImage (RGB888). Copies row by row rather than wrapping the
/// Mat's buffer, because the Mat that owns those pixels is a caller local.
QImage matToImage(const cv::Mat& bgr) {
    if (bgr.empty()) return {};

    QImage image(bgr.cols, bgr.rows, QImage::Format_RGB888);
    for (int y = 0; y < bgr.rows; ++y) {
        const cv::Vec3b* source = bgr.ptr<cv::Vec3b>(y);
        uchar* line = image.scanLine(y);
        for (int x = 0; x < bgr.cols; ++x) {
            line[x * 3 + 0] = source[x][2];
            line[x * 3 + 1] = source[x][1];
            line[x * 3 + 2] = source[x][0];
        }
    }
    return image;
}

/// QImage -> cv::Mat (BGR), the inverse of the above.
cv::Mat imageToMat(const QImage& source) {
    if (source.isNull()) return {};

    const QImage rgb = source.convertToFormat(QImage::Format_RGB888);
    cv::Mat bgr(rgb.height(), rgb.width(), CV_8UC3);
    for (int y = 0; y < rgb.height(); ++y) {
        const uchar* line = rgb.constScanLine(y);
        cv::Vec3b* destination = bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < rgb.width(); ++x) {
            destination[x] = cv::Vec3b(line[x * 3 + 2], line[x * 3 + 1], line[x * 3 + 0]);
        }
    }
    return bgr;
}

constexpr int kPanelMargin = 32;

}  // namespace

/// One monitor's dimmed cover: a frameless always-on-top window sized to exactly
/// one screen, holding that screen's slice of the frozen capture.
///
/// Being per-screen is the point. The window then has that monitor's device pixel
/// ratio, so its own slice paints 1:1 over what it is a picture of, and its
/// logical coordinates convert to physical pixels with that monitor's own scale
/// factor rather than a desktop-wide average that fits no monitor.
///
/// No Q_OBJECT: it declares no signals or slots and reports back through the
/// owning controller, which keeps it a private implementation detail of this file.
class PickerSurface : public QWidget {
public:
    PickerSurface(TargetPickerOverlay* owner, const core::ScreenPairing& pairing, QPixmap slice)
        : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          owner_(owner),
          mapping_(pairing.mapping()),
          slice_(std::move(slice)) {
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setGeometry(pairing.logical.x, pairing.logical.y, pairing.logical.width,
                    pairing.logical.height);
    }

    const core::ScaleMapping& mapping() const { return mapping_; }
    const QPixmap& slice() const { return slice_; }

    /// The rubber band to draw, or an invalid rect when this surface has none.
    void setMarquee(const QRect& marquee) {
        marquee_ = marquee;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        // Explicit source and target rects: the slice is in physical pixels and
        // the widget is in logical ones, and on a scaled monitor those differ.
        painter.drawPixmap(rect(), slice_, QRect(0, 0, slice_.width(), slice_.height()));

        // Dim everything, then punch the selection back to full brightness so the
        // user's chosen region is what stands out.
        painter.fillRect(rect(), QColor(20, 26, 34, 110));

        if (!marquee_.isValid() || marquee_.isEmpty()) return;

        const core::Rect crop = mapping_.toCapture(marquee_.x(), marquee_.y(), marquee_.width(),
                                                   marquee_.height());
        painter.drawPixmap(marquee_, slice_, QRect(crop.x, crop.y, crop.width, crop.height));
        painter.setPen(QPen(QColor(53, 97, 143), 2, Qt::DashLine));
        painter.drawRect(marquee_);

        // Report the physical rect: that is what the step stores and what the
        // executor will click.
        const core::Rect screen = mapping_.toPhysical(marquee_.x(), marquee_.y(), marquee_.width(),
                                                      marquee_.height());
        const QString label = QStringLiteral("%1 × %2  @ (%3, %4)")
                                  .arg(screen.width)
                                  .arg(screen.height)
                                  .arg(screen.x)
                                  .arg(screen.y);

        // Above the box normally, inside it when the box is at the top edge, so
        // the readout is never clipped off-screen.
        constexpr int labelHeight = 22;
        QRect labelRect = marquee_.adjusted(2, -labelHeight, 0, 0);
        labelRect.setHeight(labelHeight);
        if (labelRect.top() < 0) labelRect.moveTop(marquee_.top() + 2);

        painter.setPen(QColor(255, 255, 255));
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, label);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) owner_->surfacePressed(this, event->pos());
    }

    void mouseMoveEvent(QMouseEvent* event) override { owner_->surfaceDragged(this, event->pos()); }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) owner_->surfaceReleased(this, event->pos());
    }

    void keyPressEvent(QKeyEvent* event) override { owner_->surfaceKeyPressed(event->key()); }

private:
    TargetPickerOverlay* owner_;
    core::ScaleMapping mapping_;
    QPixmap slice_;
    QRect marquee_;
};

TargetPickerOverlay::TargetPickerOverlay(vision::VisionLocator* locator,
                                         const QString& templateDirectory,
                                         QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      locator_(locator),
      templateDirectory_(templateDirectory) {
    setAttribute(Qt::WA_DeleteOnClose, false);
    buildPanel();
}

TargetPickerOverlay::~TargetPickerOverlay() = default;

void TargetPickerOverlay::setTemplateDirectory(const QString& directory) {
    templateDirectory_ = directory;
}

void TargetPickerOverlay::buildPanel() {
    setObjectName(QStringLiteral("pickerPanel"));
    setAutoFillBackground(true);
    // Scoped by object name. A bare `QWidget { border: ... }` rule would cascade
    // to every descendant, drawing a box around each label, radio button and
    // combo in the panel.
    setStyleSheet(QStringLiteral("QWidget#pickerPanel { background: palette(window); "
                                "border: 1px solid palette(mid); border-radius: 6px; }"));

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(QStringLiteral("<b>選取目標</b>"), this));

    auto* hint = new QLabel(
        QStringLiteral("在你要操作的東西上拖一個框，再選執行時要用什麼方式找到它。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* modeBox = new QGroupBox(QStringLiteral("定位方式"), this);
    auto* modeLayout = new QVBoxLayout(modeBox);

    ocrRadio_ = new QRadioButton(QStringLiteral("文字錨點（OCR）"), modeBox);
    ocrRadio_->setChecked(true);
    modeLayout->addWidget(ocrRadio_);

    ocrResultLabel_ = new QLabel(QStringLiteral("拖一個框，我來讀裡面的文字。"), modeBox);
    ocrResultLabel_->setWordWrap(true);
    ocrResultLabel_->setIndent(20);
    modeLayout->addWidget(ocrResultLabel_);

    auto* matchRow = new QWidget(modeBox);
    auto* matchLayout = new QHBoxLayout(matchRow);
    matchLayout->setContentsMargins(20, 0, 0, 0);
    matchLayout->addWidget(new QLabel(QStringLiteral("比對方式"), matchRow));
    matchCombo_ = new QComboBox(matchRow);
    // Same order as the property panel, and for the same reason: "contains" is
    // the default because OCR tends to bring a neighbouring glyph along.
    matchCombo_->addItem(QStringLiteral("包含"), QStringLiteral("contains"));
    matchCombo_->addItem(QStringLiteral("完全相同"), QStringLiteral("exact"));
    matchCombo_->addItem(QStringLiteral("正規表示式"), QStringLiteral("regex"));
    matchLayout->addWidget(matchCombo_);
    matchLayout->addStretch(1);
    modeLayout->addWidget(matchRow);

    // Offered only when the automation tree recognises what was selected, so
    // the option appears with a real sentence in it rather than as a mode the
    // user has to understand and fill in.
    relativeRadio_ = new QRadioButton(modeBox);
    relativeRadio_->setVisible(false);
    modeLayout->addWidget(relativeRadio_);

    templateRadio_ = new QRadioButton(QStringLiteral("圖片比對（OpenCV）"), modeBox);
    modeLayout->addWidget(templateRadio_);

    auto* templateRow = new QWidget(modeBox);
    auto* templateLayout = new QHBoxLayout(templateRow);
    templateLayout->setContentsMargins(20, 0, 0, 0);
    templatePreview_ = new QLabel(templateRow);
    templatePreview_->setFixedSize(72, 40);
    templatePreview_->setFrameShape(QFrame::Box);
    templatePreview_->setScaledContents(true);
    // Opaque, or the empty well shows the frozen desktop straight through and
    // reads as a hole punched in the panel.
    templatePreview_->setAutoFillBackground(true);
    templatePreview_->setStyleSheet(QStringLiteral("QLabel { background: palette(base); }"));
    templateLayout->addWidget(templatePreview_);
    templateLayout->addWidget(new QLabel(QStringLiteral("相似度"), templateRow));
    thresholdSpin_ = new QDoubleSpinBox(templateRow);
    thresholdSpin_->setRange(0.10, 1.00);
    thresholdSpin_->setSingleStep(0.01);
    thresholdSpin_->setDecimals(2);
    thresholdSpin_->setValue(0.85);
    templateLayout->addWidget(thresholdSpin_);
    templateLayout->addStretch(1);
    modeLayout->addWidget(templateRow);

    layout->addWidget(modeBox);

    auto* offsetRow = new QWidget(this);
    auto* offsetLayout = new QHBoxLayout(offsetRow);
    offsetLayout->setContentsMargins(0, 0, 0, 0);
    offsetLayout->addWidget(new QLabel(QStringLiteral("點擊偏移  X"), offsetRow));
    offsetXSpin_ = new QSpinBox(offsetRow);
    offsetXSpin_->setRange(-4096, 4096);
    offsetLayout->addWidget(offsetXSpin_);
    offsetLayout->addWidget(new QLabel(QStringLiteral("Y"), offsetRow));
    offsetYSpin_ = new QSpinBox(offsetRow);
    offsetYSpin_->setRange(-4096, 4096);
    offsetLayout->addWidget(offsetYSpin_);
    offsetLayout->addStretch(1);
    layout->addWidget(offsetRow);

    auto* buttonRow = new QWidget(this);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addStretch(1);
    cancelButton_ = new QPushButton(QStringLiteral("取消（Esc）"), buttonRow);
    buttonLayout->addWidget(cancelButton_);
    confirmButton_ = new QPushButton(QStringLiteral("✔ 使用這個目標"), buttonRow);
    confirmButton_->setDefault(true);
    confirmButton_->setEnabled(false);
    buttonLayout->addWidget(confirmButton_);
    layout->addWidget(buttonRow);

    // A word-wrapping label with no width constraint reports a sizeHint wide
    // enough for its whole text on one line, so without this the panel would be
    // enormous and would resize itself every time an OCR result of a different
    // length arrived.
    setFixedWidth(470);
    adjustSize();

    connect(cancelButton_, &QPushButton::clicked, this, &TargetPickerOverlay::cancel);
    connect(confirmButton_, &QPushButton::clicked, this, &TargetPickerOverlay::confirm);
    connect(ocrRadio_, &QRadioButton::toggled, this, [this](bool on) {
        matchCombo_->setEnabled(on);
        if (on && hasSelection_) runOcrOnSelection();
    });

    // `clicked` rather than `toggled`: it fires only for a real click, so the
    // panel's own setChecked() calls do not get mistaken for the user making a
    // choice.
    for (QRadioButton* radio : {ocrRadio_, relativeRadio_, templateRadio_}) {
        connect(radio, &QRadioButton::clicked, this, [this] { modeChosenByUser_ = true; });
    }
}

bool TargetPickerOverlay::beginPick(QString& error) {
    // Before anything of ours is on screen: once the covers are up, the
    // foreground window is one of them, and the tree we would read is our own.
    {
        recorder::initializeUiaForThread();
        std::string uiaError;
        uiaSnapshot_ = recorder::dumpWindowTree({}, 40, 4000, uiaError);
        recorder::uninitializeUiaForThread();
    }
    proposedRelative_.reset();
    // A fresh picking session starts with no choice made, so the panel may
    // suggest again.
    modeChosenByUser_ = false;

    std::string captureError;
    const cv::Mat capture = vision::ScreenCapture::grab(std::nullopt, captureError);
    if (capture.empty()) {
        error = QString::fromStdString(captureError);
        return false;
    }

    const core::Rect desktop = vision::ScreenCapture::virtualDesktopBounds();
    const QPixmap frozen = QPixmap::fromImage(matToImage(capture));
    if (frozen.isNull()) {
        error = QStringLiteral("已擷取畫面，但無法從中建立影像。");
        return false;
    }

    // Qt describes the monitors in logical pixels, Windows in physical ones.
    // Pairing them gives each surface the scale factor for its own screen.
    std::vector<core::Rect> logicalScreens;
    for (const QScreen* screen : QGuiApplication::screens()) {
        const QRect geometry = screen->geometry();
        logicalScreens.push_back(
            core::Rect{geometry.x(), geometry.y(), geometry.width(), geometry.height()});
    }

    std::vector<core::ScreenPairing> pairings =
        core::pairScreens(logicalScreens, vision::ScreenCapture::monitors());
    if (pairings.empty()) {
        // The two views disagreed about how many monitors exist. One cover over
        // the whole logical desktop still lets the user pick; on a mixed-DPI
        // setup it will be imprecise, which `--dump-geometry` will show.
        QRect logicalUnion;
        for (const core::Rect& screen : logicalScreens) {
            logicalUnion = logicalUnion.united(QRect(screen.x, screen.y, screen.width, screen.height));
        }
        if (logicalUnion.isEmpty()) {
            logicalUnion = QRect(desktop.x, desktop.y, desktop.width, desktop.height);
        }
        pairings.push_back(core::ScreenPairing{
            core::Rect{logicalUnion.x(), logicalUnion.y(), logicalUnion.width(),
                       logicalUnion.height()},
            desktop});
    }

    surfaces_.clear();
    activeSurface_ = nullptr;
    dragging_ = false;
    hasSelection_ = false;
    dragStart_ = QPoint();
    dragEnd_ = QPoint();
    recognisedText_.clear();
    recognisedBox_.reset();
    confirmButton_->setEnabled(false);
    // Reopening the picker otherwise shows the previous session's crop next to a
    // flow that has no selection yet.
    templatePreview_->clear();
    ocrResultLabel_->setText(locator_ && locator_->ocrReady()
                                 ? QStringLiteral("拖一個框，我來讀裡面的文字。")
                                 : QStringLiteral("尚未載入 OCR 模型，只能用圖片比對。"));
    if (locator_ && !locator_->ocrReady()) templateRadio_->setChecked(true);

    for (const core::ScreenPairing& pairing : pairings) {
        // The capture spans the virtual desktop, so shift each monitor's physical
        // rect into the capture's own coordinates before slicing it out.
        const QRect sliceRect =
            QRect(pairing.physical.x - desktop.x, pairing.physical.y - desktop.y,
                  pairing.physical.width, pairing.physical.height)
                .intersected(QRect(0, 0, frozen.width(), frozen.height()));
        if (sliceRect.isEmpty()) continue;

        auto surface = std::make_unique<PickerSurface>(this, pairing, frozen.copy(sliceRect));
        surface->show();
        surfaces_.push_back(std::move(surface));
    }

    if (surfaces_.empty()) {
        error = QStringLiteral("找不到可以覆蓋的螢幕。");
        return false;
    }

    // Shown after the covers so it stacks above them.
    placePanel();
    show();
    raise();
    activateWindow();
    setFocus();
    return true;
}

void TargetPickerOverlay::placePanel() {
    adjustSize();

    // On the screen the pointer is on, so the panel appears where the user is
    // already looking. Its own window means it also picks up that screen's scale
    // factor and renders at the right size there.
    const QPoint cursor = QCursor::pos();
    const QScreen* screen = QGuiApplication::screenAt(cursor);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect bounds = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);

    const int top = std::max(bounds.y() + kPanelMargin,
                             bounds.bottom() - height() - kPanelMargin);
    move(bounds.x() + kPanelMargin, top);
}

void TargetPickerOverlay::dismiss() {
    hide();
    // Destroyed rather than hidden: they hold a full-screen pixmap each, and a
    // later pick rebuilds them anyway from a fresh capture.
    surfaces_.clear();
    activeSurface_ = nullptr;
    dragging_ = false;
}

void TargetPickerOverlay::surfacePressed(PickerSurface* surface, const QPoint& local) {
    // Starting a band on another screen abandons the previous one, so only one
    // surface ever shows a marquee.
    if (activeSurface_ && activeSurface_ != surface) activeSurface_->setMarquee(QRect());

    activeSurface_ = surface;
    dragging_ = true;
    hasSelection_ = false;
    dragStart_ = local;
    dragEnd_ = local;
    confirmButton_->setEnabled(false);
    surface->setMarquee(QRect());
}

void TargetPickerOverlay::surfaceDragged(PickerSurface* surface, const QPoint& local) {
    if (!dragging_ || surface != activeSurface_) return;
    dragEnd_ = local;
    surface->setMarquee(selectionRect());
}

void TargetPickerOverlay::surfaceReleased(PickerSurface* surface, const QPoint& local) {
    if (!dragging_ || surface != activeSurface_) return;
    dragging_ = false;
    dragEnd_ = local;

    const QRect selection = selectionRect();
    // Ignore a stray click that produced a degenerate box.
    if (selection.width() < 6 || selection.height() < 6) {
        hasSelection_ = false;
        confirmButton_->setEnabled(false);
        surface->setMarquee(QRect());
        return;
    }

    hasSelection_ = true;
    confirmButton_->setEnabled(true);
    surface->setMarquee(selection);

    templatePreview_->setPixmap(surface->slice().copy(toPixmapRect()));

    // OCR first: the proposal may switch the selected mode away from it, and
    // reading the text anyway keeps that a choice rather than a dead end.
    if (ocrRadio_->isChecked()) runOcrOnSelection();
    proposeRelativeTarget();
}

void TargetPickerOverlay::surfaceKeyPressed(int key) {
    if (key == Qt::Key_Escape) {
        cancel();
        return;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && hasSelection_) confirm();
}

void TargetPickerOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        cancel();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && hasSelection_) {
        confirm();
        return;
    }
    QWidget::keyPressEvent(event);
}

QRect TargetPickerOverlay::selectionRect() const {
    return QRect(dragStart_, dragEnd_).normalized();
}

core::Rect TargetPickerOverlay::toScreenRect() const {
    if (!activeSurface_) return {};
    const QRect selection = selectionRect();
    return activeSurface_->mapping().toPhysical(selection.x(), selection.y(), selection.width(),
                                                selection.height());
}

QRect TargetPickerOverlay::toPixmapRect() const {
    if (!activeSurface_) return {};
    const QRect selection = selectionRect();
    // QPixmap::copy() indexes raw device pixels, so the rect has to be scaled up
    // to the slice's resolution.
    const core::Rect crop = activeSurface_->mapping().toCapture(
        selection.x(), selection.y(), selection.width(), selection.height());
    return QRect(crop.x, crop.y, crop.width, crop.height);
}

void TargetPickerOverlay::runOcrOnSelection() {
    recognisedText_.clear();
    recognisedConfidence_ = 0.0;
    recognisedBox_.reset();

    if (!locator_ || !locator_->ocrReady()) {
        ocrResultLabel_->setText(QStringLiteral("尚未載入 OCR 模型，只能用圖片比對。"));
        templateRadio_->setChecked(true);
        return;
    }
    if (!activeSurface_) return;

    // The covers hide the desktop, so a fresh capture would read the dimmed
    // overlay rather than the application. OCR the frozen slice instead.
    ocrResultLabel_->setText(QStringLiteral("正在辨識…"));
    QApplication::processEvents();

    // Crop in the slice's own pixels: OCR wants the full-resolution region, not
    // one downscaled to the overlay's logical size.
    const QRect pixmapRect = toPixmapRect();
    const cv::Mat crop = imageToMat(activeSurface_->slice().copy(pixmapRect).toImage());

    std::string error;
    const std::vector<vision::OcrLine> lines = locator_->readImage(crop, error);

    if (lines.empty()) {
        ocrResultLabel_->setText(
            error.empty() ? QStringLiteral("框選範圍裡找不到文字，請改用圖片比對。")
                          : QStringLiteral("OCR 失敗：%1").arg(QString::fromStdString(error)));
        templateRadio_->setChecked(true);
        return;
    }

    const vision::OcrLine* best = &lines.front();
    for (const auto& line : lines) {
        if (line.confidence > best->confidence) best = &line;
    }

    recognisedText_ = QString::fromStdString(best->text);
    recognisedConfidence_ = best->confidence;
    // The line's box is relative to the crop, in capture pixels; shift it by both
    // the monitor's physical origin and the crop's own offset within the monitor.
    const core::Rect origin = activeSurface_->mapping().physical;
    recognisedBox_ = core::Rect{best->box.x + origin.x + pixmapRect.x(),
                                best->box.y + origin.y + pixmapRect.y(), best->box.width,
                                best->box.height};

    ocrResultLabel_->setText(QStringLiteral("辨識結果：<b>%1</b>　信心值 %2")
                                 .arg(recognisedText_)
                                 .arg(recognisedConfidence_, 0, 'f', 2));
}

void TargetPickerOverlay::cancel() {
    dismiss();
    emit cancelled();
}

void TargetPickerOverlay::proposeRelativeTarget() {
    proposedRelative_.reset();

    // Hand the selection back to OCR before hiding the option. Leaving a hidden
    // radio checked would send the next confirm down the template branch --
    // saving a PNG for a user who asked for neither.
    if (relativeRadio_->isChecked()) {
        // Blocked so this does not re-run the OCR that just finished.
        const QSignalBlocker block(ocrRadio_);
        ocrRadio_->setChecked(true);
        matchCombo_->setEnabled(true);
    }
    relativeRadio_->setVisible(false);

    if (uiaSnapshot_.empty() || !hasSelection_) return;

    const core::Rect selection = toScreenRect();
    const core::Point centre = selection.center();

    auto contains = [&](const core::Rect& r) {
        return centre.x >= r.x && centre.x < r.x + r.width && centre.y >= r.y &&
               centre.y < r.y + r.height;
    };
    auto area = [](const core::Rect& r) { return static_cast<long long>(r.width) * r.height; };

    // Smallest containing control: the tree nests, and the innermost node under
    // the cursor is the thing pointed at rather than the panel around it.
    const recorder::UiaNode* picked = nullptr;
    for (const auto& node : uiaSnapshot_) {
        if (node.offscreen || node.bounds.empty() || !contains(node.bounds)) continue;
        if (!node.keyboardFocusable) continue;
        if (!picked || area(node.bounds) < area(picked->bounds)) picked = &node;
    }
    if (!picked) return;

    core::Target target;
    target.kind = core::TargetKind::Relative;
    target.match = core::MatchMode::Exact;
    target.role = picked->controlType == "Button"     ? core::ElementRole::Button
                  : picked->controlType == "CheckBox" ? core::ElementRole::Checkbox
                                                      : core::ElementRole::Input;

    QString sentence;
    if (!picked->name.empty()) {
        // The control names itself after its label, which is the sturdiest form
        // of this target: no coordinates are involved at all.
        target.text = picked->name;
        target.direction = core::Direction::Right;
        sentence = QStringLiteral("名稱是「%1」的%2")
                       .arg(QString::fromStdString(picked->name),
                            target.role == core::ElementRole::Button
                                ? QStringLiteral("按鈕")
                                : QStringLiteral("輸入框"));
    } else {
        // Unnamed, so anchor on the nearest label instead. Left first, then
        // above: that is the order forms actually use.
        const recorder::UiaNode* label = nullptr;
        int bestDistance = -1;
        core::Direction bestDirection = core::Direction::Right;

        for (const auto& node : uiaSnapshot_) {
            if (node.offscreen || node.bounds.empty() || node.name.empty()) continue;
            if (node.controlType != "Text") continue;

            const bool sameRow = node.bounds.y < picked->bounds.y + picked->bounds.height &&
                                 picked->bounds.y < node.bounds.y + node.bounds.height;
            const bool sameColumn = node.bounds.x < picked->bounds.x + picked->bounds.width &&
                                    picked->bounds.x < node.bounds.x + node.bounds.width;

            int distance = -1;
            core::Direction direction = core::Direction::Right;
            if (sameRow && node.bounds.x + node.bounds.width <= picked->bounds.x) {
                distance = picked->bounds.x - (node.bounds.x + node.bounds.width);
                direction = core::Direction::Right;
            } else if (sameColumn && node.bounds.y + node.bounds.height <= picked->bounds.y) {
                // Weighted so a label on the same row wins over one above at an
                // equal pixel distance.
                distance = (picked->bounds.y - (node.bounds.y + node.bounds.height)) + 1000;
                direction = core::Direction::Below;
            }
            if (distance < 0) continue;
            if (bestDistance >= 0 && distance >= bestDistance) continue;

            bestDistance = distance;
            bestDirection = direction;
            label = &node;
        }
        if (!label) return;

        target.text = label->name;
        target.direction = bestDirection;
        sentence = QStringLiteral("「%1」%2的%3")
                       .arg(QString::fromStdString(label->name),
                            bestDirection == core::Direction::Right ? QStringLiteral("右邊")
                                                                    : QStringLiteral("下面"),
                            target.role == core::ElementRole::Button
                                ? QStringLiteral("按鈕")
                                : QStringLiteral("輸入框"));
    }

    proposedRelative_ = target;
    relativeRadio_->setText(QStringLiteral("%1  —— 不看畫面，最穩").arg(sentence));
    relativeRadio_->setToolTip(
        QStringLiteral("由程式本身回報的控制項資訊定位，不受字級、顯示縮放或視窗位移影響。"
                       "遠端桌面或自繪介面則不適用，那時請改用上面兩種。"));
    relativeRadio_->setVisible(true);

    // Pre-selected only while the user has not picked a mode themselves. It is
    // the sturdier target, so it is worth offering by default -- but someone who
    // deliberately chose image matching and then drew a box does not want the
    // panel quietly switching them to something else on release.
    if (!modeChosenByUser_) relativeRadio_->setChecked(true);
}

void TargetPickerOverlay::confirm() {
    if (!hasSelection_ || !activeSurface_) return;

    if (relativeRadio_->isChecked() && proposedRelative_) {
        core::Target target = *proposedRelative_;
        target.offsetX = offsetXSpin_->value();
        target.offsetY = offsetYSpin_->value();
        emit targetPicked(target);
        dismiss();
        return;
    }

    core::Target target;
    target.offsetX = offsetXSpin_->value();
    target.offsetY = offsetYSpin_->value();

    if (ocrRadio_->isChecked() && !recognisedText_.isEmpty()) {
        target.kind = core::TargetKind::Ocr;
        target.text = recognisedText_.toStdString();
        core::parseMatchMode(matchCombo_->currentData().toString().toStdString(), target.match);
    } else {
        target.kind = core::TargetKind::Image;
        target.threshold = thresholdSpin_->value();

        // Persist the crop; a template target is only as good as its file.
        QDir directory(templateDirectory_);
        if (!directory.exists()) directory.mkpath(QStringLiteral("."));

        const QString filename =
            QStringLiteral("template_%1.png")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmsszzz")));
        const QString absolutePath = directory.filePath(filename);

        // Saved at capture resolution, because TemplateMatcher will match it
        // against a physical-pixel screen grab. A logical-sized crop would be the
        // wrong scale and would never match on a scaled display.
        if (activeSurface_->slice().copy(toPixmapRect()).save(absolutePath, "PNG")) {
            // Store it relative to the project so the flow stays portable.
            target.templatePath = QStringLiteral("assets/%1").arg(filename).toStdString();
        } else {
            target.templatePath = absolutePath.toStdString();
        }
    }

    dismiss();
    emit targetPicked(target);
}

}  // namespace rpa::studio
