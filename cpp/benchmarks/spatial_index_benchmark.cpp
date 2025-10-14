#include <iostream>
#include <chrono>
#include <vector>
#include <map>
#include <string>
#include <fstream>
#include <random>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <sys/resource.h>
#include <filesystem>

// S2 Geometry Library
#include <s2/s2latlng.h>
#include <s2/s2latlng_rect.h>
#include <s2/s2region_coverer.h>
#include <s2/s2cell_id.h>

// GDAL/OGR
#include <ogrsf_frmts.h>

// Simple JSON output (avoid external dependency)
struct JsonValue {
    std::string key;
    std::string value;
    std::vector<JsonValue> children;

    JsonValue(const std::string& k, const std::string& v)
        : key(k)
        , value(v) {}
    JsonValue(const std::string& k)
        : key(k) {}

    void addChild(const JsonValue& child) { children.push_back(child); }

    std::string toString(int indent = 0) const {
        std::string result;
        std::string indentStr(indent * 2, ' ');

        if (children.empty()) {
            result += indentStr + "\"" + key + "\": " + value;
        } else {
            result += indentStr + "\"" + key + "\": {\n";
            for (size_t i = 0; i < children.size(); ++i) {
                result += children[i].toString(indent + 1);
                if (i < children.size() - 1)
                    result += ",";
                result += "\n";
            }
            result += indentStr + "}";
        }
        return result;
    }
};

struct BenchmarkResult {
    std::string index_type;
    std::string operation;
    int data_size;
    double avg_time_ms;
    double min_time_ms;
    double max_time_ms;
    double std_dev_ms;
    int result_count;
    double memory_usage_mb;
    int iterations;
    std::string data_source;
};

struct QueryBBox {
    double min_lat, min_lng, max_lat, max_lng;
    std::string name;
};

class FairSpatialIndexBenchmark {
  private:
    std::string data_path_;
    std::string data_format_;
    std::vector<OGRFeature*> features_;
    std::vector<S2Point> s2_points_;
    std::vector<S2CellId> s2_cells_;
    std::map<std::string, std::vector<int>> s2_index_;
    std::vector<long long> ogr_fids_all_;

    // 测试配置 - 将根据数据集规模动态调整
    std::vector<int> test_sizes_;
    std::vector<QueryBBox> test_bboxes_ = {{26.4297, 103.2504, 26.4747, 103.3028, "小范围查询"}, {26.0, 103.0, 27.0, 104.0, "中等范围查询"}, {25.0, 102.0, 28.0, 105.0, "大范围查询"}};
    int test_iterations_ = 5;

    // 随机数生成器
    std::mt19937 rng_;

    // 内存优化配置
    static constexpr size_t MAX_DATA_POINTS = 10000000; // 1000万要素
    static constexpr size_t MEMORY_LIMIT_MB = 8192;

    // 进度显示
    size_t total_features_ = 0;
    size_t processed_features_ = 0;

    // 根据数据集规模动态设置测试规模
    void setupTestSizes() {
        test_sizes_.clear();

        // 基础测试规模
        std::vector<int> base_sizes = {100, 500, 1000, 5000, 10000, 50000, 100000};

        // 根据数据集规模添加更大的测试规模
        if (total_features_ >= 500000) {
            base_sizes.push_back(500000);
        }
        if (total_features_ >= 1000000) {
            base_sizes.push_back(1000000);
        }
        if (total_features_ >= 5000000) {
            base_sizes.push_back(5000000);
        }
        if (total_features_ >= 10000000) {
            base_sizes.push_back(10000000);
        }

        // 只保留不超过数据集总要素数的测试规模
        for (int size : base_sizes) {
            if (size <= static_cast<int>(total_features_)) {
                test_sizes_.push_back(size);
            }
        }

        std::cout << "数据集总要素数: " << total_features_ << std::endl;
        std::cout << "测试规模: ";
        for (size_t i = 0; i < test_sizes_.size(); ++i) {
            std::cout << test_sizes_[i];
            if (i < test_sizes_.size() - 1)
                std::cout << ", ";
        }
        std::cout << std::endl;
    }

