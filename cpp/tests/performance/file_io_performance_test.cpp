#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>

class FileIOPerformanceTest {
  private:
    std::string test_file_path_;
    size_t file_size_;
    size_t buffer_size_;

  public:
    FileIOPerformanceTest(const std::string& test_file_path, size_t buffer_size = 8192)
        : test_file_path_(test_file_path)
        , buffer_size_(buffer_size) {
        if (std::filesystem::exists(test_file_path_)) {
            file_size_ = std::filesystem::file_size(test_file_path_);
        } else {
            file_size_ = 0;
        }
    }

    // 使用 std::ifstream 顺序读取
    double test_ifstream_sequential_read() {
        if (file_size_ == 0)
            return -1.0;

        std::ifstream file(test_file_path_, std::ios::binary);
        if (!file) {
            return -1.0;
        }

        std::vector<char> buffer(buffer_size_);
        size_t total_read = 0;

        auto start_time = std::chrono::high_resolution_clock::now();

        while (file.read(buffer.data(), buffer.size())) {
            total_read += file.gcount();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 使用 C 原生 API 顺序读取
    double test_c_api_sequential_read() {
        if (file_size_ == 0)
            return -1.0;

        FILE* file = fopen(test_file_path_.c_str(), "rb");
        if (!file) {
            return -1.0;
        }

        std::vector<char> buffer(buffer_size_);
        size_t total_read = 0;

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t bytes_read;
        while ((bytes_read = fread(buffer.data(), 1, buffer.size(), file)) > 0) {
            total_read += bytes_read;
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        fclose(file);
        return duration.count() / 1000.0; // 返回毫秒
    }

    // 使用 std::ifstream 随机读取
    double test_ifstream_random_read(int num_reads = 100000) {
        if (file_size_ == 0)
            return -1.0;

        std::ifstream file(test_file_path_, std::ios::binary);
        if (!file) {
            return -1.0;
        }

        std::vector<char> buffer(buffer_size_);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dis(0, file_size_ - buffer_size_);

        auto start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < num_reads; ++i) {
            size_t offset = dis(gen);
            file.seekg(offset);
            file.read(buffer.data(), buffer.size());
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 使用 C 原生 API 随机读取
    double test_c_api_random_read(int num_reads = 100000) {
        if (file_size_ == 0)
            return -1.0;

        FILE* file = fopen(test_file_path_.c_str(), "rb");
        if (!file) {
            return -1.0;
        }

        std::vector<char> buffer(buffer_size_);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dis(0, file_size_ - buffer_size_);

        auto start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < num_reads; ++i) {
            size_t offset = dis(gen);
            fseek(file, offset, SEEK_SET);
            fread(buffer.data(), 1, buffer.size(), file);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        fclose(file);
        return duration.count() / 1000.0; // 返回毫秒
    }

    // 使用 std::ifstream 读取小块数据（模拟几何数据读取）
    double test_ifstream_small_reads(int num_reads = 100000) {
        if (file_size_ == 0)
            return -1.0;

        std::ifstream file(test_file_path_, std::ios::binary);
        if (!file) {
            return -1.0;
        }

        std::vector<char> buffer(256); // 小块数据
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dis(0, file_size_ - buffer.size());

        auto start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < num_reads; ++i) {
            size_t offset = dis(gen);
            file.seekg(offset);
            file.read(buffer.data(), buffer.size());
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        return duration.count() / 1000.0; // 返回毫秒
    }

    // 使用 C 原生 API 读取小块数据
    double test_c_api_small_reads(int num_reads = 100000) {
        if (file_size_ == 0)
            return -1.0;

        FILE* file = fopen(test_file_path_.c_str(), "rb");
        if (!file) {
            return -1.0;
        }

        std::vector<char> buffer(256); // 小块数据
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dis(0, file_size_ - buffer.size());

        auto start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < num_reads; ++i) {
            size_t offset = dis(gen);
            fseek(file, offset, SEEK_SET);
            fread(buffer.data(), 1, buffer.size(), file);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        fclose(file);
        return duration.count() / 1000.0; // 返回毫秒
    }

    const std::string& get_test_file_path() const { return test_file_path_; }
    size_t get_file_size() const { return file_size_; }
};

// Google Test 测试用例

class FileIOPerformanceTestFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        // 可以在这里准备测试文件
        geom_file = "/home/chenming/Projects/test/s2-test/output_data/test.geom";
    }

    std::string geom_file;
};

TEST_F(FileIOPerformanceTestFixture, FileExists) {
    // 检查测试文件是否存在
    EXPECT_TRUE(std::filesystem::exists(geom_file)) << "测试文件不存在: " << geom_file;
}

TEST_F(FileIOPerformanceTestFixture, SequentialReadPerformance) {
    if (!std::filesystem::exists(geom_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    FileIOPerformanceTest test(geom_file, 8192);

    double ifstream_time = test.test_ifstream_sequential_read();
    double c_api_time = test.test_c_api_sequential_read();

    EXPECT_GT(ifstream_time, 0) << "std::ifstream 顺序读取失败";
    EXPECT_GT(c_api_time, 0) << "C API 顺序读取失败";

    std::cout << "顺序读取性能测试:" << std::endl;
    std::cout << "  std::ifstream: " << ifstream_time << " ms" << std::endl;
    std::cout << "  C API: " << c_api_time << " ms" << std::endl;

    if (ifstream_time > 0 && c_api_time > 0) {
        double speedup = ifstream_time / c_api_time;
        std::cout << "  性能比: C API 比 std::ifstream " << (speedup > 1 ? "快" : "慢") << " " << std::abs(speedup - 1.0) << " 倍" << std::endl;
    }
}

TEST_F(FileIOPerformanceTestFixture, RandomReadPerformance) {
    if (!std::filesystem::exists(geom_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    FileIOPerformanceTest test(geom_file, 8192);

    double ifstream_time = test.test_ifstream_random_read(100000);
    double c_api_time = test.test_c_api_random_read(100000);

    EXPECT_GT(ifstream_time, 0) << "std::ifstream 随机读取失败";
    EXPECT_GT(c_api_time, 0) << "C API 随机读取失败";

    std::cout << "随机读取性能测试 (100000次):" << std::endl;
    std::cout << "  std::ifstream: " << ifstream_time << " ms" << std::endl;
    std::cout << "  C API: " << c_api_time << " ms" << std::endl;

    if (ifstream_time > 0 && c_api_time > 0) {
        double speedup = ifstream_time / c_api_time;
        std::cout << "  性能比: C API 比 std::ifstream " << (speedup > 1 ? "快" : "慢") << " " << std::abs(speedup - 1.0) << " 倍" << std::endl;
    }
}

TEST_F(FileIOPerformanceTestFixture, SmallReadPerformance) {
    if (!std::filesystem::exists(geom_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    FileIOPerformanceTest test(geom_file, 8192);

    double ifstream_time = test.test_ifstream_small_reads(100000);
    double c_api_time = test.test_c_api_small_reads(100000);

    EXPECT_GT(ifstream_time, 0) << "std::ifstream 小块读取失败";
    EXPECT_GT(c_api_time, 0) << "C API 小块读取失败";

    std::cout << "小块数据读取性能测试 (100000次):" << std::endl;
    std::cout << "  std::ifstream: " << ifstream_time << " ms" << std::endl;
    std::cout << "  C API: " << c_api_time << " ms" << std::endl;

    if (ifstream_time > 0 && c_api_time > 0) {
        double speedup = ifstream_time / c_api_time;
        std::cout << "  性能比: C API 比 std::ifstream " << (speedup > 1 ? "快" : "慢") << " " << std::abs(speedup - 1.0) << " 倍" << std::endl;
    }
}

// 参数化测试示例
class BufferSizeTest : public ::testing::TestWithParam<size_t> {};

TEST_P(BufferSizeTest, SequentialReadWithDifferentBufferSizes) {
    std::string geom_file = "../output_data/dltb_532300_2020.geom";

    if (!std::filesystem::exists(geom_file)) {
        GTEST_SKIP() << "测试文件不存在，跳过测试";
    }

    size_t buffer_size = GetParam();
    FileIOPerformanceTest test(geom_file, buffer_size);

    double ifstream_time = test.test_ifstream_sequential_read();
    double c_api_time = test.test_c_api_sequential_read();

    EXPECT_GT(ifstream_time, 0) << "std::ifstream 顺序读取失败 (buffer_size=" << buffer_size << ")";
    EXPECT_GT(c_api_time, 0) << "C API 顺序读取失败 (buffer_size=" << buffer_size << ")";

    std::cout << "缓冲区大小 " << buffer_size << " 字节的顺序读取:" << std::endl;
    std::cout << "  std::ifstream: " << ifstream_time << " ms" << std::endl;
    std::cout << "  C API: " << c_api_time << " ms" << std::endl;
}

INSTANTIATE_TEST_SUITE_P(BufferSizes, BufferSizeTest, ::testing::Values(1024, 4096, 8192, 16384, 32768));

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}