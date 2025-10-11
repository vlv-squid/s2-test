# GIS库构建配置 - 创建共享库

# 收集源文件
file(GLOB GISINDEX_SOURCES "gisindex/*.cpp")
file(GLOB GISSTORAGE_SOURCES "gisstorage/*.cpp")

# 创建gisindex共享库
add_library(gisindex SHARED ${GISINDEX_SOURCES})

# 设置gisindex库的包含目录
target_include_directories(gisindex PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/gisindex
)

# 链接gisindex库的依赖
target_link_libraries(gisindex PUBLIC
    GDAL::GDAL
    s2::s2
    TBB::tbb
)

# 设置gisindex库的编译特性
target_compile_features(gisindex PUBLIC cxx_std_17)

# 设置gisindex库的编译选项
target_compile_options(gisindex PRIVATE
    -Wall
    -Wextra
    -O2
    -fPIC
)

# 创建gisstorage共享库
add_library(gisstorage SHARED ${GISSTORAGE_SOURCES})

# 设置gisstorage库的包含目录
target_include_directories(gisstorage PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/gisstorage
)

# 链接gisstorage库的依赖
target_link_libraries(gisstorage PUBLIC
    gisindex
)

# 设置gisstorage库的编译特性
target_compile_features(gisstorage PUBLIC cxx_std_17)

# 设置gisstorage库的编译选项
target_compile_options(gisstorage PRIVATE
    -Wall
    -Wextra
    -O2
)

# 安装规则
install(TARGETS gisindex gisstorage
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# 安装头文件
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/gisindex
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/gisstorage
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# 打印信息
message(STATUS "GIS库配置完成")
message(STATUS "共享库: gisindex, gisstorage")
message(STATUS "gisindex依赖: s2, TBB")
message(STATUS "gisstorage依赖: gisindex, GDAL, Boost::geometry, nlohmann_json, s2, TBB")

