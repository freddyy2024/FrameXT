#include "render/BuiltinTemplates.h"

namespace fxt::BuiltinTemplates {

using namespace FieldKeys;

namespace {
// 相对字号（= 比例 × 画布宽；黄金图校准点）。布局对齐原版默认样式
// （2026-10 用户附件图：左块机型/镜头、右块 Logo|竖线|参数/日期）
constexpr qreal kFontScaleModel = 0.028;      // 机型：最大、粗体
constexpr qreal kFontScaleParams = 0.021;     // 参数串：粗体
constexpr qreal kFontScaleSecondary = 0.020;  // 镜头/日期：常规
constexpr qreal kLogoScale = 0.62;   // Logo 高 = 0.62 × 信息带高（跨两行居中）
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
    row0.heightRatio = 0.5;
    row0.elementIds = { findById(kModel) };
    row0.rightIds = { findById(kBrandLogo), findById(kDivider),
                      findById(kParamString) };
    row0.elementIds.removeAll(QUuid());
    row0.rightIds.removeAll(QUuid());

    LayoutRow row1;
    row1.index = 1;
    row1.heightRatio = 0.5;
    row1.elementIds = { findById(kLens) };
    row1.rightIds = { findById(kDateTime) };
    row1.elementIds.removeAll(QUuid());
    row1.rightIds.removeAll(QUuid());

    QList<LayoutRow> rows;
    if (!row0.elementIds.isEmpty() || !row0.rightIds.isEmpty())
        rows.append(row0);
    if (!row1.elementIds.isEmpty() || !row1.rightIds.isEmpty())
        rows.append(row1);
    return rows;
}

BuiltinSpec defaultSpec(const QString &templateId)
{
    BuiltinSpec spec;

    // blur 模板在模糊背景上直接绘白字（原版 createImageWithTextForBlurBackgroud
    // 同源）；bottom/around 黑字压白底/白边。
    // 默认布局对齐原版（用户附件图）：左块 = 机型（粗体大字）/ 镜头，
    // 右块 = Logo | 竖线 | 参数串（粗体）/ 日期（常规），Logo 跨两行居中
    const bool onBlur = (templateId == QLatin1String("blur"));
    const QColor textColor = onBlur ? QColor(Qt::white) : QColor(Qt::black);

    Element model;
    model.id = QUuid::createUuid();
    model.kind = ElementKind::Text;
    model.fieldKey = QString::fromLatin1(kModel);
    model.style.fontScale = kFontScaleModel;
    model.style.bold = true;
    model.style.color = textColor;

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
    params.style.bold = true;
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
    // 日期为次级信息，颜色浅一档（用户指令）：黑字带用次级灰，
    // 白字带（blur）用半透明白
    dateTime.style.color = onBlur ? QColor(255, 255, 255, 200)
                                  : QColor(QStringLiteral("#5A6068"));

    spec.elements = { model, logo, divider, params, lens, dateTime };

    LayoutRow row0;
    row0.index = 0;
    row0.heightRatio = 0.5;
    row0.elementIds = { model.id };
    row0.rightIds = { logo.id, divider.id, params.id };

    LayoutRow row1;
    row1.index = 1;
    row1.heightRatio = 0.5;
    row1.elementIds = { lens.id };
    row1.rightIds = { dateTime.id };

    spec.rows = { row0, row1 };
    return spec;
}

} // namespace fxt::BuiltinTemplates
