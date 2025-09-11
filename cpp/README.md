# GIS存储系统 - C++版本

## 📋 项目概述

这是一个基于Google S2几何库的高性能GIS数据存储和索引系统。系统提供完整的Shapefile/FileGDB转换、数据存储、空间索引和元数据管理功能，专为大规模GIS数据处理设计。支持多种GIS格式的转换，包括Shapefile、FileGDB等，并提供高效的S2空间索引和属性查询功能。

## 🏗️ 系统架构

### 核心模块

1. **GIS索引模块** (`gisindex/`)
   - S2空间索引 - 基于Google S2几何库的高性能空间索引
   - 二进制序列化 - 高效的索引数据序列化/反序列化
   - 多层级索引支持 - 支持不同精度的空间索引

2. **GIS存储模块** (`gisstorage/`)
   - 几何数据存储 - 高效的几何数据存储和管理
   - 属性数据管理 - 属性数据的存储和查询
   - 字符串池优化 - 字符串数据的去重和优化
   - OGR格式转换 - 支持Shapefile、FileGDB等多种格式转换
   - 统一存储系统 - 提供统一的存储和查询接口
   - 逆向转换 - 支持将自定义格式转换回标准GIS格式

### 设计特点

- **高性能**: 使用二进制格式，避免数据库开销
- **模块化**: 清晰的模块化设计，易于扩展和维护
- **完整性**: 提供从数据转换到索引构建的完整工作流
- **标准化**: 统一的文件扩展名和元数据格式

## 📁 项目结构

```
cpp/
├── include/              # 头文件
│   ├── gisindex/        # GIS索引模块头文件
│   └── gisstorage/      # GIS存储模块头文件
├── src/                 # 源代码
│   ├── gisindex/        # GIS索引模块实现
│   └── gisstorage/      # GIS存储模块实现
├── tests/               # 测试代码
│   ├── unit/            # 单元测试
│   ├── integration/     # 集成测试
│   └── performance/     # 性能测试
├── examples/            # 示例程序
├── benchmarks/          # 基准测试
├── tools/               # 工具脚本
│   ├── build/           # 构建工具
│   ├── demo/            # 演示工具
│   └── benchmark/       # 基准测试工具
└── docs/                # 文档
    ├── architecture.md  # 系统架构
    ├── build.md         # 构建指南
    ├── test.md          # 测试指南
    └── benchmark.md     # 基准测试指南
```

## 🚀 快速开始

### 1. 构建项目

```bash
# 使用构建脚本（推荐）
./tools/build/build.sh --all

# 或使用CMake
mkdir build && cd build
cmake ../cpp
make
```

### 2. 运行测试

```bash
# 使用CTest运行所有测试
cd build
ctest

# 运行特定测试
cd build/tests
./s2index_test
./gis_storage_test
./integrated_gis_format_test
```

### 3. 运行示例

```bash
# 运行完整演示程序
cd examples
./gis_storage_demo ../../data/test.shp ./output

# 或使用演示脚本
./tools/demo/run_demo.sh ../../data/test.shp ./output
```

## 📦 可执行文件

### 测试程序
- `s2index_test` - S2索引功能测试
- `gis_storage_test` - GIS存储系统测试
- `reverse_conversion_test` - 逆向转换测试
- `integrated_gis_format_test` - 集成测试
- `file_io_performance_test` - 文件I/O性能测试
- `gdal_filegdb_performance_test` - GDAL FileGDB性能测试
- `custom_format_performance_test` - 自定义格式性能测试

### 基准测试
- `spatial_index_benchmark` - 空间索引性能基准测试

### 示例程序
- `gis_storage_demo` - 完整演示程序，展示完整工作流

## 💻 使用方法

### 基本使用流程

```cpp
#include "gisstorage/gis_storage_system.h"
#include "gisstorage/ogr_format_converter.h"

// 1. 转换OGR格式
GisStorage::OGRFormatConverter converter("input.shp", "output_dir");
auto feature_ids = converter.Convert();

// 2. 创建存储系统
GisStorage::GisStorageSystem storage("output_dir");
storage.InitializeStorageFiles("input.shp");

// 3. 构建S2索引
storage.InitializeS2Index(15);
storage.BuildS2IndexFromDataset("input.shp", 1000);

// 4. 生成和保存元数据
storage.UpdateFileSizes();
storage.UpdateChecksums();
storage.SaveMetadata();

// 5. 查询数据
auto features = storage.QueryS2Index(bbox, 15);
auto geometry = storage.ReadGeometry(feature_id);
auto attributes = storage.ReadAttribute(feature_id);
```

### 文件格式

系统生成以下文件：
- `.geom` - 几何数据文件
- `.attr` - 属性数据文件
- `.pool` - 字符串池文件
- `.idx` - 索引数据文件
- `_meta.json` - 元数据文件
- `.s2idx` - S2索引文件

## 🔧 依赖要求

- CMake 3.28.3+
- C++17 编译器
- GDAL 3.x
- Google S2 几何库
- Boost Geometry
- Google Test
- nlohmann/json
- TBB (Threading Building Blocks)
- absl (Abseil C++库)

## 📊 性能特点

- **查询性能**: 相比GDAL顺序扫描，性能提升显著
- **内存效率**: 优化的数据结构，内存占用合理
- **I/O性能**: 二进制格式，读写速度快
- **扩展性**: 支持大规模数据集处理

## 🛠️ 工具脚本

### 构建工具
```bash
./tools/build/build.sh --help    # 查看构建选项
./tools/build/build.sh --clean   # 清理构建
./tools/build/build.sh --test    # 构建测试
```

### 测试工具
```bash
# 使用CTest运行测试
cd build
ctest                           # 运行所有测试
ctest -L unit                   # 运行单元测试
ctest -L performance            # 运行性能测试
ctest --verbose                 # 详细输出
```

### 演示工具
```bash
./tools/demo/run_demo.sh input.shp output/  # 运行演示程序
```

### 基准测试工具
```bash
./tools/benchmark/run_cpp_benchmark.sh  # 运行基准测试
```

## 📚 文档

详细文档请参考 `docs/` 目录：
- [系统架构](docs/architecture.md) - 系统架构和模块说明
- [构建指南](docs/build.md) - 构建和配置说明
- [演示程序](docs/examples.md) - 使用示例和演示
- [基准测试](docs/benchmark.md) - 性能测试指南

## 🔍 测试

```bash
# 运行所有测试
cd build
ctest

# 运行特定测试
cd build/tests
./s2index_test
./gis_storage_test
./integrated_gis_format_test
./reverse_conversion_test

# 运行性能测试
./file_io_performance_test
./gdal_filegdb_performance_test
./custom_format_performance_test
```

## 📈 扩展性

系统采用模块化设计，可以轻松扩展：
- 新的空间索引算法
- 不同的数据格式支持
- 额外的GIS功能模块
- 自定义存储后端

## ⚠️ 注意事项

- 确保输出目录有写入权限
- S2层级设置影响索引精度和性能
- 大数据集建议使用适当的S2层级设置
- 建议在构建前检查所有依赖项