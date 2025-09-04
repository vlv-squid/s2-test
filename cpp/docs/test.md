# 测试指南

## 🧪 测试结构

### 单元测试
- `tests/unit/gisindex/s2index_test.cpp` - S2索引功能测试
- `tests/unit/gisstorage/gis_storage_test.cpp` - GIS存储功能测试

### 集成测试
- `tests/integration/integrated_gis_format_test.cpp` - 完整工作流测试

### 性能测试
- `tests/performance/file_io_performance_test.cpp` - 文件I/O性能测试

## 🚀 运行测试

### 使用测试脚本（推荐）
```bash
# 运行所有测试
./tools/test/run_tests.sh --all

# 运行单元测试
./tools/test/run_tests.sh --unit

# 运行集成测试
./tools/test/run_tests.sh --integration

# 运行性能测试
./tools/test/run_tests.sh --performance

# 运行特定测试
./tools/test/run_tests.sh s2index_test
```

### 使用CTest
```bash
cd build
ctest
```

### 直接运行
```bash
cd build/tests
./s2index_test
./gis_storage_test
./integrated_gis_format_test
./file_io_performance_test
```

## 📊 测试结果

### 测试输出
- 成功：显示绿色 ✓
- 失败：显示红色 ✗
- 警告：显示黄色 ⚠

### 测试报告
- 详细输出：`--verbose` 选项
- XML报告：自动生成 `test_results.xml`

## 🔧 测试配置

### 测试超时
- 单元测试：300秒
- 集成测试：600秒
- 性能测试：300秒

### 测试标签
- `unit` - 单元测试
- `integration` - 集成测试
- `performance` - 性能测试
- `gisindex` - S2索引相关
- `gisstorage` - 存储相关

## 📝 编写测试

### 测试框架
使用Google Test框架：
```cpp
#include <gtest/gtest.h>

TEST(TestSuite, TestName) {
    // 测试代码
    EXPECT_EQ(expected, actual);
}
```

### 测试数据
测试数据位于 `tests/fixtures/test_data/` 目录
