#pragma once

#include <QString>

#include "platform/MetadataMuxer.h"

namespace fxt {

// 输出文件格式（§3.3：格式在「自定义…」高级项中选择）
enum class OutputFormat { Jpeg, Png };

// JPEG 色度子采样（02-技术设计 §4.8；Qt 的 JPEG 插件不可控子采样，
// 编码走 libjpeg-turbo —— 视觉无损预设必须 4:4:4）
enum class JpegSubsampling { s420, s422, s444 };

// 画质预设（§3.3，R2 画质是 P0 反馈）：
//   视觉无损 = q100 + 4:4:4（PSNR ≥45dB 准入，test_export 钉住）
//   平衡     = q90 + 4:2:0
enum class QualityPreset { VisualLossless, Balanced, Custom };

// 输出尺寸（§3.3 尺寸下拉）
enum class SizeMode { Original, LongEdge };

// 命名模板（§3.3：保留原名/原名+序号/日期+原名/自定义占位符）
enum class NamingMode {
    KeepOriginal,        // DSC_2261.jpg
    OriginalPlusIndex,   // DSC_2261-0001.jpg（indexStart/indexDigits 可配）
    DatePlusOriginal,    // 20261010_DSC_2261.jpg（yyyyMMdd）
    CustomPattern,       // 自定义占位符 {name} {index} {date} {model}
};

// 同名冲突策略（02-技术设计 §4.8：批量前预扫 + 按策略处置，默认自动重命名；
// AskEach = §5.3 冲突对话框的「每个文件询问」，preplan 串行逐个弹问）
enum class ConflictPolicy { AutoRename, Overwrite, Skip, AskEach };

// 一次批量导出的全部选项（§3.3 表单的值映射；纯值类型，无行为）
struct ExportOptions
{
    QString outputDir;

    OutputFormat format = OutputFormat::Jpeg;
    // JPEG 画质（format==Jpeg 时生效）
    QualityPreset preset = QualityPreset::VisualLossless;
    int customQuality = 90;                    // preset==Custom 时的质量 1..100
    JpegSubsampling customSubsampling = JpegSubsampling::s420;

    SizeMode sizeMode = SizeMode::Original;
    int longEdge = 2048;                       // sizeMode==LongEdge 时生效

    NamingMode naming = NamingMode::KeepOriginal;
    QString customPattern = QStringLiteral("{name}");   // 占位符模板
    int indexStart = 1;                        // 序号起始
    int indexDigits = 4;                       // 序号位数（补零）

    ConflictPolicy conflict = ConflictPolicy::AutoRename;

    // 元数据策略（结构直用 MetadataMuxer 的 ExportPolicy，语义单一来源）
    Platform::ExportPolicy meta;

    // 便捷派生：预设 → (质量, 子采样)
    int quality() const
    {
        switch (preset) {
        case QualityPreset::VisualLossless: return 100;
        case QualityPreset::Balanced: return 90;
        case QualityPreset::Custom: return customQuality;
        }
        return 90;
    }
    JpegSubsampling subsampling() const
    {
        if (preset == QualityPreset::VisualLossless)
            return JpegSubsampling::s444;
        if (preset == QualityPreset::Custom)
            return customSubsampling;
        return JpegSubsampling::s420;
    }
    QString outputSuffix() const
    {
        return format == OutputFormat::Jpeg ? QStringLiteral(".jpg")
                                            : QStringLiteral(".png");
    }

    bool operator==(const ExportOptions &) const = default;
};

} // namespace fxt
