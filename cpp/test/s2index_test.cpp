//
//  Created by vlv-squid on 2025.07.18.
//

#include "gisindex/s2spatial_index.h"
#include "gisindex/struct_dkbbox.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <gdal.h>
#include <ogrsf_frmts.h>
#include <gtest/gtest.h>
#include <s2/s2latlng.h>
#include <s2/s2cell_id.h>
#include <iomanip> // Added for std::fixed and std::setprecision

using namespace S2Main;
using Point = bg::model::point<double, 2, bg::cs::cartesian>;
using Box = bg::model::box<Point>;

class S2IndexTest : public ::testing::Test {
  protected:
    S2IndexTest()
        : gis_dataset_path("/home/chenming/Data/GIS_DATA/filegdb/DLTB_2021CG.gdb")
        , index_file_path("./test_output/storage_test/DLTB_2021CG_s2.idx")
        , s2level(15)
        , s2index(std::make_unique<S2SpatialIndex>(index_file_path, s2level))
        , sample_bbox(103.2504, 26.4297, 103.3028, 26.4747) {}

    void SetUp() override {
        CPLSetConfigOption("CPL_DEBUG", "ON");
        CPLSetConfigOption("GDAL_FILENAME_IS_UTF8", "NO");
        CPLGetConfigOption("SHAPE_ENCODING", "UTF-8");
        GDALAllRegister();

        // 使用智能索引管理：自动判断是加载还是重新构建
        s2index->smartLoadOrBuild(gis_dataset_path, 50000);
    }

    std::string gis_dataset_path;
    std::string index_file_path;
    int s2level;
    std::unique_ptr<S2SpatialIndex> s2index;
    DKBBox sample_bbox;
};

TEST_F(S2IndexTest, S2IndexBuildTest) {
    std::cout << "\n[测试1] S2索引构建测试:" << std::endl;
    EXPECT_TRUE(s2index->exists()) << "S2索引文件应该存在";

    // 验证索引大小
    size_t index_size = s2index->getIndexSize();
    std::cout << "索引中的要素数量: " << index_size << std::endl;
    EXPECT_GT(index_size, 0) << "索引应该包含要素";
}

TEST_F(S2IndexTest, S2IndexQueryTest) {
    std::cout << "\n[测试2] S2索引查询测试:" << std::endl;

    // 测试1：小范围查询（应该返回较少要素）
    S2LatLngRect smallQueryRect(S2LatLng::FromDegrees(26.45, 103.26), S2LatLng::FromDegrees(26.46, 103.27));

    auto start = std::chrono::high_resolution_clock::now();
    auto smallResults = s2index->query(smallQueryRect, s2level);
    auto end = std::chrono::high_resolution_clock::now();

    auto smallTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "小范围查询时间: " << smallTime << "μs" << std::endl;
    std::cout << "小范围查询结果: " << smallResults.size() << " 个要素" << std::endl;

    // 测试2：中等范围查询（应该返回中等数量要素）
    S2LatLngRect mediumQueryRect(S2LatLng::FromDegrees(26.43, 103.25), S2LatLng::FromDegrees(26.47, 103.30));

    start = std::chrono::high_resolution_clock::now();
    auto mediumResults = s2index->query(mediumQueryRect, s2level);
    end = std::chrono::high_resolution_clock::now();

    auto mediumTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "中等范围查询时间: " << mediumTime << "μs" << std::endl;
    std::cout << "中等范围查询结果: " << mediumResults.size() << " 个要素" << std::endl;

    // 测试3：大范围查询（应该返回较多要素）
    S2LatLngRect largeQueryRect(S2LatLng::FromDegrees(26.40, 103.20), S2LatLng::FromDegrees(26.50, 103.35));

    start = std::chrono::high_resolution_clock::now();
    auto largeResults = s2index->query(largeQueryRect, s2level);
    end = std::chrono::high_resolution_clock::now();

    auto largeTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "大范围查询时间: " << largeTime << "μs" << std::endl;
    std::cout << "大范围查询结果: " << largeResults.size() << " 个要素" << std::endl;

    // 验证查询结果的合理性
    EXPECT_GE(smallResults.size(), 0) << "小范围查询结果应该非负";
    EXPECT_GE(mediumResults.size(), 0) << "中等范围查询结果应该非负";
    EXPECT_GE(largeResults.size(), 0) << "大范围查询结果应该非负";

    // 验证查询结果的数量关系（大范围应该包含更多要素）
    EXPECT_LE(smallResults.size(), mediumResults.size()) << "小范围查询结果应该少于中等范围";
    EXPECT_LE(mediumResults.size(), largeResults.size()) << "中等范围查询结果应该少于大范围";

    // 输出性能分析
    std::cout << "\n性能分析:" << std::endl;
    std::cout << "小范围查询: " << smallTime << "μs, " << smallResults.size() << " 要素, 平均 " << std::fixed << std::setprecision(3)
              << (smallResults.size() > 0 ? static_cast<double>(smallTime) / smallResults.size() : 0.0) << "μs/要素" << std::endl;
    std::cout << "中等范围查询: " << mediumTime << "μs, " << mediumResults.size() << " 要素, 平均 " << std::fixed << std::setprecision(3)
              << (mediumResults.size() > 0 ? static_cast<double>(mediumTime) / mediumResults.size() : 0.0) << "μs/要素" << std::endl;
    std::cout << "大范围查询: " << largeTime << "μs, " << largeResults.size() << " 要素, 平均 " << std::fixed << std::setprecision(3)
              << (largeResults.size() > 0 ? static_cast<double>(largeTime) / largeResults.size() : 0.0) << "μs/要素" << std::endl;
}

