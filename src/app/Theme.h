#pragma once

#include <QColor>

namespace fxt::Theme {

// 色板常量（04-UI设计.md §1.1；QSS 管控件样式，代码内自绘/选中态取色走这里）
inline const QColor kWindowBg{0xF5, 0xF6, 0xF8};
inline const QColor kCard{0xFF, 0xFF, 0xFF};
inline const QColor kCardBorder{0xE4, 0xE7, 0xEC};
inline const QColor kSeparator{0xEE, 0xF0, 0xF3};
inline const QColor kTextPrimary{0x1F, 0x23, 0x29};
inline const QColor kTextSecondary{0x5A, 0x60, 0x68};
inline const QColor kTextMuted{0x9A, 0xA0, 0xA8};
inline const QColor kTextDisabled{0xC3, 0xC8, 0xCF};
inline const QColor kAccent{0x6C, 0x5C, 0xE7};
inline const QColor kAccentHover{0x5B, 0x4B, 0xD5};
inline const QColor kAccentLight{0xEF, 0xEB, 0xFD};
inline const QColor kAccentBorder{0xB9, 0xAC, 0xF5};
inline const QColor kDanger{0xE5, 0x48, 0x4D};
inline const QColor kSuccess{0x2F, 0xA3, 0x6B};
inline const QColor kWarn{0xE8, 0xA1, 0x3C};
inline const QColor kInputBorder{0xD8, 0xDC, 0xE2};

// 几何（§1.3）：全局圆角 5px 系；8px 间距网格
inline constexpr int kRadius = 5;
inline constexpr int kTitleBarHeight = 40;
inline constexpr int kPanelTitleHeight = 32;
inline constexpr int kButtonHeight = 28;
inline constexpr int kPrimaryButtonHeight = 32;
inline constexpr int kRowHeight = 28;
inline constexpr int kListRowHeight = 32;
inline constexpr int kToolstripHeight = 36;
inline constexpr int kPhotoListHeight = 175;

} // namespace fxt::Theme
