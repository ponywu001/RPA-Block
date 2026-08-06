#pragma once

#include <QColor>
#include <QObject>
#include <QString>

namespace rpa::studio {

enum class ThemeMode {
    System,  ///< Follow the OS, and keep following it when the user changes it.
    Light,
    Dark,
};

QString themeModeToString(ThemeMode mode);
ThemeMode themeModeFromString(const QString& value);

/// Semantic colours, so widgets ask for a role rather than a hex value and both
/// themes stay consistent. Adding a literal colour to a widget is what made the
/// first version impossible to give a dark mode.
struct ThemeColors {
    bool dark = false;

    // Surfaces, from furthest back to nearest front.
    QColor window;      ///< Behind docks and toolbars.
    QColor surface;     ///< Panels, docks, list backgrounds.
    QColor surfaceAlt;  ///< Inputs, and rows that need to separate from surface.
    QColor canvas;      ///< The block canvas itself.
    QColor canvasDot;   ///< Its alignment dots.

    // Ink.
    QColor text;
    QColor textMuted;
    QColor textOnAccent;
    QColor textOnBlock;  ///< On a saturated block fill; near-white in both themes.

    // Lines.
    QColor border;   ///< Input outlines, dock separators.
    QColor divider;  ///< Weaker: inside lists and groups.

    // Interaction.
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor hover;      ///< Translucent wash for hovered rows and tool buttons.
    QColor selection;  ///< Selected block halo and list selection fill.

    // Status.
    QColor success;
    QColor warning;
    QColor danger;
    QColor info;

    // Canvas details.
    QColor dropIndicator;
    QColor breakpoint;
    QColor disabledBlock;
    QColor disabledBlockEdge;
};

/// Owns the active theme, applies it to the application, and tells widgets when
/// it changed. A singleton because the palette and the global stylesheet are
/// themselves application-wide state.
class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    /// Apply `mode` to the whole application: palette, stylesheet, and font.
    /// Safe to call repeatedly; emits changed() when the effective theme moved.
    void setMode(ThemeMode mode);
    ThemeMode mode() const;

    /// The theme actually in force, with System already resolved.
    bool isDark() const;
    const ThemeColors& colors() const;

signals:
    /// Widgets that paint themselves should connect this to update().
    void changed();

private:
    ThemeManager();

    void apply();
    /// Ask the OS. Falls back to light when the platform does not say.
    bool systemPrefersDark() const;

    ThemeMode mode_ = ThemeMode::System;
    ThemeColors colors_;
};

/// Shorthand for ThemeManager::instance().colors().
const ThemeColors& theme();

}  // namespace rpa::studio
