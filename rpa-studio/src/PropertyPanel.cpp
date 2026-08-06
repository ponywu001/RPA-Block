#include "PropertyPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

#include <nlohmann/json.hpp>

#include "rpa/core/ScriptIO.h"

namespace rpa::studio {

using json = nlohmann::json;

namespace {

void fillMatchCombo(QComboBox* combo) {
    // "包含" first, because it is the default and the one that works: OCR often
    // picks up a neighbouring glyph, so exact matching fails on text that is
    // visibly on screen.
    combo->addItem(QStringLiteral("包含"), QStringLiteral("contains"));
    combo->addItem(QStringLiteral("完全相同"), QStringLiteral("exact"));
    combo->addItem(QStringLiteral("正規表示式"), QStringLiteral("regex"));
}

void selectByData(QComboBox* combo, const QString& value) {
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

QSpinBox* makeCoordinateSpin(QWidget* parent) {
    auto* spin = new QSpinBox(parent);
    // Negative values are legitimate: a secondary monitor to the left of the
    // primary one has negative screen coordinates.
    spin->setRange(-32768, 32767);
    return spin;
}

}  // namespace

PropertyPanel::PropertyPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // Scroll vertically only. Letting it scroll sideways hides the right-hand
    // edge of every field instead of making the dock wide enough.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto* container = new QWidget(scroll);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* commonBox = new QGroupBox(QStringLiteral("積木"), container);
    auto* commonForm = new QFormLayout(commonBox);
    buildCommonSection(commonForm);
    layout->addWidget(commonBox);

    pages_ = new QStackedWidget(container);
    pages_->addWidget(buildClickPage());       // 0
    pages_->addWidget(buildTypeTextPage());    // 1
    pages_->addWidget(buildKeyPressPage());    // 2
    pages_->addWidget(buildWaitPage());        // 3
    pages_->addWidget(buildLocatePage());      // 4
    pages_->addWidget(buildWindowPage());      // 5
    pages_->addWidget(buildScreenshotPage());  // 6
    pages_->addWidget(buildBranchPage());      // 7
    pages_->addWidget(buildHttpPage());        // 8
    layout->addWidget(pages_);

    // Offset, region, and retry apply to click *and* to the two locate steps, so
    // the box lives outside the stack and is shown per step type. Putting it on
    // one page would leave ocr_find/image_find unable to edit their own retry
    // policy, while step() still read those hidden widgets.
    tuningBox_ = buildTuningBox();
    layout->addWidget(tuningBox_);

    layout->addStretch(1);
    scroll->setWidget(container);

