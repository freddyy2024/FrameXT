#include "ModelIo.h"

#include <QJsonArray>
#include <QJsonValue>

namespace fxt::ModelIo {

namespace {

// 颜色按 RGBA 分量存（显式整数，避免字符串解析的格式怪癖）
QJsonObject colorToJson(const QColor &color)
{
    QJsonObject o;
    o["red"] = color.red();
    o["green"] = color.green();
    o["blue"] = color.blue();
    o["alpha"] = color.alpha();
    return o;
}

QColor colorFromJson(const QJsonObject &o, const QColor &fallback)
{
    if (!o.contains("red"))
        return fallback;
    return QColor(o.value("red").toInt(0), o.value("green").toInt(0),
                  o.value("blue").toInt(0), o.value("alpha").toInt(255));
}

QString uuidToString(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

} // namespace

QJsonObject toJson(const Element &e)
{
    QJsonObject o;
    o["id"] = uuidToString(e.id);
    o["kind"] = static_cast<int>(e.kind);
    o["fieldKey"] = e.fieldKey;
    o["literal"] = e.literal;
    o["anchor"] = static_cast<int>(e.anchor);
    o["offsetX"] = e.offset.x();
    o["offsetY"] = e.offset.y();
    QJsonObject s;
    s["fontFamily"] = e.style.fontFamily;
    s["fontScale"] = e.style.fontScale;
    s["bold"] = e.style.bold;
    s["color"] = colorToJson(e.style.color);
    s["opacity"] = e.style.opacity;
    o["style"] = s;
    o["logoScale"] = e.logoScale;
    o["visible"] = e.visible;
    o["locked"] = e.locked;
    o["hasCustomPosition"] = e.hasCustomPosition;
    return o;
}

Element elementFromJson(const QJsonObject &o)
{
    Element e;  // 缺字段保持默认值（schema 演进向后兼容）
    e.id = QUuid(o.value("id").toString());
    if (e.id.isNull())
        e.id = QUuid::createUuid();
    e.kind = static_cast<ElementKind>(o.value("kind").toInt(int(ElementKind::Text)));
    e.fieldKey = o.value("fieldKey").toString();
    e.literal = o.value("literal").toString();
    e.anchor = static_cast<Anchor>(o.value("anchor").toInt(int(Anchor::BottomLeft)));
    e.offset = QPointF(o.value("offsetX").toDouble(0.0),
                       o.value("offsetY").toDouble(0.0));
    const QJsonObject s = o.value("style").toObject();
    if (!s.isEmpty()) {
        e.style.fontFamily = s.value("fontFamily").toString(e.style.fontFamily);
        e.style.fontScale = s.value("fontScale").toDouble(e.style.fontScale);
        e.style.bold = s.value("bold").toBool(e.style.bold);
        e.style.color = colorFromJson(s.value("color").toObject(), e.style.color);
        e.style.opacity = s.value("opacity").toDouble(e.style.opacity);
    }
    e.logoScale = o.value("logoScale").toDouble(e.logoScale);
    e.visible = o.value("visible").toBool(e.visible);
    e.locked = o.value("locked").toBool(e.locked);
    e.hasCustomPosition = o.value("hasCustomPosition").toBool(e.hasCustomPosition);
    return e;
}

QJsonObject toJson(const TemplateState &t)
{
    QJsonObject o;
    o["templateId"] = t.templateId;
    o["params"] = QJsonObject::fromVariantMap(t.templateParams);
    QJsonArray arr;
    for (const Element &e : t.elements)
        arr.append(toJson(e));
    o["elements"] = arr;
    return o;
}

TemplateState templateStateFromJson(const QJsonObject &o)
{
    TemplateState t;
    t.templateId = o.value("templateId").toString();
    t.templateParams = o.value("params").toObject().toVariantMap();
    const QJsonArray arr = o.value("elements").toArray();
    t.elements.reserve(arr.size());
    for (const QJsonValue &v : arr)
        t.elements.append(elementFromJson(v.toObject()));
    return t;
}

QJsonObject toJson(const PhotoDocument &d)
{
    QJsonObject o;
    o["id"] = uuidToString(d.id);
    o["sourcePath"] = d.sourcePath;
    QJsonObject m;
    m["pixelW"] = d.meta.pixelSize.width();
    m["pixelH"] = d.meta.pixelSize.height();
    m["format"] = d.meta.format;
    m["bitDepth"] = d.meta.bitDepth;
    m["dpiW"] = d.meta.dpi.width();
    m["dpiH"] = d.meta.dpi.height();
    m["make"] = d.meta.exif.make;
    m["model"] = d.meta.exif.model;
    m["hasGps"] = d.meta.hasGps;
    m["filePath"] = d.meta.filePath;
    // iccProfile 为大二进制，不入 JSON（导出走 M2 二进制通道）
    o["meta"] = m;
    o["templateState"] = toJson(d.templateState);
    QJsonObject ov;
    for (auto it = d.overrides.values.cbegin(); it != d.overrides.values.cend(); ++it)
        ov[it.key()] = it.value();
    o["overrides"] = ov;
    return o;
}

PhotoDocument photoDocumentFromJson(const QJsonObject &o)
{
    PhotoDocument d;
    d.id = QUuid(o.value("id").toString());
    if (d.id.isNull())
        d.id = QUuid::createUuid();
    d.sourcePath = o.value("sourcePath").toString();

    const QJsonObject m = o.value("meta").toObject();
    d.meta.pixelSize = QSize(m.value("pixelW").toInt(0), m.value("pixelH").toInt(0));
    d.meta.format = m.value("format").toString();
    d.meta.bitDepth = m.value("bitDepth").toInt(8);
    d.meta.dpi = QSizeF(m.value("dpiW").toDouble(0.0), m.value("dpiH").toDouble(0.0));
    d.meta.exif.make = m.value("make").toString();
    d.meta.exif.model = m.value("model").toString();
    d.meta.hasGps = m.value("hasGps").toBool(false);
    d.meta.filePath = m.value("filePath").toString();

    d.templateState = templateStateFromJson(o.value("templateState").toObject());

    const QJsonObject ov = o.value("overrides").toObject();
    for (auto it = ov.constBegin(); it != ov.constEnd(); ++it)
        d.overrides.values.insert(it.key(), it.value().toString());
    return d;
}

} // namespace fxt::ModelIo
