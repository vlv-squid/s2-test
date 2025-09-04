# GIS存储系统文档

## 📚 文档结构

### 🏗️ 项目模块
- [系统架构](architecture.md) - 系统整体架构和模块说明

### 📖 使用示例
- [演示程序](examples.md) - 完整的使用示例和演示

### ⚡ 性能测试
- [基准测试](benchmark.md) - 性能测试结果和分析

### 🛠️ 开发指南
- [构建指南](build.md) - 如何构建和配置项目
- [测试指南](test.md) - 如何运行和编写测试

## 🚀 快速开始

### 1. 构建项目
```bash
# 使用构建脚本
./tools/build/build.sh --all

# 或使用CMake
mkdir build && cd build
cmake ../cpp
make
```

### 2. 运行测试
```bash
# 运行所有测试
./tools/test/run_tests.sh --all

# 运行特定测试
./tools/test/run_tests.sh s2index_test
```

### 3. 运行示例
```bash
# 运行演示程序
./tools/demo/run_demo.sh ../../data/test.shp ./output
```

## 📋 项目结构

```
cpp/
├── include/          # 头文件
├── src/             # 源代码
├── tests/           # 测试代码
├── examples/        # 示例程序
├── benchmarks/      # 基准测试
├── tools/           # 工具脚本
│   ├── build/       # 构建工具
│   ├── test/        # 测试工具
│   ├── demo/        # 演示工具
│   └── benchmark/   # 基准测试工具
└── docs/            # 文档
```
