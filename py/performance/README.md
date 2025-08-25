# GIS 性能测试模块

## 📋 模块概述

GIS性能测试模块提供了全面的性能测试和分析工具，用于评估不同空间索引算法和存储格式的性能表现。该模块支持多种测试场景，提供详细的性能指标和可视化分析报告。

## 🚀 主要功能

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

## 📁 文件结构

```
performance/
├── __init__.py           # 模块初始化
├── benchmark.py          # 主要性能测试工具
└── README.md            # 本文档
```

## 🛠️ 安装依赖

```bash
# 安装核心依赖
pip install matplotlib numpy pandas

# 安装地理数据处理依赖
pip install geopandas gdal

# 安装自定义模块依赖
pip install -e ../gisindex
pip install -e ../gisstorage
```

## 📖 使用方法

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

| 参数           | 短参数 | 描述                            | 默认值               |
| -------------- | ------ | ------------------------------- | -------------------- |
| `--shapefile`  | `-s`   | Shapefile文件路径               | `./data/test.shp`    |
| `--test-type`  | `-t`   | 测试类型 (basic/detailed/quick) | `basic`              |
| `--output-dir` | `-o`   | 输出目录                        | `./output_data`      |
| `--iterations` | `-i`   | 测试重复次数                    | 根据测试类型自动设置 |
| `--scales`     | `-c`   | 测试规模列表                    | 根据测试类型自动设置 |

### 使用示例

```bash
# 基础测试
python3 performance/benchmark.py -s ./data/test.shp -t basic

# 详细测试并保存到自定义目录
python3 performance/benchmark.py -s ./data/test.shp -t detailed -o ./benchmark_results

# 快速测试
python3 performance/benchmark.py -s ./data/test.shp -t quick

# 自定义测试参数
python3 performance/benchmark.py -s ./data/test.shp -i 15 -c 100,1000,5000
```

## 📊 测试内容详解

### 1. 顺序读取测试
- **测试目标**: 比较OGR和自定义格式的顺序读取性能
- **测试内容**: 
  - 几何数据顺序读取
  - 属性数据顺序读取
  - 总体读取速率计算
- **性能指标**: 要素/秒，内存使用量

### 2. 随机读取测试
- **测试目标**: 模拟真实应用中的随机访问场景
- **测试内容**:
  - 随机选择要素进行读取
  - 不同规模数据的性能表现
  - 缓存效果评估
- **性能指标**: 平均读取时间，标准差

### 3. 空间查询测试
- **测试目标**: 比较不同空间索引算法的查询性能
- **测试内容**:
  - 使用3个不同范围的边界框进行查询
  - OGR使用内置的空间过滤器
  - 自定义格式使用S2空间索引
- **性能指标**: 查询时间，结果准确性

### 4. 字符串池压缩统计
- **测试目标**: 分析字符串池的压缩效果
- **测试内容**:
  - 计算压缩率和节省的空间
  - 统计唯一字符串数量和总字符串数量
  - 分析压缩算法的效率
- **性能指标**: 压缩率，内存节省

## 📈 性能指标

### 主要指标

| 指标         | 描述                      | 单位    |
| ------------ | ------------------------- | ------- |
| **读取速率** | 每秒处理的要素数量        | 要素/秒 |
| **查询速率** | 每秒处理的查询数量        | 查询/秒 |
| **性能提升** | 相对于OGR的性能提升百分比 | %       |
| **压缩率**   | 字符串池的压缩效果        | %       |
| **内存使用** | 测试过程中的内存占用      | MB      |

### 统计信息

| 统计量            | 描述             |
| ----------------- | ---------------- |
| **平均值**        | 多次测试的平均值 |
| **标准差**        | 测试结果的标准差 |
| **最小值/最大值** | 测试结果的范围   |
| **中位数**        | 测试结果的中位数 |
| **95%置信区间**   | 统计置信区间     |

## 📁 输出文件

### 1. 测试结果文件
- **`benchmark_results.json`**: 包含所有测试结果的JSON文件
  - 详细的测试数据
  - 统计信息
  - 性能指标

### 2. 性能图表
- **`performance_charts.png`**: 包含4个子图的综合性能对比图表
  - 顺序读取性能对比
  - 随机读取性能对比
  - 性能提升百分比
  - 空间查询性能对比

