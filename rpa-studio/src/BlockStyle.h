#pragma once

#include <QColor>
#include <QRect>
#include <QString>

#include "rpa/core/Script.h"

class QPainter;

namespace rpa::studio {

/// Block categories, in the Scratch sense: each gets one colour so the shape of
/// a flow is readable at a glance without reading any text.
enum class BlockCategory {
    Mouse,    ///< 滑鼠
    Keyboard, ///< 鍵盤
    Timing,   ///< 等待
    Vision,   ///< 找畫面
    Window,   ///< 開啟程式、視窗與截圖
    Control,  ///< 流程控制
    Network,  ///< 網路
};

BlockCategory categoryOf(core::StepType type);

QString categoryName(BlockCategory category);
QColor categoryColor(BlockCategory category);
/// Slightly darker edge, used for the block outline and the C-block spine.
QColor categoryEdge(BlockCategory category);

QColor colorOf(core::StepType type);
QColor edgeOf(core::StepType type);

/// Text and icon colour that stays legible on `fill`.
///
/// Not a fixed white. Measured against WCAG relative luminance, white on the
/// light theme's block fills fails on six of the seven categories — 1.89:1 on the
/// yellow-orange "等待", against a 3:1 minimum for large text — so the labels were
/// washing out on exactly the blocks meant to stand out. Choosing the ink per fill
/// keeps the vivid palette, which is the entire point of colour-coded blocks,
/// while making every label readable.
QColor inkOn(const QColor& fill);

/// `inkOn(fill)` at a reduced opacity, for the secondary text on a block.
QColor inkOn(const QColor& fill, int alpha);

/// Short Traditional Chinese name shown on the block.
QString stepLabel(core::StepType type);

/// One-line detail for the block body, e.g. the anchor text or wait duration.
QString stepSummary(const core::Step& step);

/// True for `if` / `loop` — the C-shaped blocks that wrap other blocks.
bool isContainer(core::StepType type);

/// Draw the step's pictogram inside `box`. These are vector-drawn rather than
/// emoji: emoji fall back to a monochrome glyph in this font stack, and vectors
/// stay crisp at any DPI.
void paintStepIcon(QPainter& painter, const QRect& box, core::StepType type, const QColor& ink);

}  // namespace rpa::studio
