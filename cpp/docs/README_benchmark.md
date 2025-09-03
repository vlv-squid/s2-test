# C++ Spatial Index Benchmark 工具

## 📋 概述

这是一个基于官方S2几何库的C++空间索引性能测试工具，用于评估不同空间索引算法（S2、R树、OGR）的性能表现。该工具提供了全面的性能测试和详细的统计分析。

## 🚀 主要特性

### 测试内容
- **索引构建性能**: 测试S2和R树索引的构建时间
- **空间查询性能**: 测试S2、R树和OGR的空间查询性能
- **内存使用分析**: 分析不同索引的内存占用情况
- **多规模测试**: 支持不同数据规模的性能测试
- **统计分析**: 提供详细的统计信息（平均值、标准差、最小值、最大值）

### 支持的索引类型
- **S2索引**: 基于Google S2几何库的全球空间索引
- **R树索引**: 经典的空间索引结构
- **OGR索引**: GDAL/OGR内置的空间过滤器

## 🛠️ 编译与安装

### 系统要求
- **编译器**: GCC 7.0+ 或 Clang 5.0+
- **C++标准**: C++17 或更高版本
- **依赖库**: 
  - S2 Geometry Library
  - GDAL/OGR
  - Boost.Geometry

### 编译步骤

```bash
# 1. 进入cpp目录
cd cpp

# 2. 创建构建目录
mkdir build && cd build

# 3. 配置CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# 4. 编译
make -j$(nproc)

# 5. 验证编译
ls -la spatial_index_benchmark
```

### 依赖安装

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install libgdal-dev libboost-all-dev libs2-dev
```

#### CentOS/RHEL
```bash
sudo yum install gdal-devel boost-devel
# 需要手动安装S2库
```

## 📖 使用方法

### 基本用法

```bash
# 运行benchmark测试
./spatial_index_benchmark <data_path> [output_file]

# 示例
./spatial_index_benchmark ../data/test.gdb ./benchmark_results.json
```

### 参数说明

| 参数 | 描述 | 默认值 |
|------|------|--------|
| `data_path` | 输入数据路径（必需） | - |
| `output_file` | 输出JSON文件路径 | `./benchmark_results.json` |

### 支持的数据格式
- **GeoDatabase (.gdb)**: ESRI File Geodatabase
- **Shapefile (.shp)**: ESRI Shapefile
- **其他OGR支持格式**: 所有GDAL/OGR支持的矢量数据格式

## 📊 测试内容详解

### 1. 索引构建测试
- **测试目标**: 比较S2和R树索引的构建性能
- **测试规模**: [100, 500, 1000, 5000, 10000] 要素
- **重复次数**: 10次
- **性能指标**: 构建时间（毫秒）、内存使用（MB）

### 2. 空间查询测试
- **测试目标**: 比较S2、R树和OGR的空间查询性能
- **查询范围**: 3个不同大小的边界框（小、中、大）
- **测试规模**: [100, 500, 1000, 5000, 10000] 要素
- **重复次数**: 10次
- **性能指标**: 查询时间（毫秒）、结果数量

### 3. 内存使用分析
- **S2索引**: 计算S2点和单元格的内存占用
- **R树索引**: 计算R树节点的内存占用
- **总体统计**: 总内存使用和每要素内存使用

## 📈 输出结果

### JSON格式输出

```json
[
  {
    "index_type": "s2",
    "operation": "build",
    "data_size": 1000,
    "avg_time_ms": 15.23,
    "min_time_ms": 14.56,
    "max_time_ms": 16.78,
    "std_dev_ms": 0.45,
    "result_count": 0,
    "memory_usage_mb": 2.34,
    "iterations": 10
  },
  {
    "index_type": "s2",
    "operation": "query",
    "data_size": 1000,
    "avg_time_ms": 8.45,
    "min_time_ms": 7.89,
    "max_time_ms": 9.12,
    "std_dev_ms": 0.23,
    "result_count": 156,
    "memory_usage_mb": 2.34,
    "iterations": 10
  }
]
```

### 控制台输出示例

```
Starting Spatial Index Benchmark...
Data path: ../data/test.gdb
Output file: ./benchmark_results.json
Loaded 5000 features
Built S2 index with 1247 cells
Built R-tree index with 5000 nodes
Running benchmark: s2 build scale=100
Running benchmark: s2 build scale=500
...

