# 构建指南

## 📋 依赖要求

- CMake 3.28.3+
- C++17 编译器
- GDAL 3.x
- Google S2 几何库
- Boost Geometry
- Google Test
- nlohmann/json
- TBB (Threading Building Blocks)
- absl (Abseil C++库)

## 🛠️ 构建步骤

### 方法1: 使用构建脚本（推荐）
```bash
# 构建所有目标
./tools/build/build.sh --all

# 构建特定目标
./tools/build/build.sh --test      # 构建测试
./tools/build/build.sh --benchmark # 构建基准测试
./tools/build/build.sh --examples  # 构建示例程序

# 清理构建
./tools/build/build.sh --clean
```

### 方法2: 使用CMake
```bash
mkdir build && cd build
cmake ../cpp
make
```

### 方法3: 使用Ninja
```bash
mkdir build && cd build
cmake -G Ninja ../cpp
ninja
```

## 📦 构建目标

### 测试程序
- `s2index_test` - S2索引单元测试
- `gis_storage_test` - GIS存储单元测试
- `reverse_conversion_test` - 逆向转换测试
- `integrated_gis_format_test` - 集成测试
- `file_io_performance_test` - 文件I/O性能测试
- `gdal_filegdb_performance_test` - GDAL FileGDB性能测试
- `custom_format_performance_test` - 自定义格式性能测试

### 基准测试
- `spatial_index_benchmark` - 空间索引基准测试

### 示例程序
- `gis_storage_demo` - 完整演示程序

## 🔧 构建选项

### 构建类型
- `Release` - 优化版本（默认）
- `Debug` - 调试版本

### 并行构建
```bash
# 指定并行任务数
./tools/build/build.sh --jobs 8
```

## 📁 输出目录

构建完成后，可执行文件位于：
- `build/tests/` - 测试程序
- `build/benchmarks/` - 基准测试
- `build/examples/` - 示例程序
- `build/src/` - 库文件（libgisindex.so, libgisstorage.so）

## 🧪 运行测试

```bash
# 使用CTest运行所有测试
cd build
ctest

# 运行特定类型的测试
ctest -L unit          # 单元测试
ctest -L performance   # 性能测试
ctest -L integration   # 集成测试

# 运行特定测试
cd build/tests
./s2index_test
./gis_storage_test
./reverse_conversion_test
```
