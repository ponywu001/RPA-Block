#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QHash>
#include <QPalette>
#include <QStyleHints>

namespace rpa::studio {

namespace {

/// The block palette already spends seven saturated hues, so the chrome stays
/// near-neutral and the accent avoids those hues: a deep indigo, which reads as
/// application furniture rather than as another category next to the azure
/// "mouse" blocks.
ThemeColors lightColors() {
    ThemeColors c;
    c.dark = false;

    c.window = QColor(0xE9, 0xEC, 0xF1);
    c.surface = QColor(0xF7, 0xF8, 0xFA);
    c.surfaceAlt = QColor(0xFF, 0xFF, 0xFF);
    c.canvas = QColor(0xF1, 0xF3, 0xF7);
    c.canvasDot = QColor(0xD5, 0xDB, 0xE4);

    c.text = QColor(0x1B, 0x23, 0x30);
    c.textMuted = QColor(0x5C, 0x68, 0x79);
    c.textOnAccent = QColor(0xFF, 0xFF, 0xFF);
    c.textOnBlock = QColor(0xFF, 0xFF, 0xFF);

    c.border = QColor(0xD1, 0xD8, 0xE2);
    c.divider = QColor(0xE4, 0xE8, 0xEE);

    c.accent = QColor(0x4C, 0x5F, 0xD5);
    c.accentHover = QColor(0x3F, 0x51, 0xC7);
    c.accentPressed = QColor(0x36, 0x46, 0xB0);
    c.hover = QColor(0x1B, 0x23, 0x30, 0x14);
    c.selection = QColor(0x4C, 0x5F, 0xD5, 0x33);

    c.success = QColor(0x1E, 0x8E, 0x3E);
    c.warning = QColor(0xB3, 0x6A, 0x00);
    c.danger = QColor(0xC5, 0x35, 0x2B);
    c.info = QColor(0x1A, 0x6F, 0xB5);

    c.dropIndicator = QColor(0x4C, 0x5F, 0xD5);
    c.breakpoint = QColor(0xE8, 0xA3, 0x1C);
    c.disabledBlock = QColor(0xC6, 0xCB, 0xD4);
    c.disabledBlockEdge = QColor(0xA8, 0xAE, 0xB9);
    return c;
}

ThemeColors darkColors() {
    ThemeColors c;
    c.dark = true;

    c.window = QColor(0x16, 0x19, 0x1F);
    c.surface = QColor(0x1D, 0x21, 0x29);
    c.surfaceAlt = QColor(0x25, 0x2A, 0x34);
    c.canvas = QColor(0x13, 0x16, 0x1B);
    c.canvasDot = QColor(0x2C, 0x32, 0x3C);

    c.text = QColor(0xE6, 0xE9, 0xEF);
    c.textMuted = QColor(0x99, 0xA3, 0xB3);
    c.textOnAccent = QColor(0xFF, 0xFF, 0xFF);
    c.textOnBlock = QColor(0xFF, 0xFF, 0xFF);

    c.border = QColor(0x33, 0x3A, 0x45);
    c.divider = QColor(0x27, 0x2D, 0x36);

    c.accent = QColor(0x7A, 0x8C, 0xF0);
    c.accentHover = QColor(0x8D, 0x9C, 0xF4);
    c.accentPressed = QColor(0x65, 0x77, 0xE0);
    c.hover = QColor(0xFF, 0xFF, 0xFF, 0x12);
    c.selection = QColor(0x7A, 0x8C, 0xF0, 0x3D);

    c.success = QColor(0x4C, 0xC2, 0x6A);
    c.warning = QColor(0xE0, 0xA3, 0x3C);
    c.danger = QColor(0xF0, 0x65, 0x5A);
    c.info = QColor(0x5A, 0xA9, 0xE6);

    c.dropIndicator = QColor(0x8D, 0x9C, 0xF4);
    c.breakpoint = QColor(0xE8, 0xA3, 0x1C);
    c.disabledBlock = QColor(0x3A, 0x41, 0x4D);
    c.disabledBlockEdge = QColor(0x4B, 0x53, 0x61);
    return c;
}

QString hex(const QColor& color) {
    return color.name(QColor::HexRgb);
}

/// `rgba(...)` rather than #AARRGGBB: Qt stylesheets read the hex form as
/// #RRGGBBAA and silently produce the wrong colour for translucent values.
QString rgba(const QColor& color) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alphaF(), 0, 'f', 3);
}

