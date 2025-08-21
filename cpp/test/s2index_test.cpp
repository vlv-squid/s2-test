//
//  Created by vlv-squid on 2025.07.18.
//

#include "gisindex/hybrid_index.h"
#include "gisindex/struct_dkbbox.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <gdal.h>
#include <ogrsf_frmts.h>
#include <gtest/gtest.h>

using namespace S2Main;
using Point = bg::model::point<double, 2, bg::cs::cartesian>;
using Box = bg::model::box<Point>;

class HybridIndexTest : public ::testing::Test {
  protected:
    // HybridIndexTest()
    //     : gis_dataset_path("../data/test.shp")
    //     , index_dir_path("../index")
    //     , hybridindexer(std::make_unique<HybridIndex>(gis_dataset_path, index_dir_path))
    //     , sample_bbox(103.2504, 26.4297, 103.3028, 26.4747)
    //     , boostbox(sample_bbox.transform2BoostBox()) {
    // }

    HybridIndexTest()
        : gis_dataset_path("/home/chenming/Data/GIS_DATA/filegdb/DLTB_2021CG.gdb")
        , index_dir_path("../index")
        , hybridindexer(std::make_unique<HybridIndex>(gis_dataset_path, index_dir_path))
        , sample_bbox(100.546875, 25.3125, 101.25, 26.015625)
        , boostbox(sample_bbox.transform2BoostBox()) {}

    void SetUp() override {
        CPLSetConfigOption("CPL_DEBUG", "ON");
        CPLSetConfigOption("GDAL_FILENAME_IS_UTF8", "NO");
        CPLGetConfigOption("SHAPE_ENCODING", "UTF-8");
        CPLSetConfigOption("PROJ_LIB", "/home/chenming/Projects/geotalk-depend-4centos-static/CppDepend/Release/share/proj");
        GDALAllRegister();
        hybridindexer->buildIndex();
    }

    std::string gis_dataset_path;
    std::string index_dir_path;
    std::unique_ptr<HybridIndex> hybridindexer;
    DKBBox sample_bbox;
    Box boostbox;
};

TEST_F(HybridIndexTest, GDALScanTest) {
    std::cout << "\n[测试1] 纯gdal索引:" << std::endl;
    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    GDALDataset* poDS = static_cast<GDALDataset*>(GDALOpenEx(gis_dataset_path.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    ASSERT_NE(poDS, nullptr) << "GDALOpenEx failed.";

    OGRLayer* poLayer = poDS->GetLayer(0);
    ASSERT_NE(poLayer, nullptr) << "GetLayer failed.";

    poLayer->SetSpatialFilterRect(sample_bbox.minlon, sample_bbox.minlat, sample_bbox.maxlon, sample_bbox.maxlat);
    poLayer->ResetReading();

    std::vector<int> results;
    results.reserve(poLayer->GetFeatureCount());
    while (OGRFeature* poFeature = poLayer->GetNextFeature()) {
        results.push_back(poFeature->GetFID());
    }

    std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
    std::cout << "Query time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
    std::cout << "Spatial Query Results: " << results.size() << " features." << std::endl;

    GDALClose(poDS);
}

TEST_F(HybridIndexTest, S2QueryTest) {
    std::cout << "\n[测试2] 纯S2索引 (无精确验证):" << std::endl;
    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    auto results = hybridindexer->queryByBbox(boostbox, false, false);
    std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
    std::cout << "Query time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
    std::cout << "Spatial Query Results: " << results.size() << " features." << std::endl;
}

TEST_F(HybridIndexTest, S2QueryWithBBoxTest) {
    std::cout << "\n[测试3] S2 + 矩形精确验证:" << std::endl;
    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    auto results = hybridindexer->queryByBbox(boostbox, false, true);
    std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
    std::cout << "Query time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
    std::cout << "Spatial Query Results: " << results.size() << " features." << std::endl;
}

TEST_F(HybridIndexTest, S2QueryWithRtreeTest) {
    std::cout << "\n[测试4] S2 + Rtree精确验证:" << std::endl;
    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    auto results = hybridindexer->queryByBbox(boostbox, true, false);
    std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
    std::cout << "Query time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
    std::cout << "Spatial Query Results: " << results.size() << " features." << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}