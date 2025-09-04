# 基准测试构建配置

# 收集源文件
file(GLOB GISSTORAGE_SOURCES "../src/gisstorage/*.cpp")
file(GLOB GISINDEX_SOURCES "../src/gisindex/*.cpp")

# 空间索引基准测试
add_executable(spatial_index_benchmark
    spatial_index_benchmark.cpp
    ${GISSTORAGE_SOURCES}
    ${GISINDEX_SOURCES}
)

target_link_libraries(spatial_index_benchmark PRIVATE
    GDAL::GDAL
    Boost::geometry
    s2::s2
    TBB::tbb
)

# 设置编译选项
target_compile_options(spatial_index_benchmark PRIVATE
    -O3
    -DNDEBUG
)

# 安装规则
install(TARGETS spatial_index_benchmark
    RUNTIME DESTINATION bin
)

# 打印信息
message(STATUS "基准测试配置完成")
message(STATUS "基准测试: spatial_index_benchmark")

