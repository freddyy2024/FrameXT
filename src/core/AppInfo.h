#pragma once

#include <QString>

// 应用级元信息（core 层，禁止依赖 Gui/Widgets）。
// 注意：版本号与根 CMakeLists.txt 的 project(FrameXT VERSION ...) 保持同步。
namespace fxt {

struct AppInfo
{
    static constexpr const char *kAppName   = "FrameXT";
    // 2.x = Qt 重构新一代（原 Flutter 版为 1.x，版本系独立不续接）
    static constexpr const char *kVersion   = "2.0.2";
    static constexpr const char *kOrgName   = "FrameXT";
    static constexpr const char *kOrgDomain = "framext.com";
    static constexpr const char *kHomePage  = "https://framext.com";

    // 展示用版本串，如 "2.0.2"
    static QString versionText();
};

} // namespace fxt
