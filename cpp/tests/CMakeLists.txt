# 测试构建配置

# 收集源文件
file(GLOB GISSTORAGE_SOURCES "../src/gisstorage/*.cpp")
file(GLOB GISINDEX_SOURCES "../src/gisindex/*.cpp")

# 单元测试
# S2索引单元测试
add_executable(s2index_test 
    unit/gisindex/s2index_test.cpp
    ${GISINDEX_SOURCES}
)
target_link_libraries(s2index_test PRIVATE
    GDAL::GDAL
    Boost::geometry
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
    s2::s2
    TBB::tbb
)

# GIS存储单元测试
add_executable(gis_storage_test
    unit/gisstorage/gis_storage_test.cpp
    ${GISSTORAGE_SOURCES}
    ${GISINDEX_SOURCES}
)
target_link_libraries(gis_storage_test PRIVATE
    GDAL::GDAL
    Boost::geometry
    nlohmann_json::nlohmann_json
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
    s2::s2
    TBB::tbb
)

# 集成测试
add_executable(integrated_gis_format_test 
    integration/integrated_gis_format_test.cpp
    ${GISSTORAGE_SOURCES}
    ${GISINDEX_SOURCES}
)
target_link_libraries(integrated_gis_format_test
    GDAL::GDAL
    Boost::geometry
    s2::s2
    TBB::tbb
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
    ${GISSTORAGE_SOURCES}
    ${GISINDEX_SOURCES}
)
target_link_libraries(custom_format_performance_test PRIVATE
    GDAL::GDAL
    Boost::geometry
    nlohmann_json::nlohmann_json
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
    s2::s2
    TBB::tbb
)

# 添加测试到CTest
enable_testing()
add_test(NAME s2index_test COMMAND s2index_test)
add_test(NAME gis_storage_test COMMAND gis_storage_test)
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
message(STATUS "单元测试: s2index_test, gis_storage_test")
message(STATUS "集成测试: integrated_gis_format_test")
message(STATUS "性能测试: file_io_performance_test, gdal_filegdb_performance_test, custom_format_performance_test")
