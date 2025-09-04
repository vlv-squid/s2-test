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

# 添加子目录
add_subdirectory(tests)
add_subdirectory(benchmarks)
add_subdirectory(examples)
