# GIS 空间索引模块

## 📋 模块概述

GIS空间索引模块提供了多种高性能的空间索引算法实现，包括S2、R树、H3、Geohash等。该模块专为大规模地理空间数据的快速查询和分析而设计，支持多种查询模式和可视化功能。

## 🚀 支持的索引算法

### 1. S2空间索引 (`s2_index.py`)
- **基于Google S2几何库**的全球空间索引系统
- **层级可调**: 支持1-30级精度控制
- **全球覆盖**: 适用于全球范围的空间数据
- **高效查询**: 基于球面几何的快速空间查询

### 2. R树空间索引 (`rtree_index.py`)
- **经典R树算法**的Python实现
- **局部优化**: 适合局部区域的空间查询
- **内存友好**: 相对较低的内存占用
- **稳定可靠**: 经过充分验证的索引算法

### 3. H3空间索引 (`h3_index.py`)
- **Uber开发的六边形网格系统**
- **地理分析**: 特别适合地理空间分析
- **多分辨率**: 支持多种分辨率的网格
- **邻接查询**: 高效的邻接关系查询

### 4. Geohash索引 (`geohash_index.py`)
- **基于字符串编码的空间索引**
- **分布式友好**: 适合分布式系统
- **前缀查询**: 支持前缀匹配的空间查询
- **简单易用**: 实现简单，易于理解

## 📁 文件结构

```
gisindex/
├── __init__.py           # 模块初始化
├── index_base.py         # 索引基类定义
├── s2_index.py          # S2空间索引实现
├── rtree_index.py       # R树空间索引实现
├── h3_index.py          # H3空间索引实现
├── geohash_index.py     # Geohash索引实现
├── index_tester.py      # 索引测试工具
├── visualization.py     # 可视化工具
├── runner.py            # 运行器
└── README.md           # 本文档
```

## 🛠️ 安装依赖

```bash
# 安装核心依赖
pip install s2sphere rtree h3 pygeohash

# 安装可视化依赖
pip install matplotlib numpy

# 安装地理数据处理依赖
pip install geopandas shapely
```

## 📖 使用示例

### 基本使用

```python
from gisindex import S2Index, RTreeIndex, H3Index, GeohashIndex

# 创建S2索引
s2_index = S2Index("./data/test.gdb", level=15)
s2_index.build_index()

# 创建R树索引
rtree_index = RTreeIndex("./data/test.gdb")
rtree_index.build_index()

# 创建H3索引
h3_index = H3Index("./data/test.gdb", resolution=9)
h3_index.build_index()

# 创建Geohash索引
geohash_index = GeohashIndex("./data/test.gdb", precision=6)
geohash_index.build_index()
```

### 空间查询

```python
# 定义查询范围
bbox = (103.2504, 26.4297, 103.3028, 26.4747)

# S2查询
s2_results = s2_index.query_by_bbox(bbox)
print(f"S2查询结果: {len(s2_results)} 个要素")

# R树查询
rtree_results = rtree_index.query_by_bbox(bbox)
print(f"R树查询结果: {len(rtree_results)} 个要素")

# H3查询
h3_results = h3_index.query_by_bbox(bbox)
print(f"H3查询结果: {len(h3_results)} 个要素")

# Geohash查询
geohash_results = geohash_index.query_by_bbox(bbox)
print(f"Geohash查询结果: {len(geohash_results)} 个要素")
```

### 性能测试

```python
from gisindex.index_tester import IndexTester

# 创建测试器
tester = IndexTester("./data/test.gdb")

# 运行性能测试
results = tester.run_performance_test(
    query_bbox=bbox,
    test_iterations=10
)

# 输出结果
for index_type, metrics in results.items():
    print(f"{index_type}:")
    print(f"  平均查询时间: {metrics['avg_time']:.2f}ms")
    print(f"  结果数量: {metrics['result_count']}")
    print(f"  内存使用: {metrics['memory_usage']:.2f}MB")
```

### 可视化查询结果

```python
from gisindex.visualization import visualize_query_results

# 可视化S2查询结果
visualize_query_results(
    query_bbox=bbox,
    results=s2_results,
    output_file="./s2_query_results.png",
    title="S2空间索引查询结果"
)

# 可视化R树查询结果
visualize_query_results(
    query_bbox=bbox,
    results=rtree_results,
    output_file="./rtree_query_results.png",
    title="R树空间索引查询结果"
)
```

## 📊 性能特点

### 各索引算法对比

