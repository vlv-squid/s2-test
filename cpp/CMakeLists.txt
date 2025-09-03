cmake_minimum_required(VERSION 3.28.3)

set(GEOTALK_HOME "$ENV{GEOTALK_HOME}" CACHE INTERNAL "路径")
message("GEOTALK_HOME路径：" ${GEOTALK_HOME})

# 检测系统架构并设置合适的vcpkg target triplet
# 首先尝试从CMAKE_SYSTEM_PROCESSOR获取架构信息
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(DEFAULT_VCPKG_TRIPLET "arm64-linux")
    message(STATUS "从CMAKE_SYSTEM_PROCESSOR检测到ARM64架构，设置vcpkg triplet为: ${DEFAULT_VCPKG_TRIPLET}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(DEFAULT_VCPKG_TRIPLET "x64-linux")
    message(STATUS "从CMAKE_SYSTEM_PROCESSOR检测到x86_64架构，设置vcpkg triplet为: ${DEFAULT_VCPKG_TRIPLET}")
else()
    # 如果CMAKE_SYSTEM_PROCESSOR为空或未知，尝试从编译器名称推断
    if(CMAKE_C_COMPILER MATCHES "aarch64|arm64")
        set(DEFAULT_VCPKG_TRIPLET "arm64-linux")
        message(STATUS "从编译器名称检测到ARM64架构，设置vcpkg triplet为: ${DEFAULT_VCPKG_TRIPLET}")
    elseif(CMAKE_C_COMPILER MATCHES "x86_64|amd64")
        set(DEFAULT_VCPKG_TRIPLET "x64-linux")
        message(STATUS "从编译器名称检测到x86_64架构，设置vcpkg triplet为: ${DEFAULT_VCPKG_TRIPLET}")
    else()
        # 最后尝试使用execute_process获取架构信息
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE ARCH_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(ARCH_OUTPUT MATCHES "aarch64|arm64")
            set(DEFAULT_VCPKG_TRIPLET "arm64-linux")
            message(STATUS "从uname -m检测到ARM64架构，设置vcpkg triplet为: ${DEFAULT_VCPKG_TRIPLET}")
        elseif(ARCH_OUTPUT MATCHES "x86_64|amd64")
            set(DEFAULT_VCPKG_TRIPLET "x64-linux")
            message(STATUS "从uname -m检测到x86_64架构，设置vcpkg triplet为: ${DEFAULT_VCPKG_TRIPLET}")
        else()
            set(DEFAULT_VCPKG_TRIPLET "x64-linux")
            message(WARNING "无法检测架构 (CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}', 编译器='${CMAKE_C_COMPILER}', uname='${ARCH_OUTPUT}')，默认使用x64-linux triplet")
        endif()
    endif()
endif()

set(VCPKG_TARGET_TRIPLET "${DEFAULT_VCPKG_TRIPLET}" CACHE STRING "vcpkg target triplet")
message(STATUS "使用vcpkg target triplet: ${VCPKG_TARGET_TRIPLET}")

set(CMAKE_TOOLCHAIN_FILE "${GEOTALK_HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "cmake toolchain file")
set(CMAKE_PREFIX_PATH "${GEOTALK_HOME}/CppDepend/Release" CACHE INTERNAL "geotalk debug路径")

project(s2test)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

find_package(GTest REQUIRED)
find_package(Boost REQUIRED COMPONENTS geometry)
find_package(GDAL REQUIRED PATHS "${CMAKE_PREFIX_PATH}/lib64/cmake/gdal")
find_package(nlohmann_json REQUIRED)
find_package(s2 REQUIRED)
find_package(TBB REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_PREFIX_PATH}/include
)

# 使用file(GLOB)收集源文件
file(GLOB GISSTORAGE_SOURCES "src/gisstorage/*.cpp")
file(GLOB GISINDEX_SOURCES "src/gisindex/*.cpp")
file(GLOB TEST_SOURCES "test/*.cpp")

# 打印收集到的源文件（调试用）
message(STATUS "GIS存储源文件: ${GISSTORAGE_SOURCES}")
message(STATUS "GIS索引源文件: ${GISINDEX_SOURCES}")
message(STATUS "测试源文件: ${TEST_SOURCES}")

# s2index_test
add_executable(s2index_test 
    test/s2index_test.cpp
    ${GISINDEX_SOURCES}
)
target_link_libraries(s2index_test PRIVATE
    GDAL::GDAL
    Boost::geometry
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
    s2::s2
    TBB::tbb
)

# gis_storage_test
add_executable(gis_storage_test
    test/gis_storage_test.cpp
    ${GISSTORAGE_SOURCES}
)
target_link_libraries(gis_storage_test PRIVATE
    GDAL::GDAL
    Boost::geometry
    nlohmann_json::nlohmann_json
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

# file_io_performance_test
add_executable(file_io_performance_test
    test/file_io_performance_test.cpp
)
target_link_libraries(file_io_performance_test PRIVATE
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)

# spatial_index_benchmark
add_executable(spatial_index_benchmark
    src/performance/spatial_index_benchmark.cpp
)
target_link_libraries(spatial_index_benchmark PRIVATE
    GDAL::GDAL
    Boost::geometry
    s2::s2
)

# integrated_gis_format_test
add_executable(integrated_gis_format_test 
    test/integrated_gis_format_test.cpp
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
