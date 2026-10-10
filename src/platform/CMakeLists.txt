# framext_platform：字节级元数据读写（02-技术设计 §7）。
# 依赖约束：仅 QtCore（QByteArray/QSize 等容器与值类型）；
# 不碰图像解码（render 层职责）与 UI。
add_library(framext_platform STATIC
    JpegSegmentIO.cpp
    ExifIfdEditor.cpp
    PngChunkIO.cpp
    MetadataMuxer.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Core)
target_link_libraries(framext_platform PUBLIC Qt6::Core)

target_include_directories(framext_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# —— 质量门：platform 不允许 Widgets/Gui/Concurrent/Quick ——
get_target_property(_fxt_platform_links framext_platform INTERFACE_LINK_LIBRARIES)
foreach(_lib ${_fxt_platform_links})
    if(_lib MATCHES "Qt6::(Gui|Widgets|Concurrent|Quick)")
        message(FATAL_ERROR "分层约束违规：framext_platform 不允许依赖 ${_lib}（仅允许 Qt6::Core）")
    endif()
endforeach()
