# Python版本与C++版本兼容性说明

## 概述

Python版本的`gisstorage`模块已经完全与C++版本兼容，确保两个版本生成的文件可以互相解析。

## 兼容性特性

### 1. 字符串池格式完全兼容

- **序列化格式**: 字符串数量(4字节) + 每个字符串的长度(4字节) + 字符串内容
- **反序列化**: 完全按照C++版本的格式进行解析
- **字符串ID映射**: 与C++版本保持一致

### 2. 几何数据序列化格式完全兼容

- **头部格式**: feature_id(8字节) + geometry_type(1字节) + bbox(32字节) + coord_size(4字节)
- **坐标压缩**: 使用与C++版本相同的差分编码算法
- **数据类型选择**: 根据偏移量范围自动选择short/int/float类型
- **精度保持**: 对于小偏移量使用float类型以保持精度

### 3. 属性数据序列化格式完全兼容

- **头部格式**: feature_id(8字节) + 属性数量(4字节)
- **属性对格式**: key_id(4字节) + value_id(4字节)
- **字符串池引用**: 使用字符串ID而不是直接存储字符串
- **压缩统计**: 与C++版本保持一致的统计信息

### 4. 文件存储格式完全兼容

- **几何文件**: 二进制格式，与C++版本完全一致
- **属性文件**: 二进制格式，使用字符串池优化
- **字符串池文件**: 二进制格式，存储所有唯一字符串
- **索引文件**: JSON格式，包含要素偏移信息

### 5. 索引文件格式完全兼容

```json
{
  "version": 1,
  "data": {
    "features": {
      "12345": {
        "geom_offset": 0,
        "attr_offset": 0
      }
    }
  }
}
```

## 主要修改

### 1. 移除了JSON字符串处理方式

- 删除了原始的JSON字符串直接处理属性的方式
- 完全采用字符串池优化方式

### 2. 更新了数据结构

- `GeometryType`: 使用枚举类型，与C++版本保持一致
- `StringPool`: 新增字符串池类，实现与C++版本相同的功能
- `AttributeData`: 属性类型限制为字符串，与C++版本一致

### 3. 更新了序列化器

- `GeometrySerializer`: 完全重写，与C++版本格式一致
- `AttributeSerializer`: 改为使用字符串池，移除JSON处理

### 4. 更新了存储类

- `GeometryStorage`: 保持与C++版本相同的文件格式
- `AttributeStorage`: 支持字符串池，构造函数需要两个参数

### 5. 更新了转换器

- `ShapefileConverter`: 与C++版本保持一致的转换逻辑
- 支持字符串池优化
- 生成与C++版本兼容的文件格式

## 文件格式说明

### 几何文件格式
```
[feature_id:8][geometry_type:1][bbox:32][coord_size:4][compressed_coordinates]
```

### 属性文件格式
```
[feature_id:8][prop_count:4][key_id:4][value_id:4]...
```

### 字符串池文件格式
```
[string_count:4][string1_length:4][string1_content][string2_length:4][string2_content]...
```

### 索引文件格式
```json
{
  "version": 1,
  "data": {
    "features": {
      "fid": {
        "geom_offset": 0,
        "attr_offset": 0
      }
    }
  }
}
```

## 使用方法

### Python版本
```python
from gisstorage.converter import ShapefileConverter

# 转换Shapefile
converter = ShapefileConverter("input.shp", "output_dir")
valid_fids = converter.convert()

# 读取数据
from gisstorage.storage import GeometryStorage, AttributeStorage

geom_storage = GeometryStorage("output_dir/test_geom.dat")
attr_storage = AttributeStorage("output_dir/test_attr.dat", "output_dir/test_pool.dat")

# 加载索引
geom_storage.load_index_from_file("output_dir/test_index.dat")
attr_storage.load_index_from_file("output_dir/test_index.dat")

# 读取要素
geometry = geom_storage.read_geometry(12345)
attribute = attr_storage.read_attribute(12345)
```

### C++版本
```cpp
#include "gisstorage/shapefile_converter.h"
#include "gisstorage/geometry_storage.h"
#include "gisstorage/attribute_storage.h"

// 转换Shapefile
ShapefileConverter converter("input.shp", "output_dir");
auto valid_fids = converter.convert();

// 读取数据
GeometryStorage geom_storage("output_dir/test_geom.dat");
AttributeStorage attr_storage("output_dir/test_attr.dat", "output_dir/test_pool.dat");

// 加载索引
geom_storage.loadIndexFromFile("output_dir/test_index.dat");
attr_storage.loadIndexFromFile("output_dir/test_index.dat");

// 读取要素
auto geometry = geom_storage.readGeometry(12345);
auto attribute = attr_storage.readAttribute(12345);
```

## 测试验证

运行兼容性测试：
```bash
python3 py/test_compatibility.py
```

测试包括：
1. 字符串池兼容性测试
2. 几何序列化兼容性测试
3. 属性序列化兼容性测试
4. 文件格式兼容性测试
5. 二进制格式兼容性测试

## 注意事项

1. **精度**: 几何坐标使用float类型存储偏移量时，可能存在微小的精度损失（通常在1e-4范围内）
2. **字符串编码**: 所有字符串使用UTF-8编码
3. **字节序**: 使用系统默认字节序（通常是小端序）
4. **内存对齐**: 与C++版本保持一致的内存对齐方式

## 总结

Python版本已经完全与C++版本兼容，两个版本生成的文件可以互相解析，确保了跨语言的数据交换能力。
