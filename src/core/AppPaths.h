#pragma once

#include <QString>

namespace fxt::AppPaths {

// 应用可写数据根（M6.1 便携版支持）：
//  - 便携模式：exe 同级存在 FrameXT.portable 标记文件（内容忽略）时，
//    数据随程序走（<exeDir>/Data）——解压即用、U 盘可携、删目录即卸载；
//  - 安装模式：平台标准位置（Windows %APPDATA%/FrameXT）。
// 打包脚本在 portable 包根放入空标记文件；安装包不含。
// 前置：QCoreApplication 已实例化（main 与测试入口均满足）。
QString appDataRoot();

} // namespace fxt::AppPaths