    clearStep();
}

void PropertyPanel::buildCommonSection(QFormLayout* layout) {
    typeLabel_ = new QLineEdit(this);
    typeLabel_->setReadOnly(true);
    layout->addRow(QStringLiteral("種類"), typeLabel_);

    idEdit_ = new QLineEdit(this);
    layout->addRow(QStringLiteral("識別碼"), idEdit_);
    connect(idEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    commentEdit_ = new QLineEdit(this);
    commentEdit_->setPlaceholderText(QStringLiteral("這個積木是做什麼的"));
    layout->addRow(QStringLiteral("備註"), commentEdit_);
    connect(commentEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    enabledCheck_ = new QCheckBox(QStringLiteral("啟用"), this);
    layout->addRow(QString(), enabledCheck_);
    connect(enabledCheck_, &QCheckBox::toggled, this, [this] { emitEdit(); });

    onFailCombo_ = new QComboBox(this);
    onFailCombo_->addItem(QStringLiteral("中止整個流程"), QStringLiteral("abort"));
    onFailCombo_->addItem(QStringLiteral("跳過，繼續下一個"), QStringLiteral("continue"));
    onFailCombo_->addItem(QStringLiteral("跳到指定積木"), QStringLiteral("goto"));
    layout->addRow(QStringLiteral("失敗時"), onFailCombo_);

    gotoCombo_ = new QComboBox(this);
    gotoCombo_->setEnabled(false);
    layout->addRow(QStringLiteral("跳到"), gotoCombo_);

    connect(onFailCombo_, &QComboBox::currentIndexChanged, this, [this] {
        gotoCombo_->setEnabled(onFailCombo_->currentData().toString() == QStringLiteral("goto"));
        emitEdit();
    });
    connect(gotoCombo_, &QComboBox::currentIndexChanged, this, [this] { emitEdit(); });
}

QWidget* PropertyPanel::buildClickPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* targetBox = new QGroupBox(QStringLiteral("目標"), page);
    auto* form = new QFormLayout(targetBox);

    targetKindCombo_ = new QComboBox(page);
    targetKindCombo_->addItem(QStringLiteral("文字錨點（OCR）"), QStringLiteral("ocr"));
    targetKindCombo_->addItem(QStringLiteral("圖片比對"), QStringLiteral("image"));
    targetKindCombo_->addItem(QStringLiteral("固定座標"), QStringLiteral("point"));
    form->addRow(QStringLiteral("定位方式"), targetKindCombo_);
    connect(targetKindCombo_, &QComboBox::currentIndexChanged, this, [this] { emitEdit(); });

    targetTextEdit_ = new QLineEdit(page);
    form->addRow(QStringLiteral("錨點文字"), targetTextEdit_);
    connect(targetTextEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    targetMatchCombo_ = new QComboBox(page);
    fillMatchCombo(targetMatchCombo_);
    form->addRow(QStringLiteral("比對方式"), targetMatchCombo_);
    connect(targetMatchCombo_, &QComboBox::currentIndexChanged, this, [this] { emitEdit(); });

    targetTemplateEdit_ = new QLineEdit(page);
    targetTemplateEdit_->setPlaceholderText(QStringLiteral("assets/button.png"));
    form->addRow(QStringLiteral("圖片檔"), targetTemplateEdit_);
    connect(targetTemplateEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    targetThresholdSpin_ = new QDoubleSpinBox(page);
    targetThresholdSpin_->setRange(0.10, 1.00);
    targetThresholdSpin_->setSingleStep(0.01);
    targetThresholdSpin_->setDecimals(2);
    form->addRow(QStringLiteral("相似度"), targetThresholdSpin_);
    connect(targetThresholdSpin_, &QDoubleSpinBox::valueChanged, this, [this] { emitEdit(); });

    auto* pointRow = new QWidget(page);
    auto* pointLayout = new QHBoxLayout(pointRow);
    pointLayout->setContentsMargins(0, 0, 0, 0);
    targetPointXSpin_ = makeCoordinateSpin(pointRow);
    targetPointYSpin_ = makeCoordinateSpin(pointRow);
    pointLayout->addWidget(new QLabel(QStringLiteral("X"), pointRow));
    pointLayout->addWidget(targetPointXSpin_);
    pointLayout->addWidget(new QLabel(QStringLiteral("Y"), pointRow));
    pointLayout->addWidget(targetPointYSpin_);
    form->addRow(QStringLiteral("座標"), pointRow);
    connect(targetPointXSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });
    connect(targetPointYSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });

    pickTargetButton_ = new QPushButton(QStringLiteral("🎯  在畫面上選取目標"), page);
    form->addRow(QString(), pickTargetButton_);
    connect(pickTargetButton_, &QPushButton::clicked, this,
            [this] { emit targetPickRequested(); });

    layout->addWidget(targetBox);

    auto* clickBox = new QGroupBox(QStringLiteral("點擊"), page);
    auto* clickForm = new QFormLayout(clickBox);

    buttonCombo_ = new QComboBox(page);
    buttonCombo_->addItem(QStringLiteral("左鍵"), QStringLiteral("left"));
    buttonCombo_->addItem(QStringLiteral("右鍵"), QStringLiteral("right"));
    buttonCombo_->addItem(QStringLiteral("中鍵"), QStringLiteral("middle"));
    clickForm->addRow(QStringLiteral("按鍵"), buttonCombo_);
    connect(buttonCombo_, &QComboBox::currentIndexChanged, this, [this] { emitEdit(); });

    clickCountSpin_ = new QSpinBox(page);
    clickCountSpin_->setRange(1, 3);
    clickForm->addRow(QStringLiteral("次數"), clickCountSpin_);
    connect(clickCountSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });

    layout->addWidget(clickBox);
    layout->addStretch(1);
    return page;
}

QWidget* PropertyPanel::buildTuningBox() {
    auto* page = new QGroupBox(QStringLiteral("偏移、搜尋範圍、重試"), this);
    auto* tuningForm = new QFormLayout(page);

    auto* offsetRow = new QWidget(page);
    auto* offsetLayout = new QHBoxLayout(offsetRow);
    offsetLayout->setContentsMargins(0, 0, 0, 0);
    offsetXSpin_ = makeCoordinateSpin(offsetRow);
    offsetYSpin_ = makeCoordinateSpin(offsetRow);
    offsetLayout->addWidget(new QLabel(QStringLiteral("X"), offsetRow));
    offsetLayout->addWidget(offsetXSpin_);
    offsetLayout->addWidget(new QLabel(QStringLiteral("Y"), offsetRow));
    offsetLayout->addWidget(offsetYSpin_);
    tuningForm->addRow(QStringLiteral("點擊偏移"), offsetRow);
    connect(offsetXSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });
    connect(offsetYSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });

    regionCheck_ = new QCheckBox(QStringLiteral("只在指定範圍內搜尋"), page);
    tuningForm->addRow(QString(), regionCheck_);

    auto* regionRow = new QWidget(page);
    auto* regionLayout = new QHBoxLayout(regionRow);
    regionLayout->setContentsMargins(0, 0, 0, 0);
    regionXSpin_ = makeCoordinateSpin(regionRow);
    regionYSpin_ = makeCoordinateSpin(regionRow);
    regionWSpin_ = new QSpinBox(regionRow);
    regionWSpin_->setRange(0, 32767);
    regionHSpin_ = new QSpinBox(regionRow);
    regionHSpin_->setRange(0, 32767);
    for (const auto& labelled : {std::pair<const char*, QSpinBox*>{"X", regionXSpin_},
                                 {"Y", regionYSpin_},
                                 {"W", regionWSpin_},
                                 {"H", regionHSpin_}}) {
        regionLayout->addWidget(new QLabel(QString::fromLatin1(labelled.first), regionRow));
        regionLayout->addWidget(labelled.second);
    }
    tuningForm->addRow(QStringLiteral("範圍"), regionRow);

    auto updateRegionEnabled = [this] {
        const bool on = regionCheck_->isChecked();
        regionXSpin_->setEnabled(on);
        regionYSpin_->setEnabled(on);
        regionWSpin_->setEnabled(on);
        regionHSpin_->setEnabled(on);
    };
    connect(regionCheck_, &QCheckBox::toggled, this, [this, updateRegionEnabled] {
        updateRegionEnabled();
        emitEdit();
    });
    for (QSpinBox* spin : {regionXSpin_, regionYSpin_, regionWSpin_, regionHSpin_}) {
        connect(spin, &QSpinBox::valueChanged, this, [this] { emitEdit(); });
    }
    updateRegionEnabled();

    auto* retryRow = new QWidget(page);
    auto* retryLayout = new QHBoxLayout(retryRow);
    retryLayout->setContentsMargins(0, 0, 0, 0);
    retryTimesSpin_ = new QSpinBox(retryRow);
    retryTimesSpin_->setRange(1, 100);
    retryIntervalSpin_ = new QSpinBox(retryRow);
    retryIntervalSpin_->setRange(0, 60000);
    retryIntervalSpin_->setSingleStep(100);
    retryIntervalSpin_->setSuffix(QStringLiteral(" 毫秒"));
    retryLayout->addWidget(retryTimesSpin_);
    retryLayout->addWidget(new QLabel(QStringLiteral("次，每次間隔"), retryRow));
    retryLayout->addWidget(retryIntervalSpin_);
    tuningForm->addRow(QStringLiteral("重試"), retryRow);
    connect(retryTimesSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });
    connect(retryIntervalSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });

    return page;
}