    // S2索引是否已构建
    bool s2_index_built_ = false;

  public:
    FairSpatialIndexBenchmark(const std::string& data_path)
        : data_path_(data_path)
        , rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
        // 初始化GDAL
        GDALAllRegister();

        // 检测数据格式
        if (data_path.find(".shp") != std::string::npos) {
            data_format_ = "Shapefile";
        } else if (data_path.find(".gdb") != std::string::npos) {
            data_format_ = "FileGDB";
        } else {
            data_format_ = "Unknown";
        }
    }

    ~FairSpatialIndexBenchmark() {
        // 清理资源
        for (auto feature : features_) {
            OGRFeature::DestroyFeature(feature);
        }
    }

    bool loadData(const std::string& data_path) {
        std::cout << "Loading data from: " << data_path << std::endl;
        std::cout << "Data format: " << data_format_ << std::endl;

        GDALAllRegister();

        GDALDataset* dataset = (GDALDataset*)GDALOpenEx(data_path.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!dataset) {
            std::cerr << "Failed to open dataset: " << data_path << std::endl;
            return false;
        }

        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            std::cerr << "Failed to get layer" << std::endl;
            GDALClose(dataset);
            return false;
        }

        // 获取总特征数量
        total_features_ = layer->GetFeatureCount();
        std::cout << "Total features: " << total_features_ << std::endl;

        // 设置目标处理数量
        size_t target_count = std::min(total_features_, MAX_DATA_POINTS);
        std::cout << "Target features to process: " << target_count << std::endl;

        size_t processed = 0;

        layer->ResetReading();
        OGRFeature* feature;

        while ((feature = layer->GetNextFeature()) != nullptr && processed < target_count) {
            OGRGeometry* geometry = feature->GetGeometryRef();
            if (geometry) {
                try {
                    // 检查几何体类型，只处理简单的几何体
                    OGRwkbGeometryType geomType = geometry->getGeometryType();
                    if (geomType == wkbMultiPolygon) {
                        // 对于多重多边形，检查复杂度
                        OGRMultiPolygon* multiPoly = (OGRMultiPolygon*)geometry;
                        if (multiPoly->getNumGeometries() > 10) {
                            // 跳过过于复杂的几何体
                            continue;
                        }
                    }

                    // 获取几何体的边界框
                    OGREnvelope envelope;
                    geometry->getEnvelope(&envelope);

                    // 检查边界框是否有效
                    if (envelope.MinX >= envelope.MaxX || envelope.MinY >= envelope.MaxY) {
                        continue;
                    }

                    // 使用边界框的中心点
                    double center_lat = (envelope.MinY + envelope.MaxY) / 2.0;
                    double center_lng = (envelope.MinX + envelope.MaxX) / 2.0;

                    // 检查坐标是否在有效范围内
                    if (center_lat < -90 || center_lat > 90 || center_lng < -180 || center_lng > 180) {
                        continue;
                    }

                    // 添加到S2索引
                    S2LatLng latlng = S2LatLng::FromDegrees(center_lat, center_lng);
                    S2Point s2_point = latlng.ToPoint();
                    s2_points_.push_back(s2_point);

                    // 记录FID以便后续随机读取
                    long long fid = static_cast<long long>(feature->GetFID());
                    ogr_fids_all_.push_back(fid);

                    // 获取S2 Cell ID
                    S2RegionCoverer::Options options;
                    options.set_min_level(10);
                    options.set_max_level(10);
                    options.set_max_cells(1);
                    S2RegionCoverer coverer(options);
                    std::vector<S2CellId> cellIds;

                    // 创建一个小矩形区域
                    S2LatLng p1 = S2LatLng::FromDegrees(center_lat - 0.0001, center_lng - 0.0001);
                    S2LatLng p2 = S2LatLng::FromDegrees(center_lat + 0.0001, center_lng + 0.0001);
                    S2LatLngRect rect(p1, p2);
                    coverer.GetCovering(rect, &cellIds);

                    if (!cellIds.empty()) {
                        S2CellId cell_id = cellIds[0];
                        s2_cells_.push_back(cell_id);
                        std::string cell_key = std::to_string(cell_id.id());
                        s2_index_[cell_key].push_back(processed);
                    }

                    processed++;
                    processed_features_ = processed;

                    // 显示进度
                    if (processed % 10000 == 0) {
                        showProgress();
                    }

                    // 检查内存限制
                    if (isMemoryLimitExceeded()) {
                        std::cout << "\n警告: 达到内存限制，停止加载更多数据" << std::endl;
                        break;
                    }
                } catch (const std::exception& e) {
                    std::cout << "\n处理几何体时出错: " << e.what() << std::endl;
                    continue;
                }
            }

            OGRFeature::DestroyFeature(feature);
        }

        showProgress();
        std::cout << std::endl;

        std::cout << "Loaded " << processed << " features" << std::endl;
        std::cout << "S2 index with " << s2_cells_.size() << " cells" << std::endl;
        std::cout << "Memory usage: " << getCurrentMemoryUsage() << "MB" << std::endl;

        // 根据实际加载的数据量设置测试规模
        total_features_ = processed; // 更新为实际处理的数据量
        setupTestSizes();

        GDALClose(dataset);
        return processed > 0;
    }

    void clearS2Structures() {
        s2_cells_.clear();
        s2_index_.clear();
        s2_index_built_ = false;
    }

    // 公平的S2索引构建：使用与S2SpatialIndex相同的构建方法
    void buildS2IndexForSize(int n) {
        clearS2Structures();

        // 重新打开数据集进行完整的数据加载和S2转换
        GDALDataset* dataset = (GDALDataset*)GDALOpenEx(data_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!dataset) {
            return;
        }

        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            GDALClose(dataset);
            return;
        }

        int build_size = std::min(n, static_cast<int>(total_features_));
        std::cout << "Building S2 index for " << build_size << " features (using S2SpatialIndex method)..." << std::endl;

        // 使用与S2SpatialIndex相同的S2级别和配置
        S2RegionCoverer::Options options;
        options.set_min_level(14); // 使用与S2SpatialIndex相同的级别
        options.set_max_level(14);
        options.set_max_cells(8); // 使用与S2SpatialIndex相同的配置
        S2RegionCoverer coverer(options);

        layer->ResetReading();
        OGRFeature* feature;
        int processed = 0;

        while ((feature = layer->GetNextFeature()) != nullptr && processed < build_size) {
            OGRGeometry* geometry = feature->GetGeometryRef();
            if (geometry) {
                try {
                    // 使用与S2SpatialIndex相同的几何体处理方法
                    OGRPoint center;
                    if (geometry->Centroid(&center) == OGRERR_NONE) {
                        // 将坐标转换为S2 Cell ID（与S2SpatialIndex完全一致）
                        S2LatLng latlng = S2LatLng::FromDegrees(center.getY(), center.getX());

                        // 创建一个小矩形区域（与S2SpatialIndex完全一致）
                        S2LatLng p1 = S2LatLng::FromDegrees(center.getY() - 0.0001, center.getX() - 0.0001);
                        S2LatLng p2 = S2LatLng::FromDegrees(center.getY() + 0.0001, center.getX() + 0.0001);
                        S2LatLngRect rect(p1, p2);

                        std::vector<S2CellId> cellIds;
                        coverer.GetCovering(rect, &cellIds);

                        if (!cellIds.empty()) {
                            S2CellId cell_id = cellIds[0];
                            s2_cells_.push_back(cell_id);
                            std::string cell_key = std::to_string(cell_id.id());
                            s2_index_[cell_key].push_back(processed);
                        }
                    }
                    processed++;
                } catch (const std::exception& e) {
                    continue;
                }
            }
            OGRFeature::DestroyFeature(feature);
        }

        GDALClose(dataset);
        s2_index_built_ = true;
        std::cout << "Built S2 index with " << s2_cells_.size() << " cells for size=" << build_size << std::endl;
    }

    // 公平的S2查询：每次查询都重新打开数据源（模拟实际使用场景）
    std::vector<int> queryS2IndexFair(const QueryBBox& bbox) {
        std::vector<int> results;

        // 重新打开数据集进行查询（模拟实际使用场景）
        GDALDataset* dataset = (GDALDataset*)GDALOpenEx(data_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!dataset) {
            return results;
        }

        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            GDALClose(dataset);
            return results;
        }

        // 创建S2查询区域
        S2LatLng p1 = S2LatLng::FromDegrees(bbox.min_lat, bbox.min_lng);
        S2LatLng p2 = S2LatLng::FromDegrees(bbox.max_lat, bbox.max_lng);
        S2LatLngRect rect(p1, p2);

        // 使用S2 Region Coverer获取覆盖的单元格
        S2RegionCoverer::Options options;
        options.set_max_cells(1000);
        S2RegionCoverer coverer(options);
        std::vector<S2CellId> covering;
        coverer.GetCovering(rect, &covering);

        // 收集所有匹配的要素
        for (const auto& cell_id : covering) {
            std::string cell_key = std::to_string(cell_id.id());
            auto it = s2_index_.find(cell_key);
            if (it != s2_index_.end()) {
                results.insert(results.end(), it->second.begin(), it->second.end());
            }
        }

        GDALClose(dataset);
        return results;
    }

    // 公平的OGR查询：每次查询都重新打开数据源
    std::vector<int> queryOGRFair(const QueryBBox& bbox) {
        std::vector<int> results;

        // 重新打开数据集进行OGR查询
        GDALDataset* dataset = (GDALDataset*)GDALOpenEx(data_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!dataset) {
            return results;
        }

        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            GDALClose(dataset);
            return results;
        }

        // 设置空间过滤器
        layer->SetSpatialFilterRect(bbox.min_lng, bbox.min_lat, bbox.max_lng, bbox.max_lat);

        // 执行查询
        layer->ResetReading();
        OGRFeature* feature;
        int count = 0;
        while ((feature = layer->GetNextFeature()) != nullptr) {
            results.push_back(count++);
            OGRFeature::DestroyFeature(feature);
        }

        // 清除空间过滤器
        layer->SetSpatialFilter(nullptr);
        GDALClose(dataset);

        return results;
    }

    // 公平的顺序读取：S2也需要从磁盘读取数据
    int sequentialReadS2Fair(int n) {
        // 重新打开数据集
        GDALDataset* dataset = (GDALDataset*)GDALOpenEx(data_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!dataset) {
            return 0;
        }
        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            GDALClose(dataset);
            return 0;
        }

        layer->ResetReading();
        OGRFeature* feature;
        int count = 0;
        while (count < n && (feature = layer->GetNextFeature()) != nullptr) {
            // 模拟S2处理：获取几何体中心点
            OGRGeometry* geometry = feature->GetGeometryRef();
            if (geometry) {
                OGREnvelope envelope;
                geometry->getEnvelope(&envelope);
                double center_lat = (envelope.MinY + envelope.MaxY) / 2.0;
                double center_lng = (envelope.MinX + envelope.MaxX) / 2.0;
                // 模拟S2处理开销
                S2LatLng latlng = S2LatLng::FromDegrees(center_lat, center_lng);
                volatile S2Point s2_point = latlng.ToPoint();
                (void)s2_point;
            }
            ++count;
            OGRFeature::DestroyFeature(feature);
        }
        GDALClose(dataset);
        return count;
    }

    // 公平的顺序读取：OGR直接读取
    int sequentialReadOGRFair(int n) {
        GDALDataset* dataset = (GDALDataset*)GDALOpenEx(data_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!dataset) {
            return 0;
        }
        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            GDALClose(dataset);
            return 0;
        }
        layer->ResetReading();
        OGRFeature* feature;
        int count = 0;
        while (count < n && (feature = layer->GetNextFeature()) != nullptr) {
            ++count;
            OGRFeature::DestroyFeature(feature);
        }
        GDALClose(dataset);
        return count;
    }

    // 公平的随机读取：S2也需要从磁盘读取数据
    int randomReadS2Fair(int n, int k) {
        if (ogr_fids_all_.empty())
            return 0;
        int limit = std::min(n, static_cast<int>(ogr_fids_all_.size()));
        if (limit <= 0)
            return 0;

        GDALDataset* dataset = (GDALDataset*)GDALOpenEx(data_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!dataset) {
            return 0;
        }
        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            GDALClose(dataset);
            return 0;
        }

        std::uniform_int_distribution<int> dist(0, limit - 1);
        int read_count = 0;
        for (int i = 0; i < k; ++i) {
            int idx = dist(rng_);
            long long fid = ogr_fids_all_[idx];
            OGRFeature* feature = layer->GetFeature(static_cast<GIntBig>(fid));
            if (feature) {
                // 模拟S2处理：获取几何体中心点
                OGRGeometry* geometry = feature->GetGeometryRef();
                if (geometry) {
                    OGREnvelope envelope;
                    geometry->getEnvelope(&envelope);
                    double center_lat = (envelope.MinY + envelope.MaxY) / 2.0;
                    double center_lng = (envelope.MinX + envelope.MaxX) / 2.0;
                    // 模拟S2处理开销
                    S2LatLng latlng = S2LatLng::FromDegrees(center_lat, center_lng);
                    volatile S2Point s2_point = latlng.ToPoint();
                    (void)s2_point;
                }
                ++read_count;
                OGRFeature::DestroyFeature(feature);
            }
        }
        GDALClose(dataset);
        return read_count;
    }

    // 公平的随机读取：OGR直接读取
    int randomReadOGRFair(int n, int k) {
        if (ogr_fids_all_.empty())
            return 0;
        int limit = std::min(n, static_cast<int>(ogr_fids_all_.size()));
        if (limit <= 0)
            return 0;

        GDALDataset* dataset = (GDALDataset*)GDALOpenEx(data_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!dataset) {
            return 0;
        }
        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            GDALClose(dataset);
            return 0;
        }

        std::uniform_int_distribution<int> dist(0, limit - 1);
        int read_count = 0;
        for (int i = 0; i < k; ++i) {
            int idx = dist(rng_);
            long long fid = ogr_fids_all_[idx];
            OGRFeature* feature = layer->GetFeature(static_cast<GIntBig>(fid));
            if (feature) {
                ++read_count;
                OGRFeature::DestroyFeature(feature);
            }
        }
        GDALClose(dataset);
        return read_count;
    }

    double measureMemoryUsage() {
        double total_memory = 0.0;

        // S2索引内存
        total_memory += s2_points_.size() * sizeof(S2Point);
        total_memory += s2_cells_.size() * sizeof(S2CellId);

        // S2索引映射内存
        for (const auto& pair : s2_index_) {
            total_memory += pair.first.size() + pair.second.size() * sizeof(int);
        }

        return total_memory / (1024.0 * 1024.0); // Convert to MB
    }

    // 检查内存使用
    size_t getCurrentMemoryUsage() {
        struct rusage r_usage;
        getrusage(RUSAGE_SELF, &r_usage);
        return r_usage.ru_maxrss / 1024; // 转换为MB
    }

    // 检查是否超过内存限制
    bool isMemoryLimitExceeded() { return getCurrentMemoryUsage() > MEMORY_LIMIT_MB; }

    // 显示进度
    void showProgress() {
        if (total_features_ > 0) {
            double progress = (double)processed_features_ / total_features_ * 100.0;
            std::cout << "\r处理进度: " << processed_features_ << "/" << total_features_ << " (" << std::fixed << std::setprecision(1) << progress << "%) " << "内存使用: " << getCurrentMemoryUsage() << "MB"
                      << std::flush;
        }
    }

    BenchmarkResult runBenchmark(const std::string& index_type, const std::string& operation, int data_size) {
        BenchmarkResult result;
        result.index_type = index_type;
        result.operation = operation;
        result.data_size = data_size;
        result.iterations = test_iterations_;
        result.data_source = data_format_;

        std::vector<double> times;
        std::vector<int> result_counts;

        int actual_size = std::min(data_size, (int)s2_points_.size());

        for (int i = 0; i < test_iterations_; ++i) {
            auto start = std::chrono::high_resolution_clock::now();

            if (operation == "build") {
                if (index_type == "s2") {
                    buildS2IndexForSize(actual_size);
                }
            } else if (operation == "spatial_query") {
                std::uniform_int_distribution<int> dist(0, test_bboxes_.size() - 1);
                const QueryBBox& bbox = test_bboxes_[dist(rng_)];
                std::vector<int> results;
                if (index_type == "s2") {
                    results = queryS2IndexFair(bbox);
                } else if (index_type == "ogr") {
                    results = queryOGRFair(bbox);
                }
                result_counts.push_back(static_cast<int>(results.size()));
            } else if (operation == "seq_read") {
                int count = 0;
                if (index_type == "ogr") {
                    count = sequentialReadOGRFair(actual_size);
                } else if (index_type == "s2") {
                    count = sequentialReadS2Fair(actual_size);
                }
                result_counts.push_back(count);
            } else if (operation == "rand_read") {
                int count = 0;
                int reads = std::min(actual_size, 1000); // 限制单次迭代的随机读取次数
                if (reads <= 0)
                    reads = actual_size;
                if (index_type == "ogr") {
                    count = randomReadOGRFair(actual_size, reads);
                } else if (index_type == "s2") {
                    count = randomReadS2Fair(actual_size, reads);
                }
                result_counts.push_back(count);
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            times.push_back(duration.count() / 1000.0); // Convert to milliseconds
        }

        // 计算统计信息
        double sum = 0.0, sum_sq = 0.0;
        for (double time : times) {
            sum += time;
            sum_sq += time * time;
        }

        result.avg_time_ms = sum / test_iterations_;
        result.min_time_ms = *std::min_element(times.begin(), times.end());
        result.max_time_ms = *std::max_element(times.begin(), times.end());
        result.std_dev_ms = sqrt((sum_sq / test_iterations_) - (result.avg_time_ms * result.avg_time_ms));
        result.result_count = result_counts.empty() ? 0 : result_counts[0];
        result.memory_usage_mb = getCurrentMemoryUsage();

        return result;
    }

    std::vector<JsonValue> runFullBenchmark() {
        std::vector<JsonValue> results;

        for (const auto& size : test_sizes_) {
            // 1) 仅对 S2 进行索引构建基准
            {
                std::string index_type = "s2";
                std::string operation = "build";
                std::cout << "Running benchmark: " << index_type << " " << operation << " scale=" << size << " format=" << data_format_ << std::endl;
                BenchmarkResult result = runBenchmark(index_type, operation, size);
                JsonValue result_json("result");
                result_json.addChild(JsonValue("index_type", "\"" + result.index_type + "\""));
                result_json.addChild(JsonValue("operation", "\"" + result.operation + "\""));
                result_json.addChild(JsonValue("data_size", std::to_string(result.data_size)));
                result_json.addChild(JsonValue("data_source", "\"" + result.data_source + "\""));
                result_json.addChild(JsonValue("avg_time_ms", std::to_string(result.avg_time_ms)));
                result_json.addChild(JsonValue("min_time_ms", std::to_string(result.min_time_ms)));
                result_json.addChild(JsonValue("max_time_ms", std::to_string(result.max_time_ms)));
                result_json.addChild(JsonValue("std_dev_ms", std::to_string(result.std_dev_ms)));
                result_json.addChild(JsonValue("result_count", std::to_string(result.result_count)));
                result_json.addChild(JsonValue("memory_usage_mb", std::to_string(result.memory_usage_mb)));
                result_json.addChild(JsonValue("iterations", std::to_string(result.iterations)));
                results.push_back(result_json);
            }

            // 2) 空间过滤、顺序读取、随机读取：对 S2 与 OGR 分别测量
            std::vector<std::string> query_ops = {"spatial_query", "seq_read", "rand_read"};
            std::vector<std::string> index_types = {"s2", "ogr"};
            for (const auto& op : query_ops)
                for (const auto& idx_type : index_types) {
                    std::cout << "Running benchmark: " << idx_type << " " << op << " scale=" << size << " format=" << data_format_ << std::endl;
                    BenchmarkResult result = runBenchmark(idx_type, op, size);
                    JsonValue result_json("result");
                    result_json.addChild(JsonValue("index_type", "\"" + result.index_type + "\""));
                    result_json.addChild(JsonValue("operation", "\"" + result.operation + "\""));
                    result_json.addChild(JsonValue("data_size", std::to_string(result.data_size)));
                    result_json.addChild(JsonValue("data_source", "\"" + result.data_source + "\""));
                    result_json.addChild(JsonValue("avg_time_ms", std::to_string(result.avg_time_ms)));
                    result_json.addChild(JsonValue("min_time_ms", std::to_string(result.min_time_ms)));
                    result_json.addChild(JsonValue("max_time_ms", std::to_string(result.max_time_ms)));
                    result_json.addChild(JsonValue("std_dev_ms", std::to_string(result.std_dev_ms)));
                    result_json.addChild(JsonValue("result_count", std::to_string(result.result_count)));
                    result_json.addChild(JsonValue("memory_usage_mb", std::to_string(result.memory_usage_mb)));
                    result_json.addChild(JsonValue("iterations", std::to_string(result.iterations)));
                    results.push_back(result_json);
                }
        }

        return results;
    }

    void printResults(const std::vector<JsonValue>& results) {
        std::cout << "{\n";
        std::cout << "  \"benchmark_info\": {\n";
        std::cout << "    \"data_path\": \"" << data_path_ << "\",\n";
        std::cout << "    \"data_format\": \"" << data_format_ << "\",\n";
        std::cout << "    \"total_features\": " << total_features_ << ",\n";
        std::cout << "    \"processed_features\": " << processed_features_ << ",\n";
        std::cout << "    \"memory_usage_mb\": " << getCurrentMemoryUsage() << "\n";
        std::cout << "  },\n";
        std::cout << "  \"results\": [\n";

        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << "    " << results[i].toString();
            if (i < results.size() - 1) {
                std::cout << ",";
            }
            std::cout << "\n";
        }

        std::cout << "  ]\n";
        std::cout << "}\n";
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <data_path> [output_file]" << std::endl;
        std::cerr << "Example: " << argv[0] << " /home/chenming/Data/GIS_DATA/filegdb/td_gtbhdc_bg_530000_2020.gdb" << std::endl;
        std::cerr << "Example: " << argv[0] << " /home/chenming/Data/GIS_DATA/shapefile/td_gtbhdc_bg_532300_2020.shp" << std::endl;
        return 1;
    }

    std::string data_path = argv[1];
    std::string output_file = argc > 2 ? argv[2] : "";

    // 检查文件是否存在
    if (!std::filesystem::exists(data_path)) {
        std::cerr << "Error: Data file does not exist: " << data_path << std::endl;
        return 1;
    }

    FairSpatialIndexBenchmark benchmark(data_path);

    if (!benchmark.loadData(data_path)) {
        std::cerr << "Failed to load data" << std::endl;
        return 1;
    }

    std::cout << "\n开始公平对比基准测试..." << std::endl;
    auto results = benchmark.runFullBenchmark();

    if (!output_file.empty()) {
        std::ofstream out_file(output_file);
        if (out_file.is_open()) {
            // 重定向输出到文件
            std::streambuf* orig = std::cout.rdbuf();
            std::cout.rdbuf(out_file.rdbuf());
            benchmark.printResults(results);
            std::cout.rdbuf(orig);
            out_file.close();
            std::cout << "结果已保存到: " << output_file << std::endl;
        } else {
            std::cerr << "无法打开输出文件: " << output_file << std::endl;
        }
    } else {
        benchmark.printResults(results);
    }

    return 0;
}
