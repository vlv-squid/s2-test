# S2空间索引系统 - C++版本

## 概述

这是一个基于Google S2几何库的高性能空间索引系统，专为GIS数据设计。系统使用二进制文件格式存储索引，提供快速的空间查询能力。

## 架构特点

### 核心组件

1. **S2空间索引** (`gisindex/s2spatial_index.h`)
   - 基于Google S2几何库
   - 支持多层级空间索引
   - 高效的球面几何计算

2. **二进制序列化** (`gisindex/serialize_s2.h`)
   - 高效的二进制文件格式
   - 支持快速读写操作
   - 无外部数据库依赖

3. **GIS存储系统** (`gisstorage/`)
   - 几何数据存储
   - 属性数据管理
   - 字符串池优化

### 设计原则

- **简化架构**: 移除复杂的混合索引，专注于S2索引
- **高性能**: 使用二进制格式，避免SQLite开销
- **易维护**: 清晰的模块化设计，易于扩展

## 构建说明

### 依赖要求

- CMake 3.28.3+
- C++17 编译器
- GDAL 3.x
- Google S2 几何库
- Boost Geometry
- Google Test

### 构建步骤

```bash
cd cpp
mkdir -p build && cd build
cmake ..
make
```

### 可执行文件

- `s2index_test`: S2索引功能测试
- `gis_storage_test`: GIS存储系统测试
- `file_io_performance_test`: 文件I/O性能测试
- `spatial_index_benchmark`: 空间索引性能基准测试

## 使用方法

### 基本索引操作

```cpp
#include "gisindex/s2spatial_index.h"

// 创建S2索引
S2SpatialIndex index("path/to/index.idx", 16);

// 构建索引
std::vector<std::pair<int64_t, int>> entries;
// ... 填充数据 ...
index.build(entries);

// 保存索引
index.save();

// 查询
S2LatLngRect queryRect = /* 查询范围 */;
auto results = index.query(queryRect, 16);
```

### 索引文件格式

索引文件使用二进制格式存储：

```
[索引条目数量: size_t]
[Cell ID: int64_t][要素ID数量: size_t][要素ID列表: int[]]
...
```

## 性能特点

- **查询性能**: 相比GDAL顺序扫描，性能提升显著
- **内存效率**: 优化的数据结构，内存占用合理
- **I/O性能**: 二进制格式，读写速度快

## 测试

运行测试套件：

```bash
# 运行所有测试
make test

# 运行特定测试
./s2index_test
./gis_storage_test
```

## 扩展性

系统设计为模块化架构，可以轻松添加：

- 新的空间索引算法
- 不同的序列化格式
- 额外的GIS功能

## 注意事项

- 索引文件路径需要确保有写入权限
- S2层级设置影响索引精度和性能
- 大数据集建议使用适当的S2层级设置
