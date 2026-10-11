# 单元测试：core/platform 均为纯计算层（QtCore[/Gui] + Qt6::Test），无显示环境可跑。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Concurrent Test)

add_executable(test_appinfo test_appinfo.cpp)
target_link_libraries(test_appinfo PRIVATE framext_core Qt6::Core Qt6::Test)

# M5.1 模板参数化：参数声明/JSON 往返/几何覆盖（黄金图一致性由 test_golden 背书）
# 链 app 资源：断言 resources/templates/*.json 与 builtinSpecs 一致
add_executable(test_templates test_templates.cpp ../resources.qrc)
target_link_libraries(test_templates PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

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

# M2.5 往返测试集（真实样张，FXT_SAMPLE_DIR 环境变量指定，缺失时 SKIP）
add_executable(test_samples test_samples.cpp)
target_link_libraries(test_samples PRIVATE framext_core framext_platform Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME test_appinfo COMMAND test_appinfo)
add_test(NAME test_templates COMMAND test_templates)
add_test(NAME test_model COMMAND test_model)
add_test(NAME test_render COMMAND test_render)
add_test(NAME test_layout COMMAND test_layout)
add_test(NAME test_pipeline COMMAND test_pipeline)
add_test(NAME test_golden COMMAND test_golden)
add_test(NAME test_metadata COMMAND test_metadata)
add_test(NAME test_exif COMMAND test_exif)
# M2 收尾：BrandRegistry 品牌识别 + SensorRegistry 画幅倍率/等效焦距回退（R8）
# M5.4 起链 app 资源：断言 :/brands/brands.json 与 C++ 内置表一致
add_executable(test_brand test_brand.cpp ../resources.qrc)
target_link_libraries(test_brand PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

# M3 评审修复：ProjectController 状态机（撤销/批量组/模板保留规则/布局过滤）
# app 层 controller 直接编入测试 target（纯 QObject+QtCore 语义，可离屏测）
add_executable(test_projectcontroller test_projectcontroller.cpp
    ../src/app/controllers/ProjectController.cpp)
target_include_directories(test_projectcontroller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
target_link_libraries(test_projectcontroller PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

# M4 导出与批量：编码/命名/往返/健壮性 + ExportController 冲突与取消
# （app 层 ExportController 同 ProjectController 模式直接编入）
add_executable(test_export test_export.cpp
    ../src/app/controllers/ExportController.cpp)
target_include_directories(test_export PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
target_link_libraries(test_export PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Test)

# M4 性能基准（4.1 准入）：60 张 45MP 全尺寸导出 ≤60s。
# 独立 target 不进默认 ctest 轮（45MP 样本生成一次性成本高）；
# 手工跑：FXT_PERF_DIR=<持久目录> 缓存样本后重复测量
add_executable(perf_export perf_export.cpp
    ../src/app/controllers/ExportController.cpp)
target_include_directories(perf_export PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
target_link_libraries(perf_export PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Test)

# §6.1 指标 1：导入 100 张 45MP ≤15s（样本与 perf_export 共享 FXT_PERF_DIR）
add_executable(perf_import perf_import.cpp
    ../src/app/controllers/ImportController.cpp)
target_include_directories(perf_import PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
target_link_libraries(perf_import PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Test)

add_test(NAME test_samples COMMAND test_samples)
add_test(NAME test_brand COMMAND test_brand)
add_test(NAME test_projectcontroller COMMAND test_projectcontroller)
add_test(NAME test_export COMMAND test_export)
# perf_export 不进 ctest（手工跑，样本经 FXT_PERF_DIR 缓存）
