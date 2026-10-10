#pragma once

#include <QByteArray>
#include <QList>

namespace fxt::Platform {

// PNG chunk 级读写（02-技术设计 §4.8）：eXIf / iCCP / pHYs。
// 畸形输入（坏签名/长度越界/CRC 错）返回空结果，绝不崩溃。
namespace PngChunkIO {

struct Chunk
{
    QByteArray type;    // 4 字节
    QByteArray data;

    bool operator==(const Chunk &) const = default;
};

// PNG 是否有效（8 字节签名）
bool isValidPng(const QByteArray &png);

// 解析全部 chunk（IDAT 等照常列出；调用方按 type 过滤）。
// 非 PNG / 畸形返回空表。
QList<Chunk> parse(const QByteArray &png);

// 重插/替换指定 type 的 chunk：删除已有的同 type chunk，将 newChunk 插入
// IEND 之前（PNG 规范：eXIf/iCCP/pHYs 均须在 IDAT 之前，此约束由调用方
// 确保位置语义；本函数保证「先删全部同 type，再在 IDAT 前插入」）。
// newChunk.data 为空时仅删除（摘除该类 chunk）。
QByteArray setChunk(const QByteArray &png, const Chunk &newChunk);

// —— 构造器 ——

// pHYs：dpi → pixels per metre（标准换算 round(dpi/0.0254)），unit=1
Chunk makePhys(double dpi);

// eXIf：直接包裹原始 Exif TIFF 块（不带 "Exif\0\0" 头——PNG eXIf 就是裸 TIFF）
Chunk makeExif(const QByteArray &tiff);

// 提取：文件中首个指定 type 的 chunk 数据；无则返回空
QByteArray chunkData(const QByteArray &png, const char type[4]);

} // namespace PngChunkIO

} // namespace fxt::Platform