### 3. 日志文件
- **`benchmark.log`**: 详细的测试日志
  - 测试进度信息
  - 错误和警告信息
  - 性能数据记录

## 🔧 配置选项

### 测试参数配置

```python
# 测试规模配置
TEST_SCALES = {
    'quick': [100, 1000],
    'basic': [100, 500, 1000, 5000, 10000],
    'detailed': [100, 500, 1000, 2000, 5000, 10000, 20000]
}

# 重复次数配置
TEST_ITERATIONS = {
    'quick': 3,
    'basic': 5,
    'detailed': 10
}

# 空间查询范围配置
QUERY_BBOXES = [
    (103.2504, 26.4297, 103.3028, 26.4747),  # 小范围
    (103.2000, 26.4000, 103.3500, 26.5000),  # 中范围
    (103.0000, 26.3000, 103.5000, 26.6000)   # 大范围
]
```

### 可视化配置

```python
# 图表样式配置
CHART_STYLE = {
    'figure_size': (16, 12),
    'dpi': 300,
    'style': 'seaborn-v0_8',
    'color_palette': ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']
}

# 输出格式配置
OUTPUT_FORMATS = ['png', 'pdf', 'svg']
```

## 🐛 故障排除

### 常见问题

1. **找不到Shapefile文件**
   ```bash
   # 检查文件路径
   ls -la ./data/test.shp
   
   # 确保文件存在且有读取权限
   chmod 644 ./data/test.shp
   ```

2. **GDAL错误**
   ```bash
   # 检查GDAL安装
   python -c "from osgeo import gdal; print(gdal.__version__)"
   
   # 设置GDAL环境变量
   export GDAL_DATA=/usr/share/gdal
   ```

3. **内存不足**
   ```bash
   # 减少测试规模
   python3 performance/benchmark.py -t quick
   
   # 增加系统内存限制
   ulimit -v unlimited
   ```

4. **图表生成失败**
   ```bash
   # 检查matplotlib后端
   python -c "import matplotlib; print(matplotlib.get_backend())"
   
   # 设置非交互式后端
   export MPLBACKEND=Agg
   ```

### 性能优化建议

1. **测试环境优化**
   - 使用SSD存储测试数据
   - 关闭不必要的后台进程
   - 确保有足够的内存

2. **测试参数调整**
   - 根据硬件配置调整测试规模
   - 适当减少重复次数以提高测试速度
   - 使用快速测试模式进行初步评估

3. **结果分析**
   - 关注相对性能而非绝对数值
   - 考虑测试环境的差异
   - 多次运行测试以获得稳定结果

## 📊 性能基准

### 典型测试结果

| 测试类型 | OGR性能      | 自定义格式性能 | 性能提升 |
| -------- | ------------ | -------------- | -------- |
| 顺序读取 | 1000 要素/秒 | 1500 要素/秒   | +50%     |
| 随机读取 | 500 要素/秒  | 1200 要素/秒   | +140%    |
| 空间查询 | 200 查询/秒  | 800 查询/秒    | +300%    |
| 内存使用 | 200 MB       | 150 MB         | -25%     |

### 系统要求

- **CPU**: 4核心以上
- **内存**: 8GB以上
- **存储**: SSD推荐
- **操作系统**: Linux, macOS, Windows

## 📈 开发计划

### 近期计划
- [ ] 添加更多空间索引算法的性能测试
- [ ] 支持分布式性能测试
- [ ] 增加内存使用分析
- [ ] 改进可视化功能

### 长期计划
- [ ] 支持云环境性能测试
- [ ] 添加自动化性能回归测试
- [ ] 集成机器学习性能预测
- [ ] 支持实时性能监控

## 📚 参考资料

- [Python Performance Testing](https://docs.python.org/3/library/timeit.html)
- [Matplotlib Documentation](https://matplotlib.org/)
- [Pandas Performance](https://pandas.pydata.org/pandas-docs/stable/user_guide/enhancingperf.html)

## 🤝 贡献指南

欢迎提交Issue和Pull Request来改进模块。请确保：
1. 代码符合PEP 8编码规范
2. 添加适当的测试用例
3. 更新相关文档
4. 通过所有测试

---

*最后更新: 2025年1月*