QPalette buildPalette(const ThemeColors& c) {
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.surfaceAlt);
    p.setColor(QPalette::AlternateBase, c.surface);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::PlaceholderText, c.textMuted);
    p.setColor(QPalette::Button, c.surface);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, c.danger);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.textOnAccent);
    p.setColor(QPalette::ToolTipBase, c.surfaceAlt);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border);
    p.setColor(QPalette::Light, c.surfaceAlt);

    // Disabled text has to be dimmer than muted or a greyed control is
    // indistinguishable from an enabled one.
    QColor disabled = c.textMuted;
    disabled.setAlpha(0x80);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    return p;
}

/// The chrome. Kept as one template with ${token} placeholders rather than a
/// chain of .arg() calls, which at this size stops being readable and starts
/// silently shifting when a placeholder is added in the middle.
const char* kStyleTemplate = R"QSS(
QWidget {
    color: ${text};
}
QMainWindow, QDialog {
    background: ${window};
}
QToolTip {
    background: ${surfaceAlt};
    color: ${text};
    border: 1px solid ${border};
    border-radius: 6px;
    padding: 5px 8px;
}

/* --- Menu bar and menus ------------------------------------------------- */
QMenuBar {
    background: ${window};
    border: none;
    padding: 3px 4px;
}
QMenuBar::item {
    padding: 5px 10px;
    border-radius: 6px;
    background: transparent;
}
QMenuBar::item:selected {
    background: ${hover};
}
QMenu {
    background: ${surfaceAlt};
    border: 1px solid ${border};
    border-radius: 8px;
    padding: 5px;
}
QMenu::item {
    padding: 6px 22px 6px 12px;
    border-radius: 5px;
}
QMenu::item:selected {
    background: ${accent};
    color: ${textOnAccent};
}
QMenu::item:disabled {
    color: ${textMuted};
}
QMenu::separator {
    height: 1px;
    background: ${divider};
    margin: 5px 8px;
}

/* --- Toolbar ------------------------------------------------------------ */
QToolBar {
    background: ${window};
    border: none;
    border-bottom: 1px solid ${divider};
    padding: 5px 6px;
    spacing: 3px;
}
QToolBar::separator {
    width: 1px;
    background: ${divider};
    margin: 5px 6px;
}
QToolButton {
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 5px 9px;
    color: ${text};
}
QToolButton:hover {
    background: ${hover};
}
QToolButton:pressed, QToolButton:checked {
    background: ${selection};
}
QToolButton:disabled {
    color: ${textMuted};
}

/* --- Docks -------------------------------------------------------------- */
/* The close and float buttons keep the style's own icons. Setting them to `none`
   does not remove the buttons -- it only leaves them unable to resolve an image,
   so each dock ends up with an invisible but fully clickable close button. A
   stray click then removes a panel with nothing on screen to explain where it
   went. */
QDockWidget {
    color: ${text};
}
QDockWidget::title {
    background: ${window};
    padding: 7px 10px;
    border-bottom: 1px solid ${divider};
    text-align: left;
    font-weight: 600;
}
QDockWidget > QWidget {
    background: ${surface};
}

