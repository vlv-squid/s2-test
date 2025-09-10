//
// 逆向转换功能单元测试
//

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>

#include "gisstorage/ogr_format_converter.h"
#include <ogrsf_frmts.h>

class ReverseConversionTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // 设置输入shapefile路径
        input_shapefile_ = "/home/chenming/Projects/test/s2-test/data/test.shp";

        // 创建输出目录
        output_base_dir_ = "/home/chenming/Projects/test/s2-test/output_data";
        test_output_dir_ = output_base_dir_ + "/reverse_conversion_test_" + std::to_string(std::time(nullptr));
        std::filesystem::create_directories(test_output_dir_);

        // 验证输入文件存在
        ASSERT_TRUE(std::filesystem::exists(input_shapefile_)) << "输入shapefile不存在: " << input_shapefile_;

        // 创建测试数据
        createTestData();
    }

    void TearDown() override {
        // 注意：不删除输出目录，保留测试结果供检查
        // 如果需要清理，可以手动删除 /home/chenming/Projects/test/s2-test/output_data 目录
    }

    void createTestData() {
        // 使用指定的shapefile转换为自定义格式
        GisStorage::OGRFormatConverter converter(input_shapefile_, test_output_dir_);
        converter.Convert();

        // 设置自定义格式数据目录
        custom_data_dir_ = test_output_dir_;
        // 从输入文件名提取数据集名称（去掉路径和扩展名）
        std::filesystem::path input_path(input_shapefile_);
        dataset_name_ = input_path.stem().string();
    }

    bool checkRequiredFiles() {
        std::string geom_file = custom_data_dir_ + "/" + dataset_name_ + ".geom";
        std::string attr_file = custom_data_dir_ + "/" + dataset_name_ + ".attr";
        std::string idx_file = custom_data_dir_ + "/" + dataset_name_ + ".idx";
        std::string meta_file = custom_data_dir_ + "/" + dataset_name_ + "_meta.json";

        return std::filesystem::exists(geom_file) && std::filesystem::exists(attr_file) && std::filesystem::exists(idx_file) && std::filesystem::exists(meta_file);
    }

    std::string input_shapefile_;
    std::string output_base_dir_;
    std::string test_output_dir_;
    std::string custom_data_dir_;
    std::string dataset_name_;
};

// 测试批量转换
TEST_F(ReverseConversionTest, ConvertToMultipleFormats) {
    ASSERT_TRUE(checkRequiredFiles()) << "缺少必要的自定义格式文件";

    // 创建逆向转换器
    std::string dummy_original_file = custom_data_dir_ + "/" + dataset_name_ + ".shp";
    GisStorage::OGRFormatConverter converter(dummy_original_file, custom_data_dir_);

    // 创建输出目录
    std::string output_dir = test_output_dir_ + "/reverse_output";
    std::filesystem::create_directories(output_dir);

    // 清理可能存在的旧文件
    std::string shapefile_output = output_dir + "/" + dataset_name_ + "_reverse.shp";
    std::string gdb_output = output_dir + "/" + dataset_name_ + "_reverse.gdb";
    if (std::filesystem::exists(shapefile_output)) {
        std::filesystem::remove(shapefile_output);
    }
    if (std::filesystem::exists(gdb_output)) {
        std::filesystem::remove_all(gdb_output);
    }

    // 测试批量转换（只测试GDB和Shapefile）
    std::vector<std::string> formats = {"ESRI Shapefile", "OpenFileGDB", "GeoJSON"};

    EXPECT_TRUE(converter.ConvertToMultipleFormats(output_dir, formats)) << "批量逆向转换失败";

    // 验证生成的文件（批量转换使用带索引的文件名）

    EXPECT_TRUE(std::filesystem::exists(shapefile_output)) << "批量转换生成的Shapefile不存在";
    EXPECT_TRUE(std::filesystem::exists(gdb_output)) << "批量转换生成的GDB不存在";
}

// 测试错误处理
TEST_F(ReverseConversionTest, ErrorHandling) {
    // 测试不存在的自定义格式数据
    std::string non_existent_dir = "/tmp/non_existent_dir";
    std::string dummy_file = non_existent_dir + "/dummy.shp";

    GisStorage::OGRFormatConverter converter(dummy_file, non_existent_dir);

    std::string output_dir = test_output_dir_ + "/error_test";
    std::filesystem::create_directories(output_dir);
    std::string output_file = output_dir + "/test.shp";

    EXPECT_FALSE(converter.ConvertToOGR(output_file, "ESRI Shapefile")) << "应该处理不存在的输入数据";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}