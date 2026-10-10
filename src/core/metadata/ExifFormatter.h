#pragma once

#include <QString>

namespace fxt {

// 有理数（Exif RATIONAL / SRATIONAL 的通用承载）
struct ExifRational
{
    qint32 numerator = 0;
    quint32 denominator = 1;   // 0 视为非法（格式化返回空）

    bool operator==(const ExifRational &) const = default;
};

// Exif 值格式化器库（02-技术设计 §4.5；方案 §7.2/§7.3）。
// 全部纯函数：非法/缺失输入返回空串（字段静默置空，UI 显"—"）。
namespace ExifFormatter {

// 快门：ExposureTime 秒 → "1/160"；≥1s → "1.6s"；长曝光 30/1 → "30s"；
// 奇异值（0/0、超范围）返回空
QString shutter(double seconds);

// 光圈：FNumber → "f/5.6"（一位小数去尾零：f/8 而非 f/8.0）
QString fNumber(double value);

// 实际焦距 → "42mm"；等效焦距 → "63mm eq."
QString focalLength(double mm);
QString focalLengthEq(double mm);

// ISO → "ISO 140"
QString iso(int value);

// 曝光补偿（SRATIONAL，EV）→ "+0.3 EV" / "-1 EV" / "0 EV"
QString exposureComp(double ev);

// —— 枚举名称化（Exif 主规范的固定映射；未知值返回空）——
QString meteringMode(int value);      // 评价/中央平均/点测/局部/多区…
QString flash(int value);             // 未闪/闪开/防红眼/强制…
QString whiteBalance(int value);      // 自动/手动
QString exposureProgram(int value);   // 手动/程序/光圈优先/快门优先…
QString sceneCaptureType(int value);  // 标准/人像/风景/夜景/其他

// GPS：十进制度 → "25°02'06.0\"N"（DMS，Ref 取 N/S/E/W）；非法返回空
QString gpsDms(double decimal, bool positiveRef, QChar positiveSymbol,
               QChar negativeSymbol);

// GPS 纬/经（十进制 + Ref）→ DMS 串
QString gpsLatitude(double decimal, QChar ref);
QString gpsLongitude(double decimal, QChar ref);

// 海拔（米，海拔基准 1 = 海平面以下取负）→ "1234 m"
QString gpsAltitude(double meters, bool belowSeaLevel);

// Exif DateTime "2026:10:10 12:00:00" → "2026-10-10 12:00"（模板化：
// 保留占位 yyyy-MM-dd HH:mm / HH:mm:ss 扩展点；解析失败返回原样）
QString dateTime(const QString &exifDateTime, bool withSeconds = false);

// 通用数值清理：去除浮点尾零（"5.60" → "5.6"）
QString trimFloat(double value, int maxDecimals = 1);

} // namespace ExifFormatter

} // namespace fxt
