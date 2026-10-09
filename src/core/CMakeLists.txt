# framext_core：领域模型与纯计算层（渲染 / 元数据 / 模板逻辑，02-技术设计 §7）。
add_library(framext_core STATIC
    AppInfo.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Core)
target_link_libraries(framext_core PUBLIC Qt6::Core)

target_include_directories(framext_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# —— 质量门（03-开发计划 0.2）：core 层禁止链接 Gui/Widgets/Concurrent/Quick ——
# 保证领域逻辑可脱离 UI 独立单测与复用（高内聚、低耦合）。
get_target_property(_fxt_core_links framext_core INTERFACE_LINK_LIBRARIES)
foreach(_lib ${_fxt_core_links})
    if(_lib MATCHES "Qt6::(Gui|Widgets|Concurrent|Quick)")
        message(FATAL_ERROR "分层约束违规：framext_core 不允许依赖 ${_lib}（仅允许 Qt6::Core）")
    endif()
endforeach()
