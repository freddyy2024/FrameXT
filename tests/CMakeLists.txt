# 单元测试：只依赖 framext_core（Qt6::Core + Qt6::Test），可在无显示环境运行。
find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(test_appinfo test_appinfo.cpp)
target_link_libraries(test_appinfo PRIVATE framext_core Qt6::Core Qt6::Test)

add_test(NAME test_appinfo COMMAND test_appinfo)
