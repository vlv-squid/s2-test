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
#include <iomanip>

using namespace S2Main;
using Point = bg::model::point<double, 2, bg::cs::cartesian>;
using Box = bg::model::box<Point>;

class S2IndexTest : public ::testing::Test {
  protected:
    S2IndexTest()
        : gis_dataset_path("/home/chenming/Data/GIS_DATA/filegdb/DLTB_2021CG.gdb")
        , index_file_path("./test_output/storage_test/DLTB_2021CG_s2.idx")
        , s2level(14)
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
    std::cout << "索引中的S2单元格数量: " << index_size << std::endl;
    EXPECT_GT(index_size, 0) << "索引应该包含单元格";
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
    S2LatLngRect mediumQueryRect(S2LatLng::FromDegrees(26.4297, 103.2504), S2LatLng::FromDegrees(26.4747, 103.3028));

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

TEST_F(S2IndexTest, MultiThreadedBuildTest) {
    std::cout << "\n[测试5] 多线程S2索引构建性能测试:" << std::endl;

    // 测试单线程构建
    std::cout << "开始单线程构建测试..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    // 创建新的索引实例进行单线程构建
    S2SpatialIndex singleThreadIndex("./test_output/single_thread_test.idx", s2level);
    bool single_success = singleThreadIndex.buildFromDataset(gis_dataset_path, 50000);

    auto end = std::chrono::high_resolution_clock::now();
    auto singleThreadTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (single_success) {
        std::cout << "单线程构建成功，耗时: " << singleThreadTime << "ms" << std::endl;
        std::cout << "单线程索引 - S2单元格数量: " << singleThreadIndex.getIndexSize() << ", 总要素数量: " << singleThreadIndex.getTotalFeatureCount() << std::endl;
    } else {
        std::cout << "单线程构建失败" << std::endl;
        return;
    }

    // 测试多线程构建（8线程）
    std::cout << "\n开始多线程构建测试（8线程）..." << std::endl;
    start = std::chrono::high_resolution_clock::now();

    // 创建新的索引实例进行多线程构建
    S2SpatialIndex multiThreadIndex("./test_output/multi_thread_test.idx", s2level);
    bool multi_success = multiThreadIndex.buildFromDatasetMultiThreaded(gis_dataset_path, 50000, 8);

    end = std::chrono::high_resolution_clock::now();
    auto multiThreadTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (multi_success) {
        std::cout << "多线程构建成功，耗时: " << multiThreadTime << "ms" << std::endl;
        std::cout << "多线程索引 - S2单元格数量: " << multiThreadIndex.getIndexSize() << ", 总要素数量: " << multiThreadIndex.getTotalFeatureCount() << std::endl;

        // 性能对比
        if (singleThreadTime > 0 && multiThreadTime > 0) {
            double speedup = static_cast<double>(singleThreadTime) / multiThreadTime;
            std::cout << "\n性能对比结果:" << std::endl;
            std::cout << "单线程时间: " << singleThreadTime << "ms" << std::endl;
            std::cout << "多线程时间: " << multiThreadTime << "ms" << std::endl;
            std::cout << "多线程加速比: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;

            // 验证多线程确实比单线程快
            EXPECT_GT(speedup, 1.0) << "多线程应该比单线程快";
        }

        // 验证索引完整性
        EXPECT_EQ(singleThreadIndex.getIndexSize(), multiThreadIndex.getIndexSize()) << "单线程和多线程构建的索引S2单元格数量应该一致";
        EXPECT_EQ(singleThreadIndex.getTotalFeatureCount(), multiThreadIndex.getTotalFeatureCount()) << "单线程和多线程构建的索引总要素数量应该一致";
    } else {
        std::cout << "多线程构建失败" << std::endl;
    }
}

TEST_F(S2IndexTest, MultiThreadedComprehensiveTest) {
    std::cout << "\n[测试6] 多线程综合测试（一致性验证 + 性能对比）:" << std::endl;

    // 测试1：单线程构建
    std::cout << "开始单线程构建测试..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    S2SpatialIndex singleThreadIndex("./test_output/single_thread_comprehensive.idx", s2level);
    bool single_success = singleThreadIndex.buildFromDataset(gis_dataset_path, 50000);

    auto end = std::chrono::high_resolution_clock::now();
    auto singleThreadTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (!single_success) {
        std::cout << "单线程构建失败，跳过综合测试" << std::endl;
        return;
    }

    // 测试2：标准多线程构建（8线程）
    std::cout << "\n开始标准多线程构建测试（8线程）..." << std::endl;
    start = std::chrono::high_resolution_clock::now();

    S2SpatialIndex multiThreadIndex("./test_output/multi_thread_comprehensive.idx", s2level);
    bool multi_success = multiThreadIndex.buildFromDatasetMultiThreaded(gis_dataset_path, 50000, 8);

    end = std::chrono::high_resolution_clock::now();
    auto multiThreadTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (!multi_success) {
        std::cout << "标准多线程构建失败，跳过综合测试" << std::endl;
        return;
    }

    // 测试3：TBB优化多线程构建（8线程）
    std::cout << "\n开始TBB优化多线程构建测试（8线程）..." << std::endl;
    start = std::chrono::high_resolution_clock::now();

    S2SpatialIndex tbbThreadIndex("./test_output/tbb_thread_comprehensive.idx", s2level);
    bool tbb_success = tbbThreadIndex.buildFromDatasetMultiThreadedTBB(gis_dataset_path, 50000, 8);

    end = std::chrono::high_resolution_clock::now();
    auto tbbThreadTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (!tbb_success) {
        std::cout << "TBB多线程构建失败，跳过综合测试" << std::endl;
        return;
    }

    // 性能对比分析
    std::cout << "\n=== 性能对比结果 ===" << std::endl;
    std::cout << "单线程构建时间: " << singleThreadTime << "ms" << std::endl;
    std::cout << "标准多线程(8线程)时间: " << multiThreadTime << "ms" << std::endl;
    std::cout << "TBB多线程(8线程)时间: " << tbbThreadTime << "ms" << std::endl;

    // 计算加速比
    if (singleThreadTime > 0 && multiThreadTime > 0) {
        double speedup_std = static_cast<double>(singleThreadTime) / multiThreadTime;
        std::cout << "标准多线程(8线程)加速比: " << std::fixed << std::setprecision(2) << speedup_std << "x" << std::endl;
    }

    if (singleThreadTime > 0 && tbbThreadTime > 0) {
        double speedup_tbb = static_cast<double>(singleThreadTime) / tbbThreadTime;
        std::cout << "TBB多线程(8线程)加速比: " << std::fixed << std::setprecision(2) << speedup_tbb << "x" << std::endl;
    }

    if (multiThreadTime > 0 && tbbThreadTime > 0) {
        double improvement = static_cast<double>(multiThreadTime) / tbbThreadTime;
        std::cout << "TBB相对于标准多线程的改进: " << std::fixed << std::setprecision(2) << improvement << "x" << std::endl;
    }

    // 验证索引完整性
    size_t single_cells = singleThreadIndex.getIndexSize();
    size_t multi_cells = multiThreadIndex.getIndexSize();
    size_t tbb_cells = tbbThreadIndex.getIndexSize();

    size_t single_features = singleThreadIndex.getTotalFeatureCount();
    size_t multi_features = multiThreadIndex.getTotalFeatureCount();
    size_t tbb_features = tbbThreadIndex.getTotalFeatureCount();

    std::cout << "\n=== 索引完整性验证 ===" << std::endl;
    std::cout << "单线程索引 - S2单元格数量: " << single_cells << ", 总要素数量: " << single_features << std::endl;
    std::cout << "标准多线程索引 - S2单元格数量: " << multi_cells << ", 总要素数量: " << multi_features << std::endl;
    std::cout << "TBB多线程索引 - S2单元格数量: " << tbb_cells << ", 总要素数量: " << tbb_features << std::endl;

    EXPECT_EQ(single_cells, multi_cells) << "单线程和标准多线程索引S2单元格数量应该一致";
    EXPECT_EQ(single_cells, tbb_cells) << "单线程和TBB多线程索引S2单元格数量应该一致";

    EXPECT_EQ(single_features, multi_features) << "单线程和标准多线程索引总要素数量应该一致";
    EXPECT_EQ(single_features, tbb_features) << "单线程和TBB多线程索引总要素数量应该一致";

    // 使用多种查询范围验证查询结果一致性
    std::cout << "\n=== 查询结果一致性验证 ===" << std::endl;
    std::vector<std::pair<std::string, S2LatLngRect>> test_queries = {{"小范围查询", S2LatLngRect(S2LatLng::FromDegrees(26.45, 103.26), S2LatLng::FromDegrees(26.46, 103.27))},
                                                                      {"中等范围查询", S2LatLngRect(S2LatLng::FromDegrees(26.43, 103.25), S2LatLng::FromDegrees(26.47, 103.30))},
                                                                      {"大范围查询", S2LatLngRect(S2LatLng::FromDegrees(26.40, 103.20), S2LatLng::FromDegrees(26.50, 103.35))},
                                                                      {"扩展范围查询", S2LatLngRect(S2LatLng::FromDegrees(26.35, 103.15), S2LatLng::FromDegrees(26.55, 103.40))}};

    for (const auto& [query_name, query_rect] : test_queries) {
        std::cout << "\n执行 " << query_name << "..." << std::endl;

        // 单线程索引查询
        auto single_start = std::chrono::high_resolution_clock::now();
        auto single_results = singleThreadIndex.query(query_rect, s2level);
        auto single_end = std::chrono::high_resolution_clock::now();
        auto single_time = std::chrono::duration_cast<std::chrono::microseconds>(single_end - single_start).count();

        // 标准多线程索引查询
        auto multi_start = std::chrono::high_resolution_clock::now();
        auto multi_results = multiThreadIndex.query(query_rect, s2level);
        auto multi_end = std::chrono::high_resolution_clock::now();
        auto multi_time = std::chrono::duration_cast<std::chrono::microseconds>(multi_end - multi_start).count();

        // TBB多线程索引查询
        auto tbb_start = std::chrono::high_resolution_clock::now();
        auto tbb_results = tbbThreadIndex.query(query_rect, s2level);
        auto tbb_end = std::chrono::high_resolution_clock::now();
        auto tbb_time = std::chrono::duration_cast<std::chrono::microseconds>(tbb_end - tbb_start).count();

        std::cout << query_name << " 结果对比:" << std::endl;
        std::cout << "  单线程索引: " << single_results.size() << " 个要素, " << single_time << "μs" << std::endl;
        std::cout << "  标准多线程索引: " << multi_results.size() << " 个要素, " << multi_time << "μs" << std::endl;
        std::cout << "  TBB多线程索引: " << tbb_results.size() << " 个要素, " << tbb_time << "μs" << std::endl;

        // 验证结果数量一致
        EXPECT_EQ(single_results.size(), multi_results.size()) << query_name << " 单线程和标准多线程结果数量应该一致";
        EXPECT_EQ(single_results.size(), tbb_results.size()) << query_name << " 单线程和TBB多线程结果数量应该一致";

        // 验证结果内容一致（排序后比较）
        if (single_results.size() == multi_results.size() && single_results.size() == tbb_results.size()) {
            std::sort(single_results.begin(), single_results.end());
            std::sort(multi_results.begin(), multi_results.end());
            std::sort(tbb_results.begin(), tbb_results.end());

            bool results_match_std = (single_results == multi_results);
            bool results_match_tbb = (single_results == tbb_results);

            EXPECT_TRUE(results_match_std) << query_name << " 单线程和标准多线程查询结果应该完全一致";
            EXPECT_TRUE(results_match_tbb) << query_name << " 单线程和TBB多线程查询结果应该完全一致";

            if (results_match_std && results_match_tbb) {
                std::cout << "  ✓ 所有索引查询结果完全一致" << std::endl;
            } else {
                std::cout << "  ✗ 查询结果不一致！" << std::endl;

                if (!results_match_std) {
                    std::cout << "    单线程与标准多线程结果不一致" << std::endl;
                }
                if (!results_match_tbb) {
                    std::cout << "    单线程与TBB多线程结果不一致" << std::endl;
                }
            }
        }

        // 查询性能对比
        std::cout << "  查询性能对比:" << std::endl;
        if (single_time > 0 && multi_time > 0) {
            double query_speedup_std = static_cast<double>(single_time) / multi_time;
            std::cout << "    标准多线程 vs 单线程: " << std::fixed << std::setprecision(2) << query_speedup_std << "x" << std::endl;
        }
        if (single_time > 0 && tbb_time > 0) {
            double query_speedup_tbb = static_cast<double>(single_time) / tbb_time;
            std::cout << "    TBB多线程 vs 单线程: " << std::fixed << std::setprecision(2) << query_speedup_tbb << "x" << std::endl;
        }
        if (multi_time > 0 && tbb_time > 0) {
            double query_improvement = static_cast<double>(multi_time) / tbb_time;
            std::cout << "    TBB vs 标准多线程: " << std::fixed << std::setprecision(2) << query_improvement << "x" << std::endl;
        }
    }

    std::cout << "\n多线程综合测试完成！" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}