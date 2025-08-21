cmake_minimum_required(VERSION 3.28.3)

set(GEOTALK_HOME "$ENV{GEOTALK_HOME}" CACHE INTERNAL "路径")
message("GEOTALK_HOME路径：" ${GEOTALK_HOME})
set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "vcpkg target triplet")
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
    /usr/lib/x86_64-linux-gnu/libs2.so
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

# compatibility_test
add_executable(compatibility_test
    test/compatibility_test.cpp
    ${GISSTORAGE_SOURCES}
)
target_link_libraries(compatibility_test PRIVATE
    GDAL::GDAL
    Boost::geometry
    nlohmann_json::nlohmann_json
    GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
)