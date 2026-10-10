#pragma once

#include <QByteArray>
#include <QList>

#include "QtGlobal"

namespace fxt::Platform {

// JPEG 元数据段（仅 APPn / COM；压缩数据段不进此抽象）
// 02-技术设计 §4.8：MetadataMuxer 的段级搬运基础件
struct JpegSegment
{
    quint8 marker = 0;     // 0xE0..0xEF (APPn) / 0xFE (COM)
    QByteArray payload;    // 段内容（不含 marker 与 2 字节长度）

    bool operator==(const JpegSegment &) const = default;
};

// JPEG segment 链解析/序列化。畸形输入（截断/坏长度/坏签名）一律返回空
// 结果，绝不崩溃（fuzz 准入：G2）。
namespace JpegSegmentIO {

// 提取 JPEG 中的全部 APPn/COM 段（按出现顺序）。SOS 起为压缩数据，不解析。
// 非 JPEG（无 SOI/FF 签名）返回空表。
QList<JpegSegment> parse(const QByteArray &jpeg);

// 以 encodedImage（编码器输出）为骨架，丢弃其中已有的 APPn/COM（防重复段），
// 将 metadataSegments 按序重插到 SOS 之前，返回最终文件字节。
// encodedImage 非 JPEG（无 SOI）时原样返回。
QByteArray splice(const QByteArray &encodedImage,
                  const QList<JpegSegment> &metadataSegments);

} // namespace JpegSegmentIO

} // namespace fxt::Platform
