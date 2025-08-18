# C++ GIS自定义格式存储系统

## 概述

这是一个用C++实现的GIS自定义格式存储系统，对应Python版本的`py/gisstorage`模块。该系统提供了高效的几何数据和属性数据的二进制存储格式，支持坐标压缩、索引缓存和多线程读取。

## 主要功能

### 1. 数据结构

#### GeometryData（几何数据）
- **要素ID**: 64位无符号整数
- **几何类型**: 枚举类型（点、线、面等）
- **压缩坐标**: 差分编码的坐标数据
- **边界框**: 几何对象的边界框

#### AttributeData（属性数据）
- **要素ID**: 64位无符号整数
- **属性字典**: 键值对形式的属性数据

### 2. 核心特性

#### 坐标压缩
- **差分编码**: 第一个点存储绝对坐标，后续点存储相对于前一个点的偏移量
- **压缩率**: 通常可达到50%的压缩率
- **精度保持**: 使用float类型存储偏移量，在保证精度的同时节省空间

#### 二进制格式
- **几何数据格式**: `feature_id(8) + geometry_type(1) + bbox(32) + coord_size(4) + coordinates`
- **属性数据格式**: `feature_id(8) + json_length(4) + json_data`

#### 索引缓存
- **偏移索引**: 构建要素ID到文件偏移的映射
- **缓存机制**: 自动缓存索引，提高读取性能
- **缓存清理**: 写入新数据时自动清除缓存

#### 多线程支持
- **并行读取**: 支持批量并行读取几何和属性数据
- **线程安全**: 使用RAII和智能指针保证线程安全

## 文件结构

```
cpp/
├── include/
│   └── gis_storage.h          # 主要头文件
├── src/
│   └── gis_storage.cpp        # 实现文件
├── test/
│   └── gis_storage_test.cpp   # 测试文件
└── CMakeLists.txt             # 构建配置
```

## 编译和运行

### 依赖项
- C++17 或更高版本
- Boost.Geometry
- GDAL（可选，用于Shapefile支持）

### 编译
```bash
cd cpp
mkdir build && cd build
cmake ..
make
```

### 运行测试
```bash
./gis_storage_test
```

## 使用示例

### 基本使用

```cpp
#include "gis_storage.h"
using namespace GisStorage;

// 创建几何数据
std::vector<Coordinate> coordinates = {
    {103.2504, 26.4297},
    {103.2604, 26.4397},
    {103.2704, 26.4497}
};

// 压缩坐标
std::vector<uint8_t> compressed = GeometrySerializer::encodeCoordinatesDelta(coordinates);
BBox bbox = GeometrySerializer::calculateBBox(coordinates);

GeometryData geom(12345, GeometryType::LINE, compressed, bbox);

// 创建属性数据
std::map<std::string, std::string> properties = {
    {"name", "测试要素"},
    {"type", "道路"}
};
AttributeData attr(12345, properties);

// 存储数据
GeometryStorage geom_storage("./output/geom.dat");
AttributeStorage attr_storage("./output/attr.dat");

int64_t geom_offset = geom_storage.writeGeometry(geom);
int64_t attr_offset = attr_storage.writeAttribute(attr);

// 读取数据
auto read_geom = geom_storage.readGeometry(12345);
auto read_attr = attr_storage.readAttribute(12345);

// 解码坐标
std::vector<Coordinate> decoded = read_geom->decodeCoordinates();
```

### 批量操作

```cpp
// 创建存储系统
GisStorageSystem system("./output");

// 批量读取几何数据
std::vector<uint64_t> feature_ids = {1, 2, 3, 4, 5};
auto geometries = system.readGeometries(feature_ids);
auto attributes = system.readAttributes(feature_ids);

// 处理结果
for (const auto& pair : geometries) {
    uint64_t fid = pair.first;
    const auto& geom = pair.second;
    std::cout << "要素 " << fid << " 坐标数量: " 
              << geom->decodeCoordinates().size() << std::endl;
}
```

## 性能特点

### 存储效率
- **坐标压缩**: 差分编码可减少50%的存储空间
- **二进制格式**: 比文本格式更紧凑
- **索引优化**: 快速定位要素数据

### 读取性能
- **随机访问**: O(1)时间复杂度的要素访问
- **批量读取**: 多线程并行处理
- **缓存机制**: 减少重复的文件扫描

### 内存使用
- **智能指针**: 自动内存管理
- **RAII**: 资源自动获取和释放
- **流式处理**: 支持大文件处理

## 与Python版本对比

| 特性       | Python版本 | C++版本 |
| ---------- | ---------- | ------- |
| 坐标压缩   | ✅          | ✅       |
| 二进制格式 | ✅          | ✅       |
| 索引缓存   | ✅          | ✅       |
| 多线程读取 | ✅          | ✅       |
| 内存效率   | 中等       | 高      |
| 执行速度   | 中等       | 高      |
| 开发便利性 | 高         | 中等    |

## 扩展功能

### 待实现功能
1. **JSON解析**: 集成JSON库（如nlohmann/json）完善属性数据解析
2. **Shapefile转换**: 集成GDAL/OGR库实现Shapefile到自定义格式的转换
3. **空间索引**: 集成R-tree或S2索引支持空间查询
4. **数据验证**: 添加数据完整性检查和修复功能

### 优化方向
1. **内存映射**: 使用mmap提高大文件读取性能
2. **压缩算法**: 支持更多压缩算法（如LZ4、Zstandard）
3. **并发写入**: 支持多线程并发写入
4. **增量更新**: 支持数据的增量更新和版本管理

## 注意事项

1. **字节序**: 当前实现假设小端字节序，跨平台使用时需要注意
2. **精度**: 差分编码使用float类型，可能影响高精度坐标
3. **文件格式**: 二进制格式不兼容，需要专门的转换工具
4. **错误处理**: 完善的异常处理机制，确保数据完整性

## 许可证

本项目遵循与主项目相同的许可证。
