#include "metadata/ExifReader.h"

#include <exiv2/exiv2.hpp>

namespace fxt {

namespace {

// GPS 三分式 "25/1 2/1 60/10" → 十进制度；解析失败返回 false
bool gpsToDecimal(const QString &triplet, double &out)
{
    const QStringList parts = triplet.split(QLatin1Char(' '));
    if (parts.size() != 3)
        return false;
    auto rational = [](const QString &s, double &v) {
        const int slash = s.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            return false;
        bool okNum = false, okDen = false;
        const double num = s.left(slash).toDouble(&okNum);
        const double den = s.mid(slash + 1).toDouble(&okDen);
        v = (okNum && okDen && den != 0) ? num / den : 0.0;
        return okNum && okDen && den != 0;
    };
    double deg = 0, min = 0, sec = 0;
    if (!rational(parts.at(0), deg) || !rational(parts.at(1), min)
        || !rational(parts.at(2), sec))
        return false;
    out = deg + min / 60.0 + sec / 3600.0;
    return true;
}

// GPS 坐标 tag（三分式）转十进制写入 raw；Ref 合并进 tags 链约定
void normalizeGpsCoordinates(RawExif &raw)
{
    struct Coord
    {
        const char *valueTag;
        const char *refTag;
        const char *outTag;
    };
    static const Coord kCoords[] = {
        { "Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef",
          "Exif.GPSInfo.GPSLatitude" },
        { "Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef",
          "Exif.GPSInfo.GPSLongitude" },
    };
    for (const Coord &c : kCoords) {
        const QString value = raw.value(QString::fromLatin1(c.valueTag));
        const QString ref = raw.value(QString::fromLatin1(c.refTag));
        if (value.isEmpty())
            continue;
        double dec = 0;
        if (!gpsToDecimal(value, dec))
            continue;
        // 海拔/经纬基准：S/W 为负（Ref 大小写都兼容）
        const QChar r = !ref.isEmpty() ? ref.at(0) : QChar('N');
        if (r == QLatin1Char('S') || r == QLatin1Char('W'))
            dec = -dec;
        raw.insert(QString::fromLatin1(c.outTag),
                   QString::number(dec, 'f', 6));
    }
}

} // namespace

ExifReader::ExifReader(const QString &filePath)
    : m_filePath(filePath)
{
}

bool ExifReader::read()
{
    m_raw.clear();
    m_error.clear();
    try {
        // 0.28：open 返回 UniquePtr；异常路径统一 Exiv2::Error（std::exception）
        Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(
            m_filePath.toStdString());
        if (image.get() == nullptr) {
            m_error = QStringLiteral("exiv2 无法识别文件：%1").arg(m_filePath);
            return false;
        }
        image->readMetadata();
        const Exiv2::ExifData &exif = image->exifData();
        for (const Exiv2::Exifdatum &datum : exif) {
            const QString key = QString::fromStdString(datum.key());
            QString value = QString::fromStdString(datum.value().toString());
            // 长 MakerNote 二进制块截断（防 UI/日志巨型串；展示无意义）
            if (value.size() > 256)
                value = value.left(256);
            m_raw.insert(key, value);
        }
        normalizeGpsCoordinates(m_raw);
        return true;
    } catch (const Exiv2::Error &e) {
        m_error = QStringLiteral("Exif 读取失败（exiv2）：%1")
                      .arg(QString::fromUtf8(e.what()));
        return false;
    } catch (const std::exception &e) {
        m_error = QStringLiteral("Exif 读取失败：%1").arg(QString::fromUtf8(e.what()));
        return false;
    }
}

QSize ExifReader::pixelSize() const
{
    const int w = m_raw.value(QStringLiteral("Exif.Image.ImageWidth")).toInt();
    const int h = m_raw.value(QStringLiteral("Exif.Image.ImageLength")).toInt();
    if (w > 0 && h > 0)
        return QSize(w, h);
    return QSize(0, 0);   // 未知（tag 缺失，普通 JPEG 常见）
}

} // namespace fxt
