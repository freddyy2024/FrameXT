#include "metadata/FieldRegistry.h"

#include "metadata/ExifFormatter.h"

namespace fxt {

namespace {

// 原始串 → double（"/" 分数支持；解析失败返回 false）
bool toDouble(const QString &raw, double &out)
{
    if (raw.isEmpty())
        return false;
    const int slash = raw.indexOf(QLatin1Char('/'));
    if (slash > 0) {
        bool okNum = false, okDen = false;
        const double num = raw.left(slash).toDouble(&okNum);
        const double den = raw.mid(slash + 1).toDouble(&okDen);
        if (okNum && okDen && den != 0) {
            out = num / den;
            return true;
        }
        return false;
    }
    bool ok = false;
    out = raw.toDouble(&ok);
    return ok;
}

QString formatBy(const FieldDef &def, const RawExif &raw)
{
    // 取值链回退：首个非空 tag 的原始值
    QString value;
    for (const QString &tag : def.tags) {
        value = raw.value(tag);
        if (!value.isEmpty())
            break;
    }
    if (value.isEmpty())
        return QString();

    double d = 0;
    switch (def.format) {
    case FieldFormat::Raw:
        return value;
    case FieldFormat::Shutter:
        return toDouble(value, d) ? ExifFormatter::shutter(d) : QString();
    case FieldFormat::FNumber:
        return toDouble(value, d) ? ExifFormatter::fNumber(d) : QString();
    case FieldFormat::FocalLength:
        return toDouble(value, d) ? ExifFormatter::focalLength(d) : QString();
    case FieldFormat::FocalLengthEq:
        return toDouble(value, d) ? ExifFormatter::focalLengthEq(d) : QString();
    case FieldFormat::Iso:
        return toDouble(value, d) ? ExifFormatter::iso(int(d)) : QString();
    case FieldFormat::ExposureComp:
        return toDouble(value, d) ? ExifFormatter::exposureComp(d) : QString();
    case FieldFormat::MeteringMode:
        return ExifFormatter::meteringMode(int(value.toDouble()));
    case FieldFormat::Flash:
        return ExifFormatter::flash(int(value.toDouble()));
    case FieldFormat::WhiteBalance:
        return ExifFormatter::whiteBalance(int(value.toDouble()));
    case FieldFormat::ExposureProgram:
        return ExifFormatter::exposureProgram(int(value.toDouble()));
    case FieldFormat::SceneCaptureType:
        return ExifFormatter::sceneCaptureType(int(value.toDouble()));
    case FieldFormat::DateTime:
        return ExifFormatter::dateTime(value);
    case FieldFormat::GpsLatitude: {
        // 值形如 "25.0350"（十进制），Ref 从独立 tag 取
        const QString ref = raw.value(def.tags.size() > 1
                                          ? def.tags.at(1)
                                          : QString());
        return toDouble(value, d) ? ExifFormatter::gpsLatitude(d,
                                                               ref.isEmpty()
                                                                   ? QChar('N')
                                                                   : ref.at(0))
                                  : QString();
    }
    case FieldFormat::GpsLongitude: {
        const QString ref = raw.value(def.tags.size() > 1
                                          ? def.tags.at(1)
                                          : QString());
        return toDouble(value, d) ? ExifFormatter::gpsLongitude(d,
                                                                ref.isEmpty()
                                                                    ? QChar('E')
                                                                    : ref.at(0))
                                  : QString();
    }
    case FieldFormat::GpsAltitude: {
        const bool below = raw.value("Exif.GPSInfo.GPSAltitudeRef")
                           == QLatin1String("1");
        return toDouble(value, d) ? ExifFormatter::gpsAltitude(d, below) : QString();
    }
    }
    return value;
}

} // namespace

QList<FieldDef> FieldRegistry::allFields()
{
    // 方案 §7.2 清单的实现映射（Maker 组按 exiv2 厂商前缀注册；
    // 解析失败静默置空 —— §4.5 设计内行为）
    return {
        // —— 基础 ——
        { QStringLiteral("make"), FieldGroup::Basic,
          { QStringLiteral("Exif.Image.Make") }, FieldFormat::Raw, true },
        { QStringLiteral("model"), FieldGroup::Basic,
          { QStringLiteral("Exif.Image.Model") }, FieldFormat::Raw, true },
        { QStringLiteral("software"), FieldGroup::Basic,
          { QStringLiteral("Exif.Image.Software") }, FieldFormat::Raw, false },
        { QStringLiteral("orientation"), FieldGroup::Basic,
          { QStringLiteral("Exif.Image.Orientation") }, FieldFormat::Raw, false },
        // —— 拍摄 ——
        { QStringLiteral("dateTime"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.DateTimeOriginal"),
            QStringLiteral("Exif.Image.DateTime") },
          FieldFormat::DateTime, true },
        { QStringLiteral("iso"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.ISOSpeedRatings"),
            QStringLiteral("Exif.Photo.ISOSpeed") },
          FieldFormat::Iso, false },
        { QStringLiteral("shutter"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.ExposureTime") }, FieldFormat::Shutter,
          false },
        { QStringLiteral("fNumber"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.FNumber") }, FieldFormat::FNumber, false },
        { QStringLiteral("exposureComp"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.ExposureBiasValue") },
          FieldFormat::ExposureComp, false },
        { QStringLiteral("meteringMode"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.MeteringMode") }, FieldFormat::MeteringMode,
          false },
        { QStringLiteral("flash"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.Flash") }, FieldFormat::Flash, false },
        { QStringLiteral("whiteBalance"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.WhiteBalance") }, FieldFormat::WhiteBalance,
          false },
        { QStringLiteral("exposureProgram"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.ExposureProgram") },
          FieldFormat::ExposureProgram, false },
        { QStringLiteral("sceneCaptureType"), FieldGroup::Capture,
          { QStringLiteral("Exif.Photo.SceneCaptureType") },
          FieldFormat::SceneCaptureType, false },
        // —— 镜头 ——
        { QStringLiteral("lensMake"), FieldGroup::Lens,
          { QStringLiteral("Exif.Photo.LensMake") }, FieldFormat::Raw, false },
        { QStringLiteral("lens"), FieldGroup::Lens,
          { QStringLiteral("Exif.Photo.LensModel") }, FieldFormat::Raw, true },
        { QStringLiteral("focalLength"), FieldGroup::Lens,
          { QStringLiteral("Exif.Photo.FocalLength") }, FieldFormat::FocalLength,
          false },
        { QStringLiteral("focalLengthEq"), FieldGroup::Lens,
          { QStringLiteral("Exif.Photo.FocalLengthIn35mmFilm") },
          FieldFormat::FocalLengthEq, false },
        { QStringLiteral("maxAperture"), FieldGroup::Lens,
          { QStringLiteral("Exif.Photo.MaxApertureValue") }, FieldFormat::FNumber,
          false },
        { QStringLiteral("lensSerial"), FieldGroup::Lens,
          { QStringLiteral("Exif.Photo.LensSerialNumber") }, FieldFormat::Raw,
          false },
        // —— 位置 ——
        { QStringLiteral("gpsLatitude"), FieldGroup::Location,
          { QStringLiteral("Exif.GPSInfo.GPSLatitude"),
            QStringLiteral("Exif.GPSInfo.GPSLatitudeRef") },
          FieldFormat::GpsLatitude, false },
        { QStringLiteral("gpsLongitude"), FieldGroup::Location,
          { QStringLiteral("Exif.GPSInfo.GPSLongitude"),
            QStringLiteral("Exif.GPSInfo.GPSLongitudeRef") },
          FieldFormat::GpsLongitude, false },
        { QStringLiteral("gpsAltitude"), FieldGroup::Location,
          { QStringLiteral("Exif.GPSInfo.GPSAltitude") },
          FieldFormat::GpsAltitude, false },
        // —— 品牌特色（MakerNote；各家 key 见 exiv2 厂商组）——
        { QStringLiteral("fujiFilmSimulation"), FieldGroup::Maker,
          { QStringLiteral("Exif.Fujifilm.FilmModel") }, FieldFormat::Raw, false },
        { QStringLiteral("sonyCreativeStyle"), FieldGroup::Maker,
          { QStringLiteral("Exif.Sony2.CreativeStyle") }, FieldFormat::Raw, false },
        { QStringLiteral("nikonPictureControl"), FieldGroup::Maker,
          { QStringLiteral("Exif.Nikon3.PictureControl") }, FieldFormat::Raw,
          false },
        { QStringLiteral("canonPictureStyle"), FieldGroup::Maker,
          { QStringLiteral("Exif.Canon.PictureStyle") }, FieldFormat::Raw, false },
    };
}

const FieldDef *FieldRegistry::find(const QString &key)
{
    static const QHash<QString, FieldDef> index = [] {
        QHash<QString, FieldDef> m;
        for (const FieldDef &def : allFields())
            m.insert(def.key, def);
        return m;
    }();
    const auto it = index.constFind(key);
    return it == index.constEnd() ? nullptr : &it.value();
}

QHash<QString, QString> FieldRegistry::resolveDisplay(const RawExif &raw)
{
    return resolveDisplay(raw, {});
}

QHash<QString, QString> FieldRegistry::resolveDisplay(
    const RawExif &raw, const QHash<QString, QString> &overrides)
{
    QHash<QString, QString> out;
    for (const FieldDef &def : allFields()) {
        // 覆盖层优先（UI 手改；"还原"= 清空覆盖层）
        const auto ov = overrides.constFind(def.key);
        if (ov != overrides.constEnd()) {
            out.insert(def.key, ov.value());
            continue;
        }
        const QString formatted = formatBy(def, raw);
        if (!formatted.isEmpty())
            out.insert(def.key, formatted);
    }
    return out;
}

} // namespace fxt