QWidget* PropertyPanel::buildTypeTextPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    textEdit_ = new QPlainTextEdit(page);
    textEdit_->setPlaceholderText(QStringLiteral("要輸入的文字。{{變數名}} 會在執行時代換成實際值。"));
    textEdit_->setMaximumHeight(120);
    form->addRow(QStringLiteral("內容"), textEdit_);
    connect(textEdit_, &QPlainTextEdit::textChanged, this, [this] { emitEdit(); });

    intervalSpin_ = new QSpinBox(page);
    intervalSpin_->setRange(0, 5000);
    intervalSpin_->setSuffix(QStringLiteral(" 毫秒"));
    form->addRow(QStringLiteral("每字間隔"), intervalSpin_);
    connect(intervalSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });

    return page;
}

QWidget* PropertyPanel::buildKeyPressPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    keysEdit_ = new QLineEdit(page);
    keysEdit_->setPlaceholderText(QStringLiteral("ctrl+shift+s"));
    form->addRow(QStringLiteral("按鍵組合"), keysEdit_);
    connect(keysEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    auto* hint = new QLabel(
        QStringLiteral("修飾鍵：ctrl、alt、shift、win。\n"
                       "特殊鍵：enter、tab、escape、f1–f12、方向鍵、home、end、"
                       "pageup、pagedown、delete、backspace、space。"),
        page);
    hint->setWordWrap(true);
    form->addRow(QString(), hint);

    return page;
}

