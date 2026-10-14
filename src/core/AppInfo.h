#pragma once

#include <QString>

// 应用级元信息（core 层，禁止依赖 Gui/Widgets）。
// 注意：版本号与根 CMakeLists.txt 的 project(FrameXT VERSION ...) 保持同步。
namespace fxt {

struct AppInfo
{
    static constexpr const char *kAppName   = "FrameXT";
    static constexpr const char *kVersion   = "1.1.0";
    static constexpr const char *kOrgName   = "FrameXT";
    static constexpr const char *kOrgDomain = "framext.com";
    static constexpr const char *kHomePage  = "https://framext.com";

    // 展示用版本串，如 "0.1.0"
    static QString versionText();
};

} // namespace fxt
