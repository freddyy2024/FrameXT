# 单元测试：core/platform 均为纯计算层（QtCore[/Gui] + Qt6::Test），无显示环境可跑。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)

add_executable(test_appinfo test_appinfo.cpp)
target_link_libraries(test_appinfo PRIVATE framext_core Qt6::Core Qt6::Test)

add_executable(test_model test_model.cpp)
target_link_libraries(test_model PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

add_executable(test_render test_render.cpp)
target_link_libraries(test_render PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

add_executable(test_layout test_layout.cpp)
target_link_libraries(test_layout PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

add_executable(test_pipeline test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

add_executable(test_golden test_golden.cpp)
target_link_libraries(test_golden PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)
# 黄金图基准写回源码树 tests/golden/
target_compile_definitions(test_golden PRIVATE
    GOLDEN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# M2 platform 层：字节级元数据读写（JpegSegmentIO/ExifIfdEditor/PngChunkIO/Muxer）
add_executable(test_metadata test_metadata.cpp)
target_link_libraries(test_metadata PRIVATE framext_platform Qt6::Core Qt6::Gui Qt6::Test)

# M2.1/2.2 元数据引擎：ExifFormatter/FieldRegistry/ExifReader(exiv2)
add_executable(test_exif test_exif.cpp)
target_link_libraries(test_exif PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME test_appinfo COMMAND test_appinfo)
add_test(NAME test_model COMMAND test_model)
add_test(NAME test_render COMMAND test_render)
add_test(NAME test_layout COMMAND test_layout)
add_test(NAME test_pipeline COMMAND test_pipeline)
add_test(NAME test_golden COMMAND test_golden)
add_test(NAME test_metadata COMMAND test_metadata)
add_test(NAME test_exif COMMAND test_exif)
