# framext_core：领域模型与纯计算层（渲染 / 元数据 / 模板逻辑，02-技术设计 §7）。
# 依赖约束（§3）：允许 QtCore + QtGui（QImage/QPainter/QColor 等渲染数据结构），
# 禁止 Widgets/Concurrent/Quick —— 保证领域逻辑可脱离 UI 独立单测与复用。
add_library(framext_core STATIC
    AppInfo.cpp
    AppPaths.cpp
    model/ModelIo.cpp
    model/TemplateSpec.cpp
    metadata/BrandRegistry.cpp
    metadata/ExifFormatter.cpp
    metadata/FieldRegistry.cpp
    metadata/ExifReader.cpp
    metadata/SensorRegistry.cpp
    render/CanvasGeometry.cpp
    render/BackdropRenderer.cpp
    render/PhotoDecoder.cpp
    render/LayoutEngine.cpp
    render/ElementPainter.cpp
    render/RenderCacheManager.cpp
    render/BuiltinTemplates.cpp
    render/RenderPipeline.cpp
    export/JpegEncoder.cpp
    export/ExportEngine.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
# exiv2（GPL-2.0-or-later；项目 GPLv3+ 开源，许可兼容——02-技术设计 §2.3）
find_package(exiv2 CONFIG REQUIRED)
# libjpeg-turbo：M4 导出编码（q100+4:4:4 视觉无损预设；Qt JPEG 插件
# 不暴露子采样——JpegEncoder.h 头注）
find_package(libjpeg-turbo CONFIG REQUIRED)
# framext_platform：M4 起 export 层直接复用字节级元数据搬运（§4.8
# MetadataMuxer）——platform 位于 core 之下，正向依赖无环
target_link_libraries(framext_core PUBLIC Qt6::Core Qt6::Gui exiv2lib
    libjpeg-turbo::turbojpeg framext_platform)

target_include_directories(framext_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# —— 质量门（03-开发计划 0.2）：UI 层依赖不得混入 core ——
get_target_property(_fxt_core_links framext_core INTERFACE_LINK_LIBRARIES)
foreach(_lib ${_fxt_core_links})
    if(_lib MATCHES "Qt6::(Widgets|Concurrent|Quick)")
        message(FATAL_ERROR "分层约束违规：framext_core 不允许依赖 ${_lib}（仅允许 Qt6::Core/Qt6::Gui）")
    endif()
endforeach()
