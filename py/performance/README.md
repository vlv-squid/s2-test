# GIS存储格式性能测试工具

这是一个统一的GIS存储格式性能测试工具，整合了原有的多个性能测试脚本，提供了完整的性能对比分析功能。

## 功能特性

### 1. 综合性能测试
- **顺序读取性能测试**: 测试OGR和自定义格式的顺序读取性能
- **随机读取性能测试**: 测试OGR和自定义格式的随机读取性能
- **空间查询性能测试**: 测试OGR和自定义格式的空间查询性能
- **字符串池压缩统计**: 分析字符串池的压缩效果

### 2. 多种测试模式
- **基础测试 (basic)**: 标准测试，5次重复，测试规模 [100, 500, 1000, 5000, 10000]
- **详细测试 (detailed)**: 详细测试，10次重复，更准确的统计结果
- **快速测试 (quick)**: 快速测试，3次重复，只测试小规模数据 [100, 1000]

### 3. 结果输出
- **JSON结果文件**: 保存详细的测试结果和统计数据
- **性能对比图表**: 生成4个子图的综合性能对比图表
- **控制台输出**: 实时显示测试进度和结果

## 使用方法

### 基本用法

```bash
# 使用默认参数运行基础测试
python3 performance/benchmark.py

# 指定Shapefile文件
python3 performance/benchmark.py --shapefile ./data/test.shp

# 运行详细测试
python3 performance/benchmark.py --test-type detailed

# 运行快速测试
python3 performance/benchmark.py --test-type quick

# 指定输出目录
python3 performance/benchmark.py --output-dir ./my_results
```

### 命令行参数

- `--shapefile, -s`: Shapefile文件路径 (默认: ./data/test.shp)
- `--test-type, -t`: 测试类型 (basic/detailed/quick, 默认: basic)
- `--output-dir, -o`: 输出目录 (默认: ./output_data)

### 示例

```bash
# 基础测试
python3 performance/benchmark.py -s ./data/test.shp -t basic

# 详细测试并保存到自定义目录
python3 performance/benchmark.py -s ./data/test.shp -t detailed -o ./benchmark_results

# 快速测试
python3 performance/benchmark.py -s ./data/test.shp -t quick
```

## 输出文件

测试完成后，会在输出目录下生成以下文件：

### 1. 测试结果文件
- `benchmark_results/benchmark_results.json`: 包含所有测试结果的JSON文件

### 2. 性能图表
- `benchmark_results/performance_charts.png`: 包含4个子图的性能对比图表
  - 顺序读取性能对比
  - 随机读取性能对比
  - 性能提升百分比
  - 空间查询性能对比

## 测试内容详解

### 1. 顺序读取测试
- 测试OGR和自定义格式的顺序读取性能
- 分别测试几何数据和属性数据的读取速度
- 计算总体读取速率（要素/秒）

### 2. 随机读取测试
- 随机选择要素进行读取测试
- 模拟真实应用中的随机访问场景
- 测试不同规模数据的性能表现

### 3. 空间查询测试
- 使用3个不同范围的边界框进行空间查询
- OGR使用内置的空间过滤器
- 自定义格式使用S2空间索引
- 比较查询效率和准确性

### 4. 字符串池压缩统计
- 分析字符串池的压缩效果
- 计算压缩率和节省的空间
- 统计唯一字符串数量和总字符串数量

## 性能指标

### 主要指标
- **读取速率**: 要素/秒
- **查询速率**: 要素/秒
- **性能提升**: 相对于OGR的性能提升百分比
- **压缩率**: 字符串池的压缩效果

### 统计信息
- **平均值**: 多次测试的平均值
- **标准差**: 测试结果的标准差
- **最小值/最大值**: 测试结果的范围
- **中位数**: 测试结果的中位数

## 依赖要求

- Python 3.7+
- GDAL/OGR
- matplotlib
- numpy
- 自定义GIS存储模块

## 注意事项

1. **数据准备**: 确保Shapefile文件存在且格式正确
2. **内存使用**: 大规模测试可能需要较多内存
3. **测试时间**: 详细测试可能需要较长时间
4. **结果解释**: 性能提升为正值表示自定义格式更快，负值表示OGR更快

## 故障排除

### 常见问题

1. **找不到Shapefile文件**
   - 检查文件路径是否正确
   - 确保文件存在且有读取权限

2. **GDAL错误**
   - 确保GDAL库正确安装
   - 检查环境变量设置

3. **内存不足**
   - 减少测试规模
   - 使用快速测试模式

4. **图表生成失败**
   - 检查matplotlib安装
   - 确保有写入权限

## 版本历史

- **v1.0**: 初始版本，整合原有性能测试脚本
- 支持基础、详细、快速三种测试模式
- 提供完整的性能对比分析功能
