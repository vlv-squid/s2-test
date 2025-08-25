# Python GIS 空间索引与存储系统

## 📋 项目概述

这是一个高性能的Python GIS空间索引与存储系统，提供了多种空间索引算法（S2、R树、H3、Geohash）和自定义二进制存储格式。该系统专为大规模地理空间数据处理而设计，支持高效的几何数据压缩、快速空间查询和多线程并行处理。

## 🏗️ 系统架构

### 核心模块

#### 1. 空间索引模块 (`gisindex/`)
- **S2空间索引**: 基于Google S2几何库的全球空间索引
- **R树索引**: 经典的空间索引结构，适用于局部查询
- **H3索引**: Uber开发的六边形空间索引系统
- **Geohash索引**: 基于字符串编码的空间索引
- **混合索引**: 结合多种索引的优势，提供最优查询性能
- **可视化支持**: 查询结果的可视化展示

#### 2. 存储系统模块 (`gisstorage/`)
- **几何数据存储**: 高效的几何数据二进制存储格式
- **属性数据存储**: 支持JSON格式的属性数据存储
- **字符串池优化**: 字符串去重和压缩存储
- **Shapefile转换**: 支持Shapefile到自定义格式的转换
- **数据序列化**: 高效的二进制序列化机制

#### 3. 性能测试模块 (`performance/`)
- **综合性能测试**: 多种索引算法的性能对比
- **存储格式测试**: 自定义格式与标准格式的性能对比
- **可视化分析**: 性能测试结果的可视化展示
- **基准测试**: 标准化的性能基准测试

## 🚀 主要特性

### 空间索引特性
- **多算法支持**: S2、R树、H3、Geohash等多种索引算法
- **自动优化**: 根据查询范围自动选择最优索引策略
- **持久化存储**: 索引数据可持久化到磁盘，支持快速加载
- **并发查询**: 支持多线程并发空间查询
- **可视化支持**: 查询结果的可视化展示

### 存储系统特性
- **坐标压缩**: 差分编码压缩，可减少50%存储空间
- **二进制格式**: 高效的二进制存储格式，支持快速读写
- **索引缓存**: 智能缓存机制，提高随机访问性能
- **数据完整性**: 完善的数据校验和错误恢复机制
- **字符串池**: 字符串去重和压缩存储

### 性能测试特性
- **多维度测试**: 顺序读取、随机读取、空间查询等多维度测试
- **对比分析**: 与GDAL/OGR等标准格式的性能对比
- **可视化报告**: 自动生成性能对比图表
- **统计分析**: 详细的统计分析和性能指标

## 📁 文件结构

```
py/
├── gisindex/              # 空间索引模块
│   ├── __init__.py
│   ├── index_base.py      # 索引基类
│   ├── s2_index.py        # S2空间索引
│   ├── rtree_index.py     # R树空间索引
│   ├── h3_index.py        # H3空间索引
│   ├── geohash_index.py   # Geohash索引
│   ├── index_tester.py    # 索引测试工具
│   ├── visualization.py   # 可视化工具
│   ├── runner.py          # 运行器
│   └── README.md          # 模块文档
├── gisstorage/            # 存储系统模块
│   ├── __init__.py
│   ├── models.py          # 数据模型
│   ├── storage.py         # 存储系统
│   ├── serializers.py     # 序列化器
│   ├── converter.py       # 格式转换器
│   ├── gissystem.py       # 系统管理
│   └── runner.py          # 运行器
├── performance/           # 性能测试模块
│   ├── __init__.py
│   ├── benchmark.py       # 性能测试
│   └── README.md          # 模块文档
└── README.md             # 本文档
```

## 🛠️ 安装与配置

### 系统要求
- **Python版本**: Python 3.7+
- **操作系统**: Linux, macOS, Windows
- **依赖库**: 见requirements.txt

### 安装步骤

```bash
# 1. 克隆项目
git clone <repository-url>
cd s2-test

# 2. 创建虚拟环境（推荐）
python -m venv venv
source venv/bin/activate  # Linux/macOS
# 或
venv\Scripts\activate     # Windows

# 3. 安装依赖
pip install -r requirements.txt

# 4. 验证安装
python -c "import gisindex, gisstorage; print('安装成功')"
```

### 依赖库说明

主要依赖库包括：
- **GDAL/OGR**: 地理空间数据处理
- **s2sphere**: S2空间索引
- **rtree**: R树空间索引
- **h3**: H3六边形索引
- **pygeohash**: Geohash编码
- **matplotlib**: 可视化支持
- **numpy**: 数值计算
- **pandas**: 数据处理

## 📖 使用示例

### 基本使用

