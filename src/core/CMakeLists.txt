# framext_core：领域模型与纯计算层（渲染 / 元数据 / 模板逻辑，02-技术设计 §7）。
# 依赖约束（§3）：允许 QtCore + QtGui（QImage/QPainter/QColor 等渲染数据结构），
# 禁止 Widgets/Concurrent/Quick —— 保证领域逻辑可脱离 UI 独立单测与复用。
add_library(framext_core STATIC
    AppInfo.cpp
    model/ModelIo.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
target_link_libraries(framext_core PUBLIC Qt6::Core Qt6::Gui)

target_include_directories(framext_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# —— 质量门（03-开发计划 0.2）：UI 层依赖不得混入 core ——
get_target_property(_fxt_core_links framext_core INTERFACE_LINK_LIBRARIES)
foreach(_lib ${_fxt_core_links})
    if(_lib MATCHES "Qt6::(Widgets|Concurrent|Quick)")
        message(FATAL_ERROR "分层约束违规：framext_core 不允许依赖 ${_lib}（仅允许 Qt6::Core/Qt6::Gui）")
    endif()
endforeach()
