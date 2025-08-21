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

# s2index_test
add_executable(s2index_test 
    test/s2index_test.cpp
    src/hybrid_index.cpp
    src/s2spatial_index.cpp
    src/rtree_spatial_index.cpp
    src/serialize_rtree.cpp
    src/serialize_s2.cpp
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
    src/gis_storage.cpp
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
    src/gis_storage.cpp
)
target_link_libraries(compatibility_test PRIVATE
    GDAL::GDAL
    Boost::geometry
    nlohmann_json::nlohmann_json
)