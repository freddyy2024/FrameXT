#pragma once

#include <QImage>
#include <QString>

#include "export/ExportOptions.h"
#include "model/PhotoDocument.h"
#include "render/RenderPipeline.h"

namespace fxt {

// 单张导出（S6 渲染 → S7 编码 → S8 元数据 → 落盘）的纯函数组装。
// 无 Qt 事件依赖、无并发原语——并发调度在 app 层 ExportController，
// 本层可独立单测（02-技术设计 §4.7「每张独立 PhotoDocument，无共享
// 可变状态」）。
namespace ExportEngine {

// —— 命名引擎（§3.3 / 4.3）——
// 返回不含目录的目标文件名（含扩展名）。
// index 为**同基名组内**的序号（0 起，内部按 indexStart 偏移）——
// 「原名+序号」的本意是同名照片（不同目录同名导入，#14 场景）各得
// -0001/-0002，不同基名互不挤占；由 controller 预扫按基名分组分配，
// 单张使用传 0。仅 OriginalPlusIndex / CustomPattern(含 {index}) 生效。
QString buildFileName(const PhotoDocument &doc, const ExportOptions &options,
                      int index);

// —— 单张导出 ——
struct ItemResult
{
    bool ok = false;
    bool skipped = false;       // 同名冲突按 Skip 策略跳过（非失败）
    bool canceled = false;      // 取消窗口内未派发（非失败，勿重试）
    QString sourcePath;
    QString outputPath;
    QString stage;              // 失败阶段：read/render/encode/write
    QString error;              // 用户可读原因（失败清单直接展示）
    qint64 sourceBytes = 0;     // 体积对比统计（§5.2）
    qint64 outputBytes = 0;
};

struct ItemRequest
{
    PhotoDocument doc;          // 快照（导出不依赖预览历史，§4.2）
    ExportOptions options;
    QString outputPath;         // 预分配的最终绝对路径（冲突消解在
                                // controller 主线程串行完成，worker 无
                                // 文件系统竞态；Skip 的项 controller 直接
                                // 标记不派发）
    int index = 0;              // 同基名组内序号（诊断/日志；命名已在 preplan 完成）
    FieldValueResolver resolver;   // 导入时算好的展示值集（含覆盖层）
    QImage logo;                // 已解析的 Logo 位图（worker 内不再读盘）
};

// 执行单张导出。绝不抛异常、绝不崩溃：任何失败都归约为 ok=false + stage/error。
ItemResult exportSingle(const ItemRequest &request);

} // namespace ExportEngine

} // namespace fxt
