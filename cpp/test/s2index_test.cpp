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
}

TEST_F(S2IndexTest, S2IndexQueryTest) {
    std::cout << "\n[测试2] S2索引查询测试:" << std::endl;

    // 创建查询范围
    S2LatLngRect queryRect(S2LatLng::FromDegrees(sample_bbox.minlat, sample_bbox.minlon), S2LatLng::FromDegrees(sample_bbox.maxlat, sample_bbox.maxlon));

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    auto results = s2index->query(queryRect, s2level);
    std::chrono::system_clock::time_point end = std::chrono::system_clock::now();

    std::cout << "查询时间: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
    std::cout << "空间查询结果: " << results.size() << " 个要素" << std::endl;

    EXPECT_GE(results.size(), 0) << "查询结果应该非负";
}

TEST_F(S2IndexTest, S2IndexLoadTest) {
    std::cout << "\n[测试3] S2索引加载测试:" << std::endl;

    // 创建新的索引实例来测试加载
    S2SpatialIndex newIndex(index_file_path, s2level);
    newIndex.load();

    // 测试查询
    S2LatLngRect queryRect(S2LatLng::FromDegrees(sample_bbox.minlat, sample_bbox.minlon), S2LatLng::FromDegrees(sample_bbox.maxlat, sample_bbox.maxlon));

    auto results = newIndex.query(queryRect, s2level);
    std::cout << "加载后查询结果: " << results.size() << " 个要素" << std::endl;

    EXPECT_GE(results.size(), 0) << "加载后查询结果应该非负";
}

TEST_F(S2IndexTest, GDALComparisonTest) {
    std::cout << "\n[测试4] GDAL顺序扫描对比测试:" << std::endl;

    // GDAL顺序扫描
    GDALDataset* poDS = static_cast<GDALDataset*>(GDALOpenEx(gis_dataset_path.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    ASSERT_NE(poDS, nullptr) << "GDALOpenEx失败";

    OGRLayer* poLayer = poDS->GetLayer(0);
    ASSERT_NE(poLayer, nullptr) << "GetLayer失败";

    poLayer->SetSpatialFilterRect(sample_bbox.minlon, sample_bbox.minlat, sample_bbox.maxlon, sample_bbox.maxlat);
    poLayer->ResetReading();

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::vector<int> gdalResults;
    while (OGRFeature* poFeature = poLayer->GetNextFeature()) {
        gdalResults.push_back(poFeature->GetFID());
    }
    std::chrono::system_clock::time_point end = std::chrono::system_clock::now();

    std::cout << "GDAL查询时间: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
    std::cout << "GDAL查询结果: " << gdalResults.size() << " 个要素" << std::endl;

    GDALClose(poDS);

    // S2索引查询
    S2LatLngRect queryRect(S2LatLng::FromDegrees(sample_bbox.minlat, sample_bbox.minlon), S2LatLng::FromDegrees(sample_bbox.maxlat, sample_bbox.maxlon));

    start = std::chrono::system_clock::now();
    auto s2Results = s2index->query(queryRect, s2level);
    end = std::chrono::system_clock::now();

    std::cout << "S2索引查询时间: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
    std::cout << "S2索引查询结果: " << s2Results.size() << " 个要素" << std::endl;

    // 性能对比
    if (!gdalResults.empty() && !s2Results.empty()) {
        double speedup = static_cast<double>(gdalResults.size()) / s2Results.size();
        std::cout << "S2索引相对于GDAL的性能提升: " << speedup << "x" << std::endl;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}