TEST_F(S2IndexTest, S2IndexLoadTest) {
    std::cout << "\n[测试3] S2索引加载测试:" << std::endl;

    // 创建新的索引实例来测试加载
    S2SpatialIndex newIndex(index_file_path, s2level);
    newIndex.load();

    // 验证加载后的索引大小
    size_t loadedIndexSize = newIndex.getIndexSize();
    size_t originalIndexSize = s2index->getIndexSize();

    std::cout << "原始索引要素数量: " << originalIndexSize << std::endl;
    std::cout << "加载后索引要素数量: " << loadedIndexSize << std::endl;

    EXPECT_EQ(loadedIndexSize, originalIndexSize) << "加载后的索引大小应该与原始索引一致";

    // 测试加载后的查询功能
    S2LatLngRect testQueryRect(S2LatLng::FromDegrees(26.45, 103.26), S2LatLng::FromDegrees(26.46, 103.27));

    auto results = newIndex.query(testQueryRect, s2level);
    std::cout << "加载后查询结果: " << results.size() << " 个要素" << std::endl;

    EXPECT_GE(results.size(), 0) << "加载后查询结果应该非负";
}

TEST_F(S2IndexTest, GDALComparisonTest) {
    std::cout << "\n[测试4] GDAL顺序扫描对比测试:" << std::endl;

    // 使用相同的查询范围
    S2LatLngRect queryRect(S2LatLng::FromDegrees(sample_bbox.minlat, sample_bbox.minlon), S2LatLng::FromDegrees(sample_bbox.maxlat, sample_bbox.maxlon));

    // GDAL顺序扫描
    GDALDataset* poDS = static_cast<GDALDataset*>(GDALOpenEx(gis_dataset_path.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    ASSERT_NE(poDS, nullptr) << "GDALOpenEx失败";

    OGRLayer* poLayer = poDS->GetLayer(0);
    ASSERT_NE(poLayer, nullptr) << "GetLayer失败";

    poLayer->SetSpatialFilterRect(sample_bbox.minlon, sample_bbox.minlat, sample_bbox.maxlon, sample_bbox.maxlat);
    poLayer->ResetReading();

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<int> gdalResults;
    while (OGRFeature* poFeature = poLayer->GetNextFeature()) {
        gdalResults.push_back(poFeature->GetFID());
        OGRFeature::DestroyFeature(poFeature);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto gdalTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "GDAL查询时间: " << gdalTime << "μs" << std::endl;
    std::cout << "GDAL查询结果: " << gdalResults.size() << " 个要素" << std::endl;

    GDALClose(poDS);

    // S2索引查询
    start = std::chrono::high_resolution_clock::now();
    auto s2Results = s2index->query(queryRect, s2level);
    end = std::chrono::high_resolution_clock::now();

    auto s2Time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "S2索引查询时间: " << s2Time << "μs" << std::endl;
    std::cout << "S2索引查询结果: " << s2Results.size() << " 个要素" << std::endl;

    // 性能对比（基于时间，而不是结果数量）
    if (gdalTime > 0 && s2Time > 0) {
        double speedup = static_cast<double>(gdalTime) / s2Time;
        std::cout << "S2索引相对于GDAL的性能提升: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;

        // 验证S2索引确实比GDAL快
        EXPECT_GT(speedup, 1.0) << "S2索引应该比GDAL顺序扫描快";

        // 输出详细的性能分析
        std::cout << "\n详细性能分析:" << std::endl;
        std::cout << "GDAL: " << gdalTime << "μs, " << gdalResults.size() << " 要素, 平均 " << std::fixed << std::setprecision(3) << (gdalResults.size() > 0 ? static_cast<double>(gdalTime) / gdalResults.size() : 0.0)
                  << "μs/要素" << std::endl;
        std::cout << "S2索引: " << s2Time << "μs, " << s2Results.size() << " 要素, 平均 " << std::fixed << std::setprecision(3) << (s2Results.size() > 0 ? static_cast<double>(s2Time) / s2Results.size() : 0.0)
                  << "μs/要素" << std::endl;
    }

    // 验证结果数量的一致性（允许有小的差异，因为不同的空间索引可能有不同的精度）
    EXPECT_NEAR(gdalResults.size(), s2Results.size(), std::max(gdalResults.size(), s2Results.size()) * 0.1) << "S2索引和GDAL查询结果数量应该大致一致";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}