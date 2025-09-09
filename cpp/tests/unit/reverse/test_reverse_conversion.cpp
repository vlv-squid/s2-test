//
// 逆向转换功能单元测试 - 使用Google Test框架
//

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <ctime>

#include "gisstorage/ogr_format_converter.h"
#include "gisstorage/gis_storage_system.h"
#include <ogrsf_frmts.h>

class ReverseConversionTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // 创建临时测试目录
        test_dir_ = "/tmp/reverse_conversion_test_" + std::to_string(std::time(nullptr));
        std::filesystem::create_directories(test_dir_);

        // 创建测试数据
        createTestData();
    }

    void TearDown() override {
        // 清理测试目录
        if (std::filesystem::exists(test_dir_)) {
            std::filesystem::remove_all(test_dir_);
        }
    }

    void createTestData() {
        // 创建简单的测试Shapefile数据
        std::string test_shapefile = test_dir_ + "/test_data.shp";
        createSimpleShapefile(test_shapefile);

        // 转换为自定义格式
        GisStorage::OGRFormatConverter converter(test_shapefile, test_dir_);
        converter.convert();

        // 设置自定义格式数据目录
        custom_data_dir_ = test_dir_;
        dataset_name_ = "test_data";
    }

    void createSimpleShapefile(const std::string& shapefile_path) {
        // 使用GDAL创建简单的测试Shapefile
        GDALAllRegister();

        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr) << "无法获取Shapefile驱动";

        GDALDataset* dataset = driver->Create(shapefile_path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(dataset, nullptr) << "无法创建测试Shapefile";

        OGRSpatialReference* spatial_ref = new OGRSpatialReference();
        spatial_ref->importFromEPSG(4326); // WGS84

        OGRLayer* layer = dataset->CreateLayer("test_layer", spatial_ref, wkbPoint, nullptr);
        ASSERT_NE(layer, nullptr) << "无法创建测试图层";

        // 创建字段
        OGRFieldDefn* name_field = new OGRFieldDefn("name", OFTString);
        name_field->SetWidth(50);
        layer->CreateField(name_field);

        OGRFieldDefn* value_field = new OGRFieldDefn("value", OFTInteger);
        layer->CreateField(value_field);

        // 创建测试要素
        for (int i = 0; i < 10; ++i) {
            OGRFeature* feature = OGRFeature::CreateFeature(layer->GetLayerDefn());

            // 设置几何
            OGRPoint* point = new OGRPoint(100.0 + i * 0.1, 20.0 + i * 0.1);
            feature->SetGeometry(point);

            // 设置属性
            feature->SetField("name", ("test_point_" + std::to_string(i)).c_str());
            feature->SetField("value", i);

            // 创建要素
            OGRErr err = layer->CreateFeature(feature);
            if (err != OGRERR_NONE) {
                std::cerr << "创建要素失败，错误代码: " << err << std::endl;
            }
            OGRFeature::DestroyFeature(feature);
        }

        GDALClose(dataset);
        delete spatial_ref;
    }

    bool checkRequiredFiles() {
        std::string geom_file = custom_data_dir_ + "/" + dataset_name_ + ".geom";
        std::string attr_file = custom_data_dir_ + "/" + dataset_name_ + ".attr";
        std::string idx_file = custom_data_dir_ + "/" + dataset_name_ + ".idx";
        std::string meta_file = custom_data_dir_ + "/" + dataset_name_ + "_meta.json";

        return std::filesystem::exists(geom_file) && std::filesystem::exists(attr_file) && std::filesystem::exists(idx_file) && std::filesystem::exists(meta_file);
    }

    std::string test_dir_;
    std::string custom_data_dir_;
    std::string dataset_name_;
};

