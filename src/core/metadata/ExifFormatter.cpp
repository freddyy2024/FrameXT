#include "metadata/ExifFormatter.h"

#include <cmath>

namespace fxt {

namespace {
// 快门分数化：seconds < 1 → 1/x（x 取整并做常见值校正）
QString shutterFraction(double seconds)
{
    if (seconds <= 0 || seconds >= 1)
        return QString();
    const double denom = 1.0 / seconds;
    const qint64 rounded = qRound64(denom);
    if (rounded <= 0)
        return QString();
    // 就近常见快门值校正（相机标称序列，避免 1/159 这类非标值）
    static const qint64 kStandard[] = { 2,    3,    4,    5,    6,    8,    10,
                                        13,   15,   20,   25,   30,   40,   50,
                                        60,   80,   100,  125,  160,  200,  250,
                                        320,  400,  500,  640,  800,  1000, 1250,
                                        1600, 2000, 2500, 3200, 4000, 5000, 6400,
                                        8000 };
    for (const qint64 s : kStandard) {
        if (qAbs(denom - s) / s < 0.05)   // 5% 容差就近吸附
            return QStringLiteral("1/%1").arg(s);
    }
    return QStringLiteral("1/%1").arg(rounded);
}
} // namespace

QString ExifFormatter::shutter(double seconds)
{
    if (seconds <= 0)
        return QString();
    if (seconds < 1.0)
        return shutterFraction(seconds);
    // ≥1s：一位小数去尾零（1.6s / 30s）
    return trimFloat(seconds, 1) + QLatin1Char('s');
}

QString ExifFormatter::fNumber(double value)
{
    if (value <= 0)
        return QString();
    return QStringLiteral("f/%1").arg(trimFloat(value, 1));
}

QString ExifFormatter::focalLength(double mm)
{
    if (mm <= 0)
        return QString();
    return QStringLiteral("%1mm").arg(qRound(mm));
}

QString ExifFormatter::focalLengthEq(double mm)
{
    if (mm <= 0)
        return QString();
    return QStringLiteral("%1mm eq.").arg(qRound(mm));
}

QString ExifFormatter::iso(int value)
{
    if (value <= 0)
        return QString();
    return QStringLiteral("ISO %1").arg(value);
}

QString ExifFormatter::exposureComp(double ev)
{
    // +0.3 EV / -1 EV / 0 EV（± 一位小数）
    if (qFuzzyIsNull(ev))
        return QStringLiteral("0 EV");
    const QString sign = ev > 0 ? QStringLiteral("+") : QString();
    return sign + trimFloat(ev, 1) + QStringLiteral(" EV");
}

QString ExifFormatter::meteringMode(int value)
{
    switch (value) {
    case 0: return QStringLiteral("未知");
    case 1: return QStringLiteral("平均测光");
    case 2: return QStringLiteral("中央平均");
    case 3: return QStringLiteral("点测");
    case 4: return QStringLiteral("多点测");
    case 5: return QStringLiteral("图案测光");
    case 6: return QStringLiteral("局部测光");
    default: return QString();
    }
}

QString ExifFormatter::flash(int value)
{
    const bool fired = (value & 0x01) != 0;
    if (!fired)
        return QStringLiteral("未闪光");
    switch (value & 0x0F) {
    case 0x01: return QStringLiteral("闪光");
    case 0x07: return QStringLiteral("强制闪光");
    case 0x09: return QStringLiteral("补光闪光");
    case 0x0D: case 0x0F: return QStringLiteral("防红眼闪光");
    default: return QStringLiteral("闪光");
    }
}

QString ExifFormatter::whiteBalance(int value)
{
    switch (value) {
    case 0: return QStringLiteral("自动白平衡");
    case 1: return QStringLiteral("手动白平衡");
    default: return QString();
    }
}

QString ExifFormatter::exposureProgram(int value)
{
    switch (value) {
    case 0: return QStringLiteral("未定义");
    case 1: return QStringLiteral("手动");
    case 2: return QStringLiteral("程序自动");
    case 3: return QStringLiteral("光圈优先");
    case 4: return QStringLiteral("快门优先");
    case 5: return QStringLiteral("创意程序");
    case 6: return QStringLiteral("动作程序");
    case 7: return QStringLiteral("人像程序");
    case 8: return QStringLiteral("风景程序");
    default: return QString();
    }
}

QString ExifFormatter::sceneCaptureType(int value)
{
    switch (value) {
    case 0: return QStringLiteral("标准");
    case 1: return QStringLiteral("人像");
    case 2: return QStringLiteral("风景");
    case 3: return QStringLiteral("夜景");
    case 4: return QStringLiteral("其他");
    default: return QString();
    }
}

QString ExifFormatter::gpsDms(double decimal, bool positiveRef, QChar positiveSymbol,
                              QChar negativeSymbol)
{
    if (!qIsFinite(decimal))
        return QString();
    const QChar ref = positiveRef ? positiveSymbol : negativeSymbol;
    double v = qAbs(decimal);
    int degrees = int(v);
    v = (v - degrees) * 60.0;
    int minutes = int(v);
    double seconds = (v - minutes) * 60.0;
    // 进位修正（59.997" → 1'）
    if (seconds >= 59.95) {
        seconds = 0;
        ++minutes;
        if (minutes >= 60) {
            minutes = 0;
            ++degrees;
        }
    }
    return QStringLiteral("%1°%2'%3\"%4")
        .arg(degrees)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 4, 'f', 1, QChar('0'))
        .arg(ref);
}

QString ExifFormatter::gpsLatitude(double decimal, QChar ref)
{
    const bool north = (ref == QLatin1Char('N') || ref == QLatin1Char('n'));
    return gpsDms(decimal, north, QLatin1Char('N'), QLatin1Char('S'));
}

QString ExifFormatter::gpsLongitude(double decimal, QChar ref)
{
    const bool east = (ref == QLatin1Char('E') || ref == QLatin1Char('e'));
    return gpsDms(decimal, east, QLatin1Char('E'), QLatin1Char('W'));
}

QString ExifFormatter::gpsAltitude(double meters, bool belowSeaLevel)
{
    if (!qIsFinite(meters))
        return QString();
    const double v = belowSeaLevel ? -meters : meters;
    return QStringLiteral("%1 m").arg(qRound(v));
}

QString ExifFormatter::dateTime(const QString &exifDateTime, bool withSeconds)
{
    // Exif: "2026:10:10 12:00:00" → "2026-10-10 12:00[:00]"
    const QString norm = exifDateTime.trimmed();
    if (norm.size() != 19 || norm.at(4) != QLatin1Char(':'))
        return norm;   // 非标格式原样返回（有值总比丢值好）
    const QString date = QStringLiteral("%1-%2-%3")
                             .arg(norm.mid(0, 4), norm.mid(5, 2), norm.mid(8, 2));
    const QString time = norm.mid(11, withSeconds ? 8 : 5);
    return date + QLatin1Char(' ') + time;
}

QString ExifFormatter::trimFloat(double value, int maxDecimals)
{
    if (!qIsFinite(value))
        return QString();
    QString s = QString::number(value, 'f', qBound(0, maxDecimals, 6));
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s;
}

} // namespace fxt
