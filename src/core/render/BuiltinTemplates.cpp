#include "render/BuiltinTemplates.h"

namespace fxt::BuiltinTemplates {

using namespace FieldKeys;

namespace {
// 相对字号（= 比例 × 画布宽；黄金图校准点）
constexpr qreal kFontScaleBrand = 0.028;
constexpr qreal kFontScaleParams = 0.024;
constexpr qreal kFontScaleSecondary = 0.020;
constexpr qreal kLogoScale = 0.5;   // Logo 高 = 0.5 × 行高
// 分隔线浅灰（对齐原版视觉；黑字模板与白字模板各一档）
const QColor kDividerDark = QColor(QStringLiteral("#C8C8C8"));
const QColor kDividerLight = QColor(255, 255, 255, 190);
} // namespace

QList<LayoutRow> defaultRows(const QList<Element> &elements)
{
    const auto findById = [&elements](const char *key) -> QUuid {
        const QString fk = QString::fromLatin1(key);
        for (const Element &e : elements)
            if (e.fieldKey == fk)
                return e.id;
        return QUuid();
    };

    LayoutRow row0;
    row0.index = 0;
    row0.heightRatio = 0.6;
    row0.elementIds = { findById(kMake), findById(kBrandLogo), findById(kDivider),
                        findById(kParamString) };
    row0.elementIds.removeAll(QUuid());

    LayoutRow row1;
    row1.index = 1;
    row1.heightRatio = 0.4;
    row1.elementIds = { findById(kLens), findById(kDateTime) };
    row1.elementIds.removeAll(QUuid());

    QList<LayoutRow> rows;
    if (!row0.elementIds.isEmpty())
        rows.append(row0);
    if (!row1.elementIds.isEmpty())
        rows.append(row1);
    return rows;
}

BuiltinSpec defaultSpec(const QString &templateId)
{
    BuiltinSpec spec;

    // blur 模板在模糊背景上直接绘白字（原版 createImageWithTextForBlurBackgroud
    // 同源）；bottom/around 黑字压白底/白边
    const bool onBlur = (templateId == QLatin1String("blur"));
    const QColor textColor = onBlur ? QColor(Qt::white) : QColor(Qt::black);

    Element make;
    make.id = QUuid::createUuid();
    make.kind = ElementKind::Text;
    make.fieldKey = QString::fromLatin1(kMake);
    make.style.fontScale = kFontScaleBrand;
    make.style.bold = true;
    make.style.color = textColor;

    Element logo;
    logo.id = QUuid::createUuid();
    logo.kind = ElementKind::Logo;
    logo.fieldKey = QString::fromLatin1(kBrandLogo);
    logo.logoScale = kLogoScale;

    Element divider;
    divider.id = QUuid::createUuid();
    divider.kind = ElementKind::Divider;
    divider.fieldKey = QString::fromLatin1(kDivider);   // 非 Exif 字段，行组定位用
    divider.style.color = onBlur ? kDividerLight : kDividerDark;

    Element params;
    params.id = QUuid::createUuid();
    params.kind = ElementKind::Text;
    params.fieldKey = QString::fromLatin1(kParamString);
    params.style.fontScale = kFontScaleParams;
    params.style.color = textColor;

    Element lens;
    lens.id = QUuid::createUuid();
    lens.kind = ElementKind::Text;
    lens.fieldKey = QString::fromLatin1(kLens);
    lens.style.fontScale = kFontScaleSecondary;
    lens.style.color = textColor;

    Element dateTime;
    dateTime.id = QUuid::createUuid();
    dateTime.kind = ElementKind::Text;
    dateTime.fieldKey = QString::fromLatin1(kDateTime);
    dateTime.style.fontScale = kFontScaleSecondary;
    dateTime.style.color = textColor;

    spec.elements = { make, logo, divider, params, lens, dateTime };

    LayoutRow row0;
    row0.index = 0;
    row0.heightRatio = 0.6;
    row0.elementIds = { make.id, logo.id, divider.id, params.id };

    LayoutRow row1;
    row1.index = 1;
    row1.heightRatio = 0.4;
    row1.elementIds = { lens.id, dateTime.id };

    spec.rows = { row0, row1 };
    return spec;
}

} // namespace fxt::BuiltinTemplates
