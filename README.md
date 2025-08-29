# S2-Test: 高性能GIS空间索引与存储系统

## 📋 项目概述

S2-Test是一个高性能的地理空间数据处理系统，提供C++和Python两个版本的实现。系统集成了多种空间索引算法（S2、R树、H3、Geohash）和自定义二进制存储格式，专为大规模地理空间数据处理而设计。

### 🎯 核心特性
- **多算法空间索引**: S2、R树、H3、Geohash等多种索引算法
- **高效存储格式**: 自定义二进制格式，支持坐标压缩和字符串池优化
- **跨语言实现**: C++高性能实现 + Python易用接口
- **性能优化**: 多线程并行处理，智能缓存机制
- **可视化支持**: 查询结果可视化展示

## 🏗️ 系统架构

### C++模块 (`cpp/`)
高性能的C++实现，适用于大规模数据处理和性能关键场景。

#### 核心组件
- **空间索引模块** (`gisindex/`): S2、R树、混合索引
- **存储系统模块** (`gisstorage/`): 几何数据、属性数据存储
- **序列化支持**: 索引数据持久化存储
- **Shapefile转换**: 支持Shapefile到自定义格式转换

#### 主要特性
- 基于Google S2几何库的全球空间索引
- 高效的几何数据二进制存储格式
- 坐标差分编码压缩，减少50%存储空间
- 多线程并发空间查询支持

### Python模块 (`py/`)
易用的Python实现，提供丰富的API和可视化功能。

#### 核心组件
- **空间索引模块** (`gisindex/`): S2、R树、H3、Geohash索引
- **存储系统模块** (`gisstorage/`): 数据存储和序列化
- **性能测试模块** (`performance/`): 综合性能测试和可视化

#### 主要特性
- 多种空间索引算法支持
- 自动索引策略优化
- 查询结果可视化展示
- 与GDAL/OGR等标准格式的性能对比

## 🚀 快速开始

### 环境配置

#### 1. 创建conda环境
```bash
# 进入环境配置文件夹
cd environment-config

# 创建环境
conda env create -f gis-dev-environment.yml

# 激活环境
conda activate gis-dev
```

#### 2. 验证安装
```bash
python -c "import s2, gdal, geopandas; print('✓ 环境配置成功')"
```

### C++编译

```bash
# 进入C++目录
cd cpp

# 创建构建目录
mkdir build && cd build

# 配置和编译
cmake ..
make -j$(nproc)

# 运行测试
./test/s2index_test
./test/gis_storage_test
```

### Python使用

```bash
# 进入Python目录
cd py

# 运行空间索引测试
python gisindex/runner.py

# 运行存储系统测试
python gisstorage/runner.py

# 运行性能测试
python performance/benchmark.py
```

## 📁 项目结构

```
s2-test/
├── cpp/                    # C++实现
│   ├── include/           # 头文件
│   ├── src/              # 源代码
│   ├── test/             # 测试文件
│   └── CMakeLists.txt    # 构建配置
├── py/                    # Python实现
│   ├── gisindex/         # 空间索引模块
│   ├── gisstorage/       # 存储系统模块
│   └── performance/      # 性能测试模块
├── environment-config/    # 环境配置
│   ├── gis-dev-environment.yml
│   └── README.md
├── data/                  # 测试数据
├── index/                 # 索引文件
├── output_data/          # 输出数据
└── README.md            # 本文档
```

## 🔧 关键依赖

### 环境配置
- **Python**: 3.9+
- **C++**: C++17+
- **编译器**: GCC 7.0+ 或 Clang 5.0+

### 核心包
- **S2相关**: s2==0.1.9, s2-py==0.11.0, s2geometry==0.9.0
- **GIS核心**: GDAL=3.3.2, Fiona=1.8.20, Geopandas=1.0.1
- **空间索引**: Rtree, H3==4.3.0
- **数据处理**: NumPy=1.26.4, Pandas=1.4.2, SciPy

## 📊 性能特性

### 空间索引性能
- **S2索引**: 全球空间索引，适用于大规模数据
- **R树索引**: 经典空间索引，适用于局部查询
- **H3索引**: 六边形网格索引，适用于地理分析
- **混合索引**: 结合多种索引优势，自动优化策略

### 存储性能
- **坐标压缩**: 差分编码，减少50%存储空间
- **二进制格式**: 高效读写，支持随机访问
- **字符串池**: 字符串去重，减少内存占用
- **索引缓存**: 智能缓存，提高查询性能

## 🧪 测试与验证

### 性能测试
```bash
# Python性能测试
cd py/performance
python benchmark.py

# C++性能测试
cd cpp/test
./file_io_performance_test
```

### 功能测试
```bash
# Python功能测试
cd py
python gisindex/index_tester.py
python gisstorage/runner.py

# C++功能测试
cd cpp/test
./s2index_test
./gis_storage_test
```

## 📈 使用示例

### Python示例
```python
from gisindex.s2_index import S2SpatialIndex
from gisstorage.storage import GeometryStorage

# 创建S2索引
s2_index = S2SpatialIndex("data.shp", resolution=15)
s2_index.build_index()

# 空间查询
bbox = (103.0, 26.0, 104.0, 27.0)
results = s2_index.query_by_bbox(bbox)

# 存储系统
storage = GeometryStorage("output.dat")
storage.write_geometry(geometry_data)
```

### C++示例
```cpp
#include "gisindex/s2spatial_index.h"
#include "gisstorage/gis_storage.h"

// 创建S2索引
S2SpatialIndex index("data.shp", 15);
index.build_index();

// 空间查询
std::vector<double> bbox = {103.0, 26.0, 104.0, 27.0};
auto results = index.query_by_bbox(bbox);

// 存储系统
GISStorage storage("output.dat");
storage.write_geometry(geometry_data);
```

## 🤝 贡献指南

1. Fork项目
2. 创建功能分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 打开Pull Request

## 📄 许可证

本项目采用MIT许可证 - 查看 [LICENSE](LICENSE) 文件了解详情。

## 📞 联系方式

如有问题或建议，请通过以下方式联系：
- 提交Issue
- 发送邮件
- 项目讨论区

---

**注意**: 本项目仍在积极开发中，API可能会有变化。建议在生产环境使用前进行充分测试。