```python
from gisindex import S2Index, RTreeIndex, H3Index, GeohashIndex
from gisstorage import GisStorageSystem
import geopandas as gpd

# 创建空间索引
s2_index = S2Index("./data/test.gdb", level=15)
s2_index.build_index()

# 执行空间查询
bbox = (103.2504, 26.4297, 103.3028, 26.4747)
results = s2_index.query_by_bbox(bbox)
print(f"查询结果数量: {len(results)}")

# 创建存储系统
storage = GisStorageSystem("./output")
storage.convert_from_shapefile("./data/test.shp")

# 批量读取数据
feature_ids = [1, 2, 3, 4, 5]
geometries = storage.read_geometries(feature_ids)
attributes = storage.read_attributes(feature_ids)
```

### 性能测试

```python
from performance import run_benchmark

# 运行基础性能测试
run_benchmark(
    shapefile="./data/test.shp",
    test_type="basic",
    output_dir="./benchmark_results"
)

# 运行详细性能测试
run_benchmark(
    shapefile="./data/test.shp",
    test_type="detailed",
    output_dir="./detailed_results"
)
```

### 可视化查询结果

```python
from gisindex.visualization import visualize_query_results

# 可视化查询结果
visualize_query_results(
    query_bbox=bbox,
    results=results,
    output_file="./query_results.png"
)
```

## 📊 性能特点

### 空间索引性能
- **S2索引**: 全球范围查询，层级可调，适合大规模数据
- **R树索引**: 局部查询性能优异，内存占用适中
- **H3索引**: 六边形网格，适合地理分析
- **Geohash索引**: 字符串编码，适合分布式系统

### 存储系统性能
- **坐标压缩**: 差分编码可减少50%存储空间
- **读取性能**: 随机访问O(1)时间复杂度
- **并发处理**: 支持多线程并行读写
- **内存效率**: 智能缓存，最小化内存占用

### 性能测试结果
- **索引构建**: 100万要素约需30-60秒
- **空间查询**: 平均查询时间<20ms
- **数据压缩**: 坐标数据压缩率50-70%
- **并发性能**: 8线程下性能提升5-7倍

## 🧪 测试与验证

### 运行测试

```bash
# 运行空间索引测试
python test/s2index_test.py
python test/h3index_test.py
python test/geohash_index_test.py

# 运行存储系统测试
python test/demo_gisstorage.py

# 运行性能测试
python py/performance/benchmark.py
```

### 测试覆盖
- **单元测试**: 各模块的核心功能测试
- **集成测试**: 模块间的集成测试
- **性能测试**: 性能基准测试
- **可视化测试**: 可视化功能测试

## 🔧 配置选项

### 环境变量
```bash
# 设置GDAL数据路径
export GDAL_DATA=/usr/share/gdal

# 设置Python路径
export PYTHONPATH=/path/to/project:$PYTHONPATH

# 设置日志级别
export LOG_LEVEL=INFO
```

### 配置文件
支持通过配置文件自定义参数：
- 索引参数配置
- 存储路径配置
- 性能测试参数
- 可视化设置

## 🐛 故障排除

### 常见问题

1. **GDAL安装问题**
   ```bash
   # Ubuntu/Debian
   sudo apt-get install python3-gdal
   
   # CentOS/RHEL
   sudo yum install python3-gdal
   
   # macOS
   brew install gdal
   ```

2. **依赖库冲突**
   ```bash
   # 使用conda环境
   conda create -n gis python=3.9
   conda activate gis
   conda install -c conda-forge gdal
   ```

3. **内存不足**
   ```bash
   # 增加Python内存限制
   export PYTHONMALLOC=malloc
   ```

## 📈 开发计划

### 近期计划
- [ ] 支持更多空间索引算法
- [ ] 添加空间分析功能
- [ ] 优化内存使用和缓存策略
- [ ] 增加更多数据格式支持
- [ ] 改进可视化功能

### 长期计划
- [ ] 分布式索引支持
- [ ] 实时数据更新
- [ ] 机器学习集成
- [ ] Web服务接口
- [ ] 云原生支持

## 📄 许可证

本项目遵循与主项目相同的许可证。

## 🤝 贡献指南

欢迎提交Issue和Pull Request来改进项目。请确保：
1. 代码符合PEP 8编码规范
2. 添加适当的测试用例
3. 更新相关文档
4. 通过所有测试

## 📞 联系方式

如有问题或建议，请通过以下方式联系：
- 提交GitHub Issue
- 发送邮件至项目维护者

## 📚 参考资料

- [S2 Geometry](https://s2geometry.io/)
- [H3 Documentation](https://h3geo.org/)
- [RTree Documentation](https://toblerity.org/rtree/)
- [GDAL/OGR](https://gdal.org/)

---

*最后更新: 2025年1月*