/* --- Buttons ------------------------------------------------------------ */
QPushButton {
    background: ${surfaceAlt};
    color: ${text};
    border: 1px solid ${border};
    border-radius: 7px;
    padding: 6px 14px;
    min-height: 18px;
}
QPushButton:hover {
    border-color: ${accent};
}
QPushButton:pressed {
    background: ${hover};
}
QPushButton:disabled {
    color: ${textMuted};
    border-color: ${divider};
}
QPushButton:default {
    background: ${accent};
    color: ${textOnAccent};
    border-color: ${accent};
    font-weight: 600;
}
QPushButton:default:hover {
    background: ${accentHover};
    border-color: ${accentHover};
}
QPushButton:default:pressed {
    background: ${accentPressed};
}
QPushButton:default:disabled {
    background: ${divider};
    color: ${textMuted};
    border-color: ${divider};
}

/* --- Text entry --------------------------------------------------------- */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: ${surfaceAlt};
    color: ${text};
    border: 1px solid ${border};
    border-radius: 7px;
    padding: 5px 8px;
    selection-background-color: ${accent};
    selection-color: ${textOnAccent};
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus,
QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: ${accent};
}
QLineEdit:disabled, QPlainTextEdit:disabled, QSpinBox:disabled,
QDoubleSpinBox:disabled, QComboBox:disabled {
    background: ${surface};
    color: ${textMuted};
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox QAbstractItemView {
    background: ${surfaceAlt};
    border: 1px solid ${border};
    border-radius: 6px;
    padding: 3px;
    selection-background-color: ${accent};
    selection-color: ${textOnAccent};
}
/* Arrows are left to the style; only the button chrome is restyled, because a
   stylesheet that claims the sub-control without supplying an image loses the
   glyph entirely. */
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    background: transparent;
    border: none;
    width: 16px;
}

/* --- Lists and trees ---------------------------------------------------- */
QTreeWidget, QListWidget, QTreeView, QListView {
    background: ${surface};
    border: 1px solid ${border};
    border-radius: 8px;
    outline: none;
    padding: 3px;
}
QTreeWidget::item, QListWidget::item {
    padding: 4px 6px;
    border-radius: 5px;
}
QTreeWidget::item:hover, QListWidget::item:hover {
    background: ${hover};
}
QTreeWidget::item:selected, QListWidget::item:selected {
    background: ${accent};
    color: ${textOnAccent};
}
QHeaderView::section {
    background: ${window};
    color: ${textMuted};
    border: none;
    border-bottom: 1px solid ${divider};
    padding: 5px 7px;
    font-weight: 600;
}

/* --- Groups and tabs ---------------------------------------------------- */
QGroupBox {
    border: 1px solid ${border};
    border-radius: 8px;
    margin-top: 12px;
    padding: 10px 8px 6px 8px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0 5px;
    color: ${textMuted};
}
QTabWidget::pane {
    background: ${surface};
    border: 1px solid ${border};
    border-radius: 8px;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    color: ${textMuted};
    border: none;
    padding: 7px 14px;
    margin-right: 2px;
    border-radius: 6px;
}
QTabBar::tab:hover {
    background: ${hover};
}
QTabBar::tab:selected {
    background: ${surface};
    color: ${text};
    font-weight: 600;
}

/* --- Scrollbars --------------------------------------------------------- */
QScrollBar:vertical {
    background: transparent;
    width: 11px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: ${scrollHandle};
    border-radius: 5px;
    min-height: 28px;
}
QScrollBar::handle:vertical:hover {
    background: ${scrollHandleHover};
}
QScrollBar:horizontal {
    background: transparent;
    height: 11px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: ${scrollHandle};
    border-radius: 5px;
    min-width: 28px;
}
QScrollBar::handle:horizontal:hover {
    background: ${scrollHandleHover};
}
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0;
    height: 0;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: transparent;
}

/* --- Checks and radios -------------------------------------------------- */
QCheckBox, QRadioButton {
    spacing: 7px;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 15px;
    height: 15px;
    border: 1px solid ${border};
    background: ${surfaceAlt};
}
QCheckBox::indicator {
    border-radius: 4px;
}
QRadioButton::indicator {
    border-radius: 8px;
}
QCheckBox::indicator:hover, QRadioButton::indicator:hover {
    border-color: ${accent};
}
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: ${accent};
    border-color: ${accent};
}