QWidget* PropertyPanel::buildWaitPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    waitSpin_ = new QSpinBox(page);
    waitSpin_->setRange(0, 600000);
    waitSpin_->setSingleStep(100);
    waitSpin_->setSuffix(QStringLiteral(" 毫秒"));
    form->addRow(QStringLiteral("等待"), waitSpin_);
    connect(waitSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });

    return page;
}

QWidget* PropertyPanel::buildLocatePage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    locateTextEdit_ = new QLineEdit(page);
    form->addRow(QStringLiteral("錨點文字"), locateTextEdit_);
    connect(locateTextEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    locateMatchCombo_ = new QComboBox(page);
    fillMatchCombo(locateMatchCombo_);
    form->addRow(QStringLiteral("比對方式"), locateMatchCombo_);
    connect(locateMatchCombo_, &QComboBox::currentIndexChanged, this, [this] { emitEdit(); });

    locateTemplateEdit_ = new QLineEdit(page);
    locateTemplateEdit_->setPlaceholderText(QStringLiteral("assets/button.png"));
    form->addRow(QStringLiteral("圖片檔"), locateTemplateEdit_);
    connect(locateTemplateEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    locateThresholdSpin_ = new QDoubleSpinBox(page);
    locateThresholdSpin_->setRange(0.10, 1.00);
    locateThresholdSpin_->setSingleStep(0.01);
    locateThresholdSpin_->setDecimals(2);
    form->addRow(QStringLiteral("相似度"), locateThresholdSpin_);
    connect(locateThresholdSpin_, &QDoubleSpinBox::valueChanged, this, [this] { emitEdit(); });

    saveToVarEdit_ = new QLineEdit(page);
    saveToVarEdit_->setPlaceholderText(QStringLiteral("last_match"));
    form->addRow(QStringLiteral("結果存入變數"), saveToVarEdit_);
    connect(saveToVarEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    pickLocateButton_ = new QPushButton(QStringLiteral("🎯  在畫面上選取目標"), page);
    form->addRow(QString(), pickLocateButton_);
    connect(pickLocateButton_, &QPushButton::clicked, this,
            [this] { emit targetPickRequested(); });

    auto* hint = new QLabel(
        QStringLiteral("找到後會寫入 名稱、名稱_x、名稱_y、名稱_found、名稱_confidence "
                       "這幾個變數，後面的積木可以用 {{名稱}} 取用。"),
        page);
    hint->setWordWrap(true);
    form->addRow(QString(), hint);

    return page;
}

QWidget* PropertyPanel::buildWindowPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    titleMatchEdit_ = new QLineEdit(page);
    titleMatchEdit_->setPlaceholderText(QStringLiteral("標題的一部分即可"));
    form->addRow(QStringLiteral("視窗標題"), titleMatchEdit_);
    connect(titleMatchEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    titleMatchCombo_ = new QComboBox(page);
    fillMatchCombo(titleMatchCombo_);
    form->addRow(QStringLiteral("比對方式"), titleMatchCombo_);
    connect(titleMatchCombo_, &QComboBox::currentIndexChanged, this, [this] { emitEdit(); });

    return page;
}

QWidget* PropertyPanel::buildScreenshotPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    screenshotPathEdit_ = new QLineEdit(page);
    screenshotPathEdit_->setPlaceholderText(QStringLiteral("out/step.png"));
    form->addRow(QStringLiteral("存到"), screenshotPathEdit_);
    connect(screenshotPathEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    return page;
}

QWidget* PropertyPanel::buildBranchPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    loopCountSpin_ = new QSpinBox(page);
    loopCountSpin_->setRange(1, 100000);
    form->addRow(QStringLiteral("重複次數"), loopCountSpin_);
    connect(loopCountSpin_, &QSpinBox::valueChanged, this, [this] { emitEdit(); });

    branchJsonEdit_ = new QPlainTextEdit(page);
    branchJsonEdit_->setReadOnly(true);
    branchJsonEdit_->setMinimumHeight(220);
    form->addRow(QStringLiteral("內部積木"), branchJsonEdit_);

    auto* hint = new QLabel(
        QStringLiteral("內部積木請直接在畫布上拖放編輯。這裡的 JSON 只供檢視，"
                       "也可以請 AI 助手幫你改。"),
        page);
    hint->setWordWrap(true);
    form->addRow(QString(), hint);

    return page;
}

QWidget* PropertyPanel::buildHttpPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    httpMethodCombo_ = new QComboBox(page);
    for (const char* method : {"GET", "POST", "PUT", "PATCH", "DELETE"}) {
        httpMethodCombo_->addItem(QString::fromLatin1(method), QString::fromLatin1(method));
    }
    form->addRow(QStringLiteral("方法"), httpMethodCombo_);
    connect(httpMethodCombo_, &QComboBox::currentIndexChanged, this, [this] { emitEdit(); });

    urlEdit_ = new QLineEdit(page);
    urlEdit_->setPlaceholderText(QStringLiteral("https://api.example.com/hook"));
    form->addRow(QStringLiteral("網址"), urlEdit_);
    connect(urlEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    headersEdit_ = new QPlainTextEdit(page);
    headersEdit_->setPlaceholderText(QStringLiteral("一行一個：  標頭名稱: 值"));
    headersEdit_->setMaximumHeight(90);
    form->addRow(QStringLiteral("標頭"), headersEdit_);
    connect(headersEdit_, &QPlainTextEdit::textChanged, this, [this] { emitEdit(); });

    bodyEdit_ = new QPlainTextEdit(page);
    bodyEdit_->setMaximumHeight(120);
    form->addRow(QStringLiteral("內文"), bodyEdit_);
    connect(bodyEdit_, &QPlainTextEdit::textChanged, this, [this] { emitEdit(); });

    httpSaveVarEdit_ = new QLineEdit(page);
    httpSaveVarEdit_->setPlaceholderText(QStringLiteral("response"));
    form->addRow(QStringLiteral("回應存入變數"), httpSaveVarEdit_);
    connect(httpSaveVarEdit_, &QLineEdit::textEdited, this, [this] { emitEdit(); });

    return page;
}

int PropertyPanel::pageIndexFor(core::StepType type) const {
    switch (type) {
        case core::StepType::Click:
        case core::StepType::DoubleClick: return 0;
        case core::StepType::TypeText: return 1;
        case core::StepType::KeyPress: return 2;
        case core::StepType::Wait: return 3;
        case core::StepType::OcrFind:
        case core::StepType::ImageFind: return 4;
        case core::StepType::WindowActivate: return 5;
        case core::StepType::Screenshot: return 6;
        case core::StepType::If:
        case core::StepType::Loop: return 7;
        case core::StepType::HttpRequest: return 8;
    }
    return 3;
}

void PropertyPanel::setStep(const core::Step& step) {
    step_ = step;
    hasStep_ = true;
    setEnabled(true);
    loadInto();
}

void PropertyPanel::clearStep() {
    hasStep_ = false;
    setEnabled(false);
    typeLabel_->setText(QStringLiteral("（尚未選取積木）"));
    idEdit_->clear();
    commentEdit_->clear();
}

void PropertyPanel::setAvailableStepIds(const QStringList& ids, const QString& currentStepId) {
    const QString previous = gotoCombo_->currentData().toString();

    loading_ = true;
    gotoCombo_->clear();
    for (const QString& id : ids) {
        // Jumping to itself would loop forever, so it is not offered.
        if (id == currentStepId) continue;
        gotoCombo_->addItem(id, id);
    }
    if (!previous.isEmpty()) selectByData(gotoCombo_, previous);
    loading_ = false;
}

void PropertyPanel::loadInto() {
    loading_ = true;

    typeLabel_->setText(QString::fromStdString(core::toString(step_.type)));
    idEdit_->setText(QString::fromStdString(step_.id));
    commentEdit_->setText(QString::fromStdString(step_.comment));
    enabledCheck_->setChecked(step_.enabled);

    switch (step_.onFail) {
        case core::FailurePolicy::Abort: selectByData(onFailCombo_, QStringLiteral("abort")); break;
        case core::FailurePolicy::Continue:
            selectByData(onFailCombo_, QStringLiteral("continue"));
            break;
        case core::FailurePolicy::Goto: selectByData(onFailCombo_, QStringLiteral("goto")); break;
    }
    gotoCombo_->setEnabled(step_.onFail == core::FailurePolicy::Goto);
    if (!step_.onFailGoto.empty()) {
        selectByData(gotoCombo_, QString::fromStdString(step_.onFailGoto));
    }

    pages_->setCurrentIndex(pageIndexFor(step_.type));

    // Only the targeting step types have anything to tune.
    const bool tunable = step_.type == core::StepType::Click ||
                         step_.type == core::StepType::DoubleClick ||
                         step_.type == core::StepType::OcrFind ||
                         step_.type == core::StepType::ImageFind;
    tuningBox_->setVisible(tunable);

    // Target / locator fields
    const core::Target& target = step_.target;
    switch (target.kind) {
        case core::TargetKind::Ocr: selectByData(targetKindCombo_, QStringLiteral("ocr")); break;
        case core::TargetKind::Image: selectByData(targetKindCombo_, QStringLiteral("image")); break;
        case core::TargetKind::Point: selectByData(targetKindCombo_, QStringLiteral("point")); break;
    }
    targetTextEdit_->setText(QString::fromStdString(target.text));
    selectByData(targetMatchCombo_, QString::fromStdString(core::toString(target.match)));
    targetTemplateEdit_->setText(QString::fromStdString(target.templatePath));
    targetThresholdSpin_->setValue(target.threshold);
    targetPointXSpin_->setValue(target.point.x);
    targetPointYSpin_->setValue(target.point.y);
    selectByData(buttonCombo_, QString::fromStdString(core::toString(step_.button)));
    clickCountSpin_->setValue(std::max(1, step_.clickCount));

    locateTextEdit_->setText(QString::fromStdString(target.text));
    selectByData(locateMatchCombo_, QString::fromStdString(core::toString(target.match)));
    locateTemplateEdit_->setText(QString::fromStdString(target.templatePath));
    locateThresholdSpin_->setValue(target.threshold);
    saveToVarEdit_->setText(QString::fromStdString(step_.saveToVar));

    offsetXSpin_->setValue(target.offsetX);
    offsetYSpin_->setValue(target.offsetY);
    regionCheck_->setChecked(target.region.has_value());
    if (target.region) {
        regionXSpin_->setValue(target.region->x);
        regionYSpin_->setValue(target.region->y);
        regionWSpin_->setValue(target.region->width);
        regionHSpin_->setValue(target.region->height);
    }
    retryTimesSpin_->setValue(std::max(1, target.retry.times));
    retryIntervalSpin_->setValue(target.retry.intervalMs);

    textEdit_->setPlainText(QString::fromStdString(step_.text));
    intervalSpin_->setValue(step_.intervalMs);
    keysEdit_->setText(QString::fromStdString(step_.keys));
    waitSpin_->setValue(step_.waitMs);
    titleMatchEdit_->setText(QString::fromStdString(step_.titleMatch));
    selectByData(titleMatchCombo_, QString::fromStdString(core::toString(step_.titleMatchMode)));
    screenshotPathEdit_->setText(QString::fromStdString(step_.path));

    loopCountSpin_->setValue(std::max(1, step_.loopCount));
    {
        // Show the nested body by round-tripping a one-step script through the
        // serializer, which guarantees the JSON matches the on-disk format.
        core::Script wrapper;
        wrapper.name = "branch";
        wrapper.steps.push_back(step_);
        branchJsonEdit_->setPlainText(QString::fromStdString(core::serializeScript(wrapper, true)));
    }

    selectByData(httpMethodCombo_, QString::fromStdString(step_.httpMethod));
    urlEdit_->setText(QString::fromStdString(step_.url));
    QStringList headerLines;
    for (const auto& [key, value] : step_.headers) {
        headerLines << QStringLiteral("%1: %2").arg(QString::fromStdString(key),
                                                    QString::fromStdString(value));
    }
    headersEdit_->setPlainText(headerLines.join(QLatin1Char('\n')));
    bodyEdit_->setPlainText(QString::fromStdString(step_.body));
    httpSaveVarEdit_->setText(QString::fromStdString(step_.saveToVar));

    // Reveal only the target fields the chosen locate mode uses.
    const QString kind = targetKindCombo_->currentData().toString();
    const bool isOcr = kind == QStringLiteral("ocr");
    const bool isImage = kind == QStringLiteral("image");
    targetTextEdit_->setEnabled(isOcr);
    targetMatchCombo_->setEnabled(isOcr);
    targetTemplateEdit_->setEnabled(isImage);
    targetThresholdSpin_->setEnabled(isImage);
    targetPointXSpin_->setEnabled(!isOcr && !isImage);
    targetPointYSpin_->setEnabled(!isOcr && !isImage);

    const bool locatingByText = step_.type == core::StepType::OcrFind;
    locateTextEdit_->setEnabled(locatingByText);
    locateMatchCombo_->setEnabled(locatingByText);
    locateTemplateEdit_->setEnabled(!locatingByText);
    locateThresholdSpin_->setEnabled(!locatingByText);

    loading_ = false;
}

core::Step PropertyPanel::step() const {
    core::Step step = step_;

    step.id = idEdit_->text().trimmed().toStdString();
    step.comment = commentEdit_->text().toStdString();
    step.enabled = enabledCheck_->isChecked();

    const QString onFail = onFailCombo_->currentData().toString();
    if (onFail == QStringLiteral("continue")) {
        step.onFail = core::FailurePolicy::Continue;
        step.onFailGoto.clear();
    } else if (onFail == QStringLiteral("goto")) {
        step.onFail = core::FailurePolicy::Goto;
        step.onFailGoto = gotoCombo_->currentData().toString().toStdString();
    } else {
        step.onFail = core::FailurePolicy::Abort;
        step.onFailGoto.clear();
    }

    switch (step.type) {
        case core::StepType::Click:
        case core::StepType::DoubleClick: {
            const QString kind = targetKindCombo_->currentData().toString();
            if (kind == QStringLiteral("ocr")) {
                step.target.kind = core::TargetKind::Ocr;
            } else if (kind == QStringLiteral("image")) {
                step.target.kind = core::TargetKind::Image;
            } else {
                step.target.kind = core::TargetKind::Point;
            }
            step.target.text = targetTextEdit_->text().toStdString();
            core::parseMatchMode(targetMatchCombo_->currentData().toString().toStdString(),
                                 step.target.match);
            step.target.templatePath = targetTemplateEdit_->text().toStdString();
            step.target.threshold = targetThresholdSpin_->value();
            step.target.point = core::Point{targetPointXSpin_->value(), targetPointYSpin_->value()};
            core::parseMouseButton(buttonCombo_->currentData().toString().toStdString(),
                                   step.button);
            step.clickCount = clickCountSpin_->value();
            break;
        }
        case core::StepType::TypeText:
            step.text = textEdit_->toPlainText().toStdString();
            step.intervalMs = intervalSpin_->value();
            break;
        case core::StepType::KeyPress:
            step.keys = keysEdit_->text().trimmed().toStdString();
            break;
        case core::StepType::Wait:
            step.waitMs = waitSpin_->value();
            break;
        case core::StepType::OcrFind:
            step.target.kind = core::TargetKind::Ocr;
            step.target.text = locateTextEdit_->text().toStdString();
            core::parseMatchMode(locateMatchCombo_->currentData().toString().toStdString(),
                                 step.target.match);
            step.saveToVar = saveToVarEdit_->text().trimmed().toStdString();
            if (step.saveToVar.empty()) step.saveToVar = "last_match";
            break;
        case core::StepType::ImageFind:
            step.target.kind = core::TargetKind::Image;
            step.target.templatePath = locateTemplateEdit_->text().toStdString();
            step.target.threshold = locateThresholdSpin_->value();
            step.saveToVar = saveToVarEdit_->text().trimmed().toStdString();
            if (step.saveToVar.empty()) step.saveToVar = "last_match";
            break;
        case core::StepType::WindowActivate:
            step.titleMatch = titleMatchEdit_->text().toStdString();
            core::parseMatchMode(titleMatchCombo_->currentData().toString().toStdString(),
                                 step.titleMatchMode);
            break;
        case core::StepType::Screenshot:
            step.path = screenshotPathEdit_->text().toStdString();
            break;
        case core::StepType::If:
            // Condition and bodies are not editable here; they survive untouched.
            break;
        case core::StepType::Loop:
            step.loopCount = loopCountSpin_->value();
            break;
        case core::StepType::HttpRequest: {
            step.httpMethod = httpMethodCombo_->currentData().toString().toStdString();
            step.url = urlEdit_->text().trimmed().toStdString();
            step.body = bodyEdit_->toPlainText().toStdString();
            step.saveToVar = httpSaveVarEdit_->text().trimmed().toStdString();

            step.headers.clear();
            const QStringList lines = headersEdit_->toPlainText().split(QLatin1Char('\n'));
            for (const QString& line : lines) {
                const int colon = line.indexOf(QLatin1Char(':'));
                if (colon <= 0) continue;
                const QString key = line.left(colon).trimmed();
                const QString value = line.mid(colon + 1).trimmed();
                if (!key.isEmpty()) step.headers[key.toStdString()] = value.toStdString();
            }
            break;
        }
    }

    // Only read the tuning widgets for the step types that show them. Reading
    // them unconditionally would overwrite a field the UI never displayed —
    // a screenshot step's `region`, for instance, would be cleared the first
    // time any other property was edited.
    const bool tunable = step.type == core::StepType::Click ||
                         step.type == core::StepType::DoubleClick ||
                         step.type == core::StepType::OcrFind ||
                         step.type == core::StepType::ImageFind;
    if (tunable) {
        step.target.offsetX = offsetXSpin_->value();
        step.target.offsetY = offsetYSpin_->value();
        if (regionCheck_->isChecked()) {
            step.target.region = core::Rect{regionXSpin_->value(), regionYSpin_->value(),
                                            regionWSpin_->value(), regionHSpin_->value()};
        } else {
            step.target.region.reset();
        }
        step.target.retry.times = retryTimesSpin_->value();
        step.target.retry.intervalMs = retryIntervalSpin_->value();
    }

    return step;
}

void PropertyPanel::emitEdit() {
    if (loading_ || !hasStep_) return;
    step_ = step();
    emit stepEdited(step_);
}

}  // namespace rpa::studio