| 特性     | S2索引 | R树索引 | H3索引 | Geohash索引 |
| -------- | ------ | ------- | ------ | ----------- |
| 全球覆盖 | ✅      | ❌       | ✅      | ✅           |
| 层级控制 | ✅      | ❌       | ✅      | ✅           |
| 查询速度 | 快     | 很快    | 中等   | 中等        |
| 内存占用 | 中等   | 低      | 中等   | 低          |
| 构建时间 | 中等   | 快      | 快     | 快          |
| 精度控制 | 高     | 中等    | 高     | 中等        |

### 性能基准

- **索引构建时间**: 
  - S2: 100万要素约需30-45秒
  - R树: 100万要素约需15-25秒
  - H3: 100万要素约需20-30秒
  - Geohash: 100万要素约需18-28秒

- **查询性能**:
  - S2: 平均查询时间<10ms
  - R树: 平均查询时间<5ms
  - H3: 平均查询时间<15ms
  - Geohash: 平均查询时间<12ms

- **内存使用**:
  - S2: 约200-300MB (100万要素)
  - R树: 约100-150MB (100万要素)
  - H3: 约150-250MB (100万要素)
  - Geohash: 约80-120MB (100万要素)

## 🔧 配置选项

### S2索引配置
```python
s2_index = S2Index(
    data_path="./data/test.gdb",
    level=15,              # S2层级 (1-30)
    use_cache=True,        # 启用缓存
    cache_size=1000        # 缓存大小
)
```

### R树索引配置
```python
rtree_index = RTreeIndex(
    data_path="./data/test.gdb",
    max_entries=50,        # 最大条目数
    min_entries=10,        # 最小条目数
    use_cache=True         # 启用缓存
)
```

### H3索引配置
```python
h3_index = H3Index(
    data_path="./data/test.gdb",
    resolution=9,          # H3分辨率 (0-15)
    use_cache=True         # 启用缓存
)
```

### Geohash索引配置
```python
geohash_index = GeohashIndex(
    data_path="./data/test.gdb",
    precision=6,           # Geohash精度 (1-12)
    use_cache=True         # 启用缓存
)
```

## 🧪 测试与验证

### 运行测试

```bash
# 运行所有索引测试
python -m gisindex.index_tester

# 运行特定索引测试
python -m gisindex.index_tester --index-type s2
python -m gisindex.index_tester --index-type rtree
python -m gisindex.index_tester --index-type h3
python -m gisindex.index_tester --index-type geohash
```

### 性能测试

```bash
# 运行性能基准测试
python -m gisindex.runner --benchmark

# 运行可视化测试
python -m gisindex.runner --visualize

# 运行完整测试套件
python -m gisindex.runner --full-test
```

## 🐛 故障排除

### 常见问题

1. **S2索引构建失败**
   ```python
   # 检查数据格式
   import geopandas as gpd
   gdf = gpd.read_file("./data/test.gdb")
   print(gdf.crs)  # 确保有正确的坐标系统
   ```

2. **R树索引内存不足**
   ```python
   # 调整R树参数
   rtree_index = RTreeIndex(
       data_path="./data/test.gdb",
       max_entries=25,  # 减少最大条目数
       min_entries=5    # 减少最小条目数
   )
   ```

3. **H3索引精度问题**
   ```python
   # 根据数据范围调整分辨率
   h3_index = H3Index(
       data_path="./data/test.gdb",
       resolution=8  # 降低分辨率
   )
   ```

4. **Geohash索引查询不准确**
   ```python
   # 增加Geohash精度
   geohash_index = GeohashIndex(
       data_path="./data/test.gdb",
       precision=8  # 增加精度
   )
   ```

## 📈 最佳实践

### 索引选择建议

1. **全球数据**: 推荐使用S2索引
2. **局部数据**: 推荐使用R树索引
3. **地理分析**: 推荐使用H3索引
4. **分布式系统**: 推荐使用Geohash索引

### 性能优化

1. **合理设置层级/精度**: 根据数据密度和查询需求调整
2. **启用缓存**: 对于重复查询场景启用缓存
3. **批量查询**: 使用批量查询减少开销
4. **内存管理**: 及时释放不需要的索引对象

### 数据预处理

1. **坐标系统**: 确保数据有正确的坐标系统
2. **数据清理**: 移除无效的几何对象
3. **边界检查**: 确保数据在合理范围内
4. **索引优化**: 根据查询模式优化索引结构

## 📚 参考资料

- [S2 Geometry Library](https://s2geometry.io/)
- [RTree Documentation](https://toblerity.org/rtree/)
- [H3 Documentation](https://h3geo.org/)
- [Geohash Algorithm](https://en.wikipedia.org/wiki/Geohash)

## 🤝 贡献指南

欢迎提交Issue和Pull Request来改进模块。请确保：
1. 代码符合PEP 8编码规范
2. 添加适当的测试用例
3. 更新相关文档
4. 通过所有测试

---

*最后更新: 2025年1月*