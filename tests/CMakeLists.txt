# 单元测试：只依赖 framext_core（QtCore/QtGui + Qt6::Test），可在无显示环境运行。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)

add_executable(test_appinfo test_appinfo.cpp)
target_link_libraries(test_appinfo PRIVATE framext_core Qt6::Core Qt6::Test)

add_executable(test_model test_model.cpp)
target_link_libraries(test_model PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

add_executable(test_render test_render.cpp)
target_link_libraries(test_render PRIVATE framext_core Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME test_appinfo COMMAND test_appinfo)
add_test(NAME test_model COMMAND test_model)
add_test(NAME test_render COMMAND test_render)