// 测试逆向转换到Shapefile
TEST_F(ReverseConversionTest, ConvertToShapefile) {
    ASSERT_TRUE(checkRequiredFiles()) << "缺少必要的自定义格式文件";

    // 创建逆向转换器
    std::string dummy_original_file = custom_data_dir_ + "/" + dataset_name_ + ".shp";
    GisStorage::OGRFormatConverter converter(dummy_original_file, custom_data_dir_);

    // 创建输出目录
    std::string output_dir = test_dir_ + "/reverse_output";
    std::filesystem::create_directories(output_dir);

    // 测试转换到Shapefile
    std::string shapefile_output = output_dir + "/" + dataset_name_ + "_reverse.shp";

    EXPECT_TRUE(converter.convertToOGR(shapefile_output, "ESRI Shapefile")) << "Shapefile逆向转换失败";

    // 验证输出文件存在且不为空
    EXPECT_TRUE(std::filesystem::exists(shapefile_output)) << "输出Shapefile文件不存在";

    auto file_size = std::filesystem::file_size(shapefile_output);
    EXPECT_GT(file_size, 0) << "输出Shapefile文件为空";

    // 验证Shapefile的辅助文件也存在
    std::string shx_file = output_dir + "/" + dataset_name_ + "_reverse.shx";
    std::string dbf_file = output_dir + "/" + dataset_name_ + "_reverse.dbf";

    EXPECT_TRUE(std::filesystem::exists(shx_file)) << "Shapefile索引文件不存在";
    EXPECT_TRUE(std::filesystem::exists(dbf_file)) << "Shapefile属性文件不存在";
}

// 测试逆向转换到GDB
TEST_F(ReverseConversionTest, ConvertToGDB) {
    ASSERT_TRUE(checkRequiredFiles()) << "缺少必要的自定义格式文件";

    // 创建逆向转换器
    std::string dummy_original_file = custom_data_dir_ + "/" + dataset_name_ + ".shp";
    GisStorage::OGRFormatConverter converter(dummy_original_file, custom_data_dir_);

    // 创建输出目录
    std::string output_dir = test_dir_ + "/reverse_output";
    std::filesystem::create_directories(output_dir);

    // 测试转换到GDB
    std::string gdb_output = output_dir + "/" + dataset_name_ + "_reverse.gdb";

    EXPECT_TRUE(converter.convertToOGR(gdb_output, "OpenFileGDB")) << "GDB逆向转换失败";

    // 验证输出目录存在
    EXPECT_TRUE(std::filesystem::exists(gdb_output)) << "输出GDB目录不存在";

    EXPECT_TRUE(std::filesystem::is_directory(gdb_output)) << "GDB输出不是目录";
}

// 测试批量转换
TEST_F(ReverseConversionTest, ConvertToMultipleFormats) {
    ASSERT_TRUE(checkRequiredFiles()) << "缺少必要的自定义格式文件";

    // 创建逆向转换器
    std::string dummy_original_file = custom_data_dir_ + "/" + dataset_name_ + ".shp";
    GisStorage::OGRFormatConverter converter(dummy_original_file, custom_data_dir_);

    // 创建输出目录
    std::string output_dir = test_dir_ + "/reverse_output";
    std::filesystem::create_directories(output_dir);

    // 测试批量转换（只测试GDB和Shapefile）
    std::vector<std::string> formats = {"ESRI Shapefile", "OpenFileGDB"};

    EXPECT_TRUE(converter.convertToMultipleFormats(output_dir, formats)) << "批量逆向转换失败";

    // 验证生成的文件（批量转换使用带索引的文件名）
    std::string shapefile_output = output_dir + "/" + dataset_name_ + "_reverse_0.shp";
    std::string gdb_output = output_dir + "/" + dataset_name_ + "_reverse_1.gdb";

    EXPECT_TRUE(std::filesystem::exists(shapefile_output)) << "批量转换生成的Shapefile不存在";
    EXPECT_TRUE(std::filesystem::exists(gdb_output)) << "批量转换生成的GDB不存在";
}

// 测试错误处理
TEST_F(ReverseConversionTest, ErrorHandling) {
    // 测试不存在的自定义格式数据
    std::string non_existent_dir = "/tmp/non_existent_dir";
    std::string dummy_file = non_existent_dir + "/dummy.shp";

    GisStorage::OGRFormatConverter converter(dummy_file, non_existent_dir);

    std::string output_dir = test_dir_ + "/error_test";
    std::filesystem::create_directories(output_dir);
    std::string output_file = output_dir + "/test.shp";

    EXPECT_FALSE(converter.convertToOGR(output_file, "ESRI Shapefile")) << "应该处理不存在的输入数据";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}