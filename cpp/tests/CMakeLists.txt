# 测试构建配置

# 单元测试
# S2索引单元测试
add_executable(s2index_test 
    unit/gisindex/s2index_test.cpp
)
target_link_libraries(s2index_test PRIVATE
    gisindex
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

# GIS存储单元测试
add_executable(gis_storage_test
    unit/gisstorage/gis_storage_test.cpp
)
target_link_libraries(gis_storage_test PRIVATE
    gisstorage
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

# 逆向转换单元测试
add_executable(reverse_conversion_test
    unit/reverse/test_reverse_conversion.cpp
)
target_link_libraries(reverse_conversion_test PRIVATE
    gisstorage
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

# 集成测试
add_executable(integrated_gis_format_test 
    integration/integrated_gis_format_test.cpp
)
target_link_libraries(integrated_gis_format_test
    gisstorage
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

# 性能测试
add_executable(file_io_performance_test
    performance/file_io_performance_test.cpp
)
target_link_libraries(file_io_performance_test PRIVATE
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

# GDAL FileGDB 性能测试
add_executable(gdal_filegdb_performance_test
    performance/gdal_filegdb_performance_test.cpp
)
target_link_libraries(gdal_filegdb_performance_test PRIVATE
    GDAL::GDAL
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

# 自定义格式性能测试
add_executable(custom_format_performance_test
    performance/custom_format_performance_test.cpp
)
target_link_libraries(custom_format_performance_test PRIVATE
    gisstorage
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

# 添加测试到CTest
enable_testing()
add_test(NAME s2index_test COMMAND s2index_test)
add_test(NAME gis_storage_test COMMAND gis_storage_test)
add_test(NAME reverse_conversion_test COMMAND reverse_conversion_test)
add_test(NAME integrated_gis_format_test COMMAND integrated_gis_format_test)
add_test(NAME file_io_performance_test COMMAND file_io_performance_test)
add_test(NAME gdal_filegdb_performance_test COMMAND gdal_filegdb_performance_test)
add_test(NAME custom_format_performance_test COMMAND custom_format_performance_test)

# 设置测试属性
set_tests_properties(s2index_test PROPERTIES
    TIMEOUT 300
    LABELS "unit;gisindex"
)

set_tests_properties(gis_storage_test PROPERTIES
    TIMEOUT 300
    LABELS "unit;gisstorage"
)

set_tests_properties(reverse_conversion_test PROPERTIES
    TIMEOUT 300
    LABELS "unit;reverse;conversion"
)

set_tests_properties(integrated_gis_format_test PROPERTIES
    TIMEOUT 600
    LABELS "integration"
)

set_tests_properties(file_io_performance_test PROPERTIES
    TIMEOUT 300
    LABELS "performance"
)

set_tests_properties(gdal_filegdb_performance_test PROPERTIES
    TIMEOUT 300
    LABELS "performance;gdal"
)

set_tests_properties(custom_format_performance_test PROPERTIES
    TIMEOUT 300
    LABELS "performance;custom"
)

# 打印信息
message(STATUS "测试配置完成")
message(STATUS "单元测试: s2index_test, gis_storage_test, reverse_conversion_test")
message(STATUS "集成测试: integrated_gis_format_test")
message(STATUS "性能测试: file_io_performance_test, gdal_filegdb_performance_test, custom_format_performance_test")
