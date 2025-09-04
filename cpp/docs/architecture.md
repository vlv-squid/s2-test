# 系统架构

## 🏗️ 整体架构

GIS存储系统采用模块化设计，主要包含以下核心模块：

### 1. GIS索引模块 (gisindex)
- **S2空间索引** - 基于Google S2几何库的高性能空间索引
- **序列化支持** - 高效的二进制序列化/反序列化

### 2. GIS存储模块 (gisstorage)
- **几何数据存储** - 高效的几何数据存储和管理
- **属性数据管理** - 属性数据的存储和查询
- **字符串池优化** - 字符串数据的去重和优化
- **Shapefile转换** - Shapefile格式的转换支持

### 3. 存储系统 (GisStorageSystem)
- **统一接口** - 提供统一的存储和查询接口
- **元数据管理** - 数据集元数据的管理
- **文件扩展名管理** - 标准化文件扩展名定义

## 📁 模块依赖关系

```
GisStorageSystem
├── GeometryStorage
├── AttributeStorage
├── StringPool
├── ShapefileConverter
└── S2SpatialIndex
    └── SerializeS2
```

## 🔧 核心组件

### S2空间索引
- 支持多层级空间索引
- 高效的球面几何计算
- 二进制文件格式存储

### 数据存储
- 几何数据：`.geom` 文件
- 属性数据：`.attr` 文件
- 字符串池：`.pool` 文件
- 索引数据：`.idx` 文件
- 元数据：`_meta.json` 文件
- S2索引：`.s2idx` 文件

### 文件格式
- 二进制格式，高效读写
- 支持大数据集
- 无外部数据库依赖