/* --- Misc --------------------------------------------------------------- */
QStatusBar {
    background: ${window};
    border-top: 1px solid ${divider};
    color: ${textMuted};
}
QStatusBar::item {
    border: none;
}
QSplitter::handle {
    background: ${divider};
}
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QScrollArea {
    background: transparent;
    border: none;
}
QLabel {
    background: transparent;
}
)QSS";

QString buildStyleSheet(const ThemeColors& c) {
    QHash<QString, QString> tokens;
    tokens[QStringLiteral("window")] = hex(c.window);
    tokens[QStringLiteral("surface")] = hex(c.surface);
    tokens[QStringLiteral("surfaceAlt")] = hex(c.surfaceAlt);
    tokens[QStringLiteral("text")] = hex(c.text);
    tokens[QStringLiteral("textMuted")] = hex(c.textMuted);
    tokens[QStringLiteral("textOnAccent")] = hex(c.textOnAccent);
    tokens[QStringLiteral("border")] = hex(c.border);
    tokens[QStringLiteral("divider")] = hex(c.divider);
    tokens[QStringLiteral("accent")] = hex(c.accent);
    tokens[QStringLiteral("accentHover")] = hex(c.accentHover);
    tokens[QStringLiteral("accentPressed")] = hex(c.accentPressed);
    tokens[QStringLiteral("hover")] = rgba(c.hover);
    tokens[QStringLiteral("selection")] = rgba(c.selection);

    QColor handle = c.dark ? QColor(0xFF, 0xFF, 0xFF, 0x2E) : QColor(0x1B, 0x23, 0x30, 0x33);
    QColor handleHover = c.dark ? QColor(0xFF, 0xFF, 0xFF, 0x4D) : QColor(0x1B, 0x23, 0x30, 0x5C);
    tokens[QStringLiteral("scrollHandle")] = rgba(handle);
    tokens[QStringLiteral("scrollHandleHover")] = rgba(handleHover);

    QString sheet = QString::fromUtf8(kStyleTemplate);
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        sheet.replace(QStringLiteral("${") + it.key() + QStringLiteral("}"), it.value());
    }
    return sheet;
}

}  // namespace

QString themeModeToString(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light: return QStringLiteral("light");
        case ThemeMode::Dark: return QStringLiteral("dark");
        case ThemeMode::System: break;
    }
    return QStringLiteral("system");
}

ThemeMode themeModeFromString(const QString& value) {
    if (value.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) return ThemeMode::Light;
    if (value.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) return ThemeMode::Dark;
    return ThemeMode::System;
}

ThemeManager& ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager() : colors_(lightColors()) {
    // Keep following the OS while the mode is System, so switching Windows to
    // dark repaints the app instead of leaving it the only light window open.
    if (QStyleHints* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (mode_ == ThemeMode::System) apply();
        });
    }
}

bool ThemeManager::systemPrefersDark() const {
    const QStyleHints* hints = QGuiApplication::styleHints();
    return hints && hints->colorScheme() == Qt::ColorScheme::Dark;
}

void ThemeManager::setMode(ThemeMode mode) {
    mode_ = mode;
    apply();
}

ThemeMode ThemeManager::mode() const {
    return mode_;
}

bool ThemeManager::isDark() const {
    return colors_.dark;
}

const ThemeColors& ThemeManager::colors() const {
    return colors_;
}

void ThemeManager::apply() {
    const bool wantDark = mode_ == ThemeMode::Dark ||
                          (mode_ == ThemeMode::System && systemPrefersDark());
    colors_ = wantDark ? darkColors() : lightColors();

    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        app->setPalette(buildPalette(colors_));
        app->setStyleSheet(buildStyleSheet(colors_));
    }

    emit changed();
}

const ThemeColors& theme() {
    return ThemeManager::instance().colors();
}

}  // namespace rpa::studio