=== Benchmark Results ===
s2 build scale=100: 2.34ms (±0.12ms) memory=0.45MB
s2 query scale=100: 1.23ms (±0.08ms) memory=0.45MB results=23
rtree build scale=100: 1.67ms (±0.09ms) memory=0.32MB
rtree query scale=100: 0.89ms (±0.05ms) memory=0.32MB results=23
ogr query scale=100: 5.67ms (±0.34ms) memory=0.00MB results=23
...
Results saved to: ./benchmark_results.json
Benchmark completed successfully!
```

## 📊 可视化分析

### 使用Python可视化工具

```bash
# 安装依赖
pip install matplotlib numpy pandas

# 生成基础图表
python3 ../../../py/performance/cpp_benchmark_visualizer.py ./benchmark_results.json

# 生成详细分析
python3 ../../../py/performance/cpp_benchmark_visualizer.py ./benchmark_results.json --detailed

# 生成总结报告
python3 ../../../py/performance/cpp_benchmark_visualizer.py ./benchmark_results.json --summary

# 指定输出目录
python3 ../../../py/performance/cpp_benchmark_visualizer.py ./benchmark_results.json --output-dir ./results
```

### 生成的图表类型

1. **性能对比图表** (`cpp_benchmark_charts.png`)
   - 索引构建时间对比
   - 空间查询时间对比
   - 内存使用对比
   - 性能 vs 数据规模

2. **详细分析图表** (`cpp_benchmark_detailed_analysis.png`)
   - 平均构建时间对比
   - 平均查询时间对比
   - 内存效率对比
   - 相对于OGR的性能提升
   - 查询时间稳定性分析
   - 查询结果数量对比

3. **总结报告** (`cpp_benchmark_summary.txt`)
   - 总体统计信息
   - 性能分析结果
   - 使用建议

## 🔧 配置选项

### 修改测试参数

在 `spatial_index_benchmark.cpp` 中可以修改以下参数：

```cpp
// 测试规模
test_scales_ = {100, 500, 1000, 5000, 10000};

// 重复次数
iterations_ = 10;

// 查询边界框
query_bboxes_ = {
    {26.4297, 103.2504, 26.4747, 103.3028, "small"},
    {26.4000, 103.2000, 26.5000, 103.3500, "medium"},
    {26.3000, 103.0000, 26.6000, 103.5000, "large"}
};
```

### S2索引配置

```cpp
// S2层级设置
void buildS2Index(int level = 15)  // 默认层级15
```

## 🐛 故障排除

### 常见问题

1. **编译错误**: 确保S2库正确安装
   ```bash
   # 检查S2库
   pkg-config --exists s2 && echo "S2 found" || echo "S2 missing"
   ```

2. **数据加载失败**: 检查数据格式和路径
   ```bash
   # 检查GDAL支持
   ogrinfo --formats | grep -i gdb
   ```

3. **内存不足**: 减少测试规模
   ```cpp
   test_scales_ = {100, 500, 1000};  // 减少测试规模
   ```

4. **性能异常**: 检查系统负载
   ```bash
   # 关闭不必要的进程
   # 使用SSD存储
   # 确保足够内存
   ```

### 性能优化建议

1. **硬件优化**
   - 使用SSD存储测试数据
   - 确保足够的内存（8GB+）
   - 使用多核CPU

2. **系统优化**
   - 关闭不必要的后台进程
   - 调整系统内存限制
   - 使用Release编译模式

3. **测试优化**
   - 多次运行取平均值
   - 在相同环境下测试
   - 避免系统负载波动

## 📈 性能基准

### 典型测试结果

| 索引类型 | 构建时间 (1000要素) | 查询时间 (1000要素) | 内存使用 (1000要素) |
|----------|-------------------|-------------------|-------------------|
| S2索引 | 15-25ms | 5-10ms | 2-3MB |
| R树索引 | 10-15ms | 3-8ms | 1-2MB |
| OGR查询 | - | 20-40ms | 0MB |

### 性能提升

相对于OGR的空间查询性能提升：
- **S2索引**: 60-80% 性能提升
- **R树索引**: 70-90% 性能提升

## 📚 参考资料

- [S2 Geometry Library](https://s2geometry.io/)
- [GDAL/OGR Documentation](https://gdal.org/)
- [Boost.Geometry](https://www.boost.org/doc/libs/master/libs/geometry/)

## 🤝 贡献指南

欢迎提交Issue和Pull Request来改进工具。请确保：
1. 代码符合项目的编码规范
2. 添加适当的测试用例
3. 更新相关文档
4. 通过所有测试

---

*最后更新: 2025年1月*
