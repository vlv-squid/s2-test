# C++ GIS 空间索引与存储系统

## 📋 项目概述

这是一个高性能的C++ GIS空间索引与存储系统，提供了多种空间索引算法（S2、R树、混合索引）和自定义二进制存储格式。该系统专为大规模地理空间数据处理而设计，支持高效的几何数据压缩、快速空间查询和多线程并行处理。

## 🏗️ 系统架构

### 核心模块

#### 1. 空间索引模块 (`gisindex/`)
- **S2空间索引**: 基于Google S2几何库的全球空间索引
- **R树索引**: 经典的空间索引结构，适用于局部查询
- **混合索引**: 结合S2和R树的优势，提供最优查询性能
- **序列化支持**: 索引数据的持久化存储和快速加载

#### 2. 存储系统模块 (`gisstorage/`)
- **几何数据存储**: 高效的几何数据二进制存储格式
- **属性数据存储**: 支持JSON格式的属性数据存储
- **字符串池优化**: 字符串去重和压缩存储
- **Shapefile转换**: 支持Shapefile到自定义格式的转换

## 🚀 主要特性

### 空间索引特性
- **多级索引**: 支持S2、R树、混合索引等多种索引类型
- **自动优化**: 根据查询范围自动选择最优索引策略
- **持久化存储**: 索引数据可持久化到磁盘，支持快速加载
- **并发查询**: 支持多线程并发空间查询

### 存储系统特性
- **坐标压缩**: 差分编码压缩，可减少50%存储空间
- **二进制格式**: 高效的二进制存储格式，支持快速读写
- **索引缓存**: 智能缓存机制，提高随机访问性能
- **数据完整性**: 完善的数据校验和错误恢复机制

## 📁 文件结构

```
cpp/
├── include/
│   ├── gisindex/           # 空间索引头文件
│   │   ├── s2spatial_index.h      # S2空间索引
│   │   ├── rtree_spatial_index.h  # R树空间索引
│   │   ├── hybrid_index.h         # 混合索引
│   │   ├── serialize_s2.h         # S2索引序列化
│   │   ├── serialize_rtree.h      # R树索引序列化
│   │   └── struct_dkbbox.h        # 数据结构定义
│   └── gisstorage/         # 存储系统头文件
│       ├── gis_storage.h          # 主存储接口
│       ├── gis_storage_system.h   # 存储系统管理
│       ├── geometry_storage.h     # 几何数据存储
│       ├── attribute_storage.h    # 属性数据存储
│       ├── geometry_data.h        # 几何数据结构
│       ├── attribute_data.h       # 属性数据结构
│       ├── geometry_serializer.h  # 几何数据序列化
│       ├── attribute_serializer.h # 属性数据序列化
│       ├── string_pool.h          # 字符串池优化
│       └── shapefile_converter.h  # Shapefile转换
├── src/
│   ├── gisindex/           # 空间索引实现
│   │   ├── s2spatial_index.cpp
│   │   ├── rtree_spatial_index.cpp
│   │   ├── hybrid_index.cpp
│   │   ├── serialize_s2.cpp
│   │   └── serialize_rtree.cpp
│   └── gisstorage/         # 存储系统实现
│       ├── gis_storage.cpp
│       ├── gis_storage_system.cpp
│       ├── geometry_storage.cpp
│       ├── attribute_storage.cpp
│       ├── geometry_serializer.cpp
│       ├── attribute_serializer.cpp
│       ├── string_pool.cpp
│       └── shapefile_converter.cpp
├── test/                   # 测试文件
│   ├── s2index_test.cpp           # S2索引测试
│   ├── gis_storage_test.cpp       # 存储系统测试
│   └── file_io_performance_test.cpp # 性能测试
├── CMakeLists.txt          # 构建配置
└── README.md              # 本文档
```

## 🛠️ 编译与安装

### 系统要求
- **编译器**: GCC 7.0+ 或 Clang 5.0+
- **C++标准**: C++17 或更高版本
- **依赖库**: 
  - S2 Geometry Library
  - Boost.Geometry
  - GDAL/OGR (可选，用于Shapefile支持)
  - SQLite3 (用于索引持久化)

### 编译步骤

```bash
# 1. 创建构建目录
cd cpp
mkdir build && cd build

# 2. 配置CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# 3. 编译
make -j$(nproc)

# 4. 运行测试
make test
```

### 依赖安装

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install libgdal-dev libboost-all-dev libsqlite3-dev
```

#### CentOS/RHEL
```bash
sudo yum install gdal-devel boost-devel sqlite-devel
```

## 📖 使用示例

### 基本使用

```cpp
#include "gisindex/s2spatial_index.h"
#include "gisstorage/gis_storage_system.h"
#include <iostream>

int main() {
    // 创建S2空间索引
    S2SpatialIndex index("./data/test.gdb", 15);
    
    // 构建索引
    index.buildIndex();
    
    // 执行空间查询
    BBox query_bbox = {103.2504, 26.4297, 103.3028, 26.4747};
    auto results = index.queryByBBox(query_bbox);
    
    std::cout << "查询结果数量: " << results.size() << std::endl;
    
    return 0;
}
```

### 存储系统使用

```cpp
#include "gisstorage/gis_storage_system.h"

int main() {
    // 创建存储系统
    GisStorageSystem storage("./output");
    
    // 从Shapefile转换数据
    storage.convertFromShapefile("./data/test.shp");
    
    // 批量读取几何数据
    std::vector<uint64_t> feature_ids = {1, 2, 3, 4, 5};
    auto geometries = storage.readGeometries(feature_ids);
    auto attributes = storage.readAttributes(feature_ids);
    
    return 0;
}
```

### 混合索引使用

```cpp
#include "gisindex/hybrid_index.h"

int main() {
    // 创建混合索引
    HybridIndex hybrid_index("./data/test.gdb");
    
    // 构建索引
    hybrid_index.buildIndex();
    
    // 执行查询
    BBox query_bbox = {103.2504, 26.4297, 103.3028, 26.4747};
    auto results = hybrid_index.query(query_bbox);
    
    return 0;
}
```

## 📊 性能特点

### 空间索引性能
- **S2索引**: 全球范围查询，层级可调，适合大规模数据
- **R树索引**: 局部查询性能优异，内存占用适中
- **混合索引**: 结合两者优势，查询性能最优

### 存储系统性能
- **坐标压缩**: 差分编码可减少50%存储空间
- **读取性能**: 随机访问O(1)时间复杂度
- **并发处理**: 支持多线程并行读写
- **内存效率**: 智能缓存，最小化内存占用

## 🧪 测试与验证

### 运行测试
```bash
cd build
./s2index_test          # S2索引测试
./gis_storage_test      # 存储系统测试
./file_io_performance_test  # 性能测试
```

### 性能基准
- **索引构建**: 100万要素约需30秒
- **空间查询**: 平均查询时间<10ms
- **数据压缩**: 坐标数据压缩率50-70%
- **并发性能**: 8线程下性能提升6-8倍

## 🔧 配置选项

### CMake配置选项
```bash
# 启用调试模式
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 启用测试
cmake .. -DBUILD_TESTS=ON

# 指定安装路径
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local

# 启用优化
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native"
```

## 🐛 故障排除

### 常见问题

1. **编译错误**: 确保C++17支持
   ```bash
   export CXXFLAGS="-std=c++17"
   ```

2. **依赖库缺失**: 检查依赖库安装
   ```bash
   pkg-config --exists gdal && echo "GDAL found" || echo "GDAL missing"
   ```

3. **内存不足**: 调整系统限制
   ```bash
   ulimit -s unlimited
   ```

## 📈 开发计划

### 近期计划
- [ ] 支持更多空间索引算法（H3、Geohash）
- [ ] 添加空间分析功能
- [ ] 优化内存使用和缓存策略
- [ ] 增加更多数据格式支持

### 长期计划
- [ ] 分布式索引支持
- [ ] 实时数据更新
- [ ] 机器学习集成
- [ ] Web服务接口

## 📄 许可证

本项目遵循与主项目相同的许可证。

## 🤝 贡献指南

欢迎提交Issue和Pull Request来改进项目。请确保：
1. 代码符合项目的编码规范
2. 添加适当的测试用例
3. 更新相关文档

## 📞 联系方式

如有问题或建议，请通过以下方式联系：
- 提交GitHub Issue
- 发送邮件至项目维护者

---

*最后更新: 2025年1月*
