#include <iostream>
#include <chrono>
#include <vector>
#include <map>
#include <string>
#include <fstream>
#include <random>
#include <memory>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <sys/resource.h>

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
};

struct QueryBBox {
    double min_lat, min_lng, max_lat, max_lng;
    std::string name;
};

class SpatialIndexBenchmark {
  private:
    std::string data_path_;
    std::vector<OGRFeature*> features_;
    std::vector<S2Point> s2_points_;
    std::vector<S2CellId> s2_cells_;
    std::map<std::string, std::vector<int>> s2_index_;

    // 测试配置 - 扩展到千万级别测试规模
    std::vector<int> test_sizes_ = {100, 500, 1000, 5000, 10000, 50000, 100000, 500000, 1000000, 5000000, 10000000};
    std::vector<QueryBBox> test_bboxes_ = {{26.4297, 103.2504, 26.4747, 103.3028, "小范围查询"}, {26.0, 103.0, 27.0, 104.0, "中等范围查询"}, {25.0, 102.0, 28.0, 105.0, "大范围查询"}};
    int test_iterations_ = 5;

    // 随机数生成器
    std::mt19937 rng_;

    // 内存优化配置
    static constexpr size_t MAX_DATA_POINTS = 100000000; // 增加最大数据点数量
    static constexpr size_t MEMORY_LIMIT_MB = 8192;      // 增加内存限制 (MB)

    // 进度显示
    size_t total_features_ = 0;
    size_t processed_features_ = 0;

  public:
    SpatialIndexBenchmark(const std::string& data_path)
        : data_path_(data_path)
        , rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
        // 初始化GDAL
        GDALAllRegister();
    }

    ~SpatialIndexBenchmark() {
        // 清理资源
        for (auto feature : features_) {
            OGRFeature::DestroyFeature(feature);
        }
    }

    bool loadData(const std::string& data_path) {
        std::cout << "Loading data from: " << data_path << std::endl;

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
                    if (processed % 1000 == 0) {
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

        GDALClose(dataset);
        return processed > 0;
    }

    void buildS2Index() {
        // S2索引已经在loadData中构建
        std::cout << "Built S2 index with " << s2_cells_.size() << " cells" << std::endl;
    }

    std::vector<int> queryS2Index(const QueryBBox& bbox) {
        std::vector<int> results;

        // 创建S2查询区域 - 修复坐标顺序：纬度在前，经度在后
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

        return results;
    }

    std::vector<int> queryOGR(const QueryBBox& bbox) {
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

        std::vector<double> times;
        std::vector<int> result_counts;

        int actual_size = std::min(data_size, (int)s2_points_.size());

        for (int i = 0; i < test_iterations_; ++i) {
            auto start = std::chrono::high_resolution_clock::now();

            if (operation == "build") {
                if (index_type == "s2") {
                    buildS2Index();
                }
            } else if (operation == "query") {
                // 使用随机查询边界框
                std::uniform_int_distribution<int> dist(0, test_bboxes_.size() - 1);
                const QueryBBox& bbox = test_bboxes_[dist(rng_)];

                std::vector<int> results;
                if (index_type == "s2") {
                    results = queryS2Index(bbox);
                } else if (index_type == "ogr") {
                    results = queryOGR(bbox);
                }

                result_counts.push_back(results.size());
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

        if (operation == "query" && !result_counts.empty()) {
            result.result_count = std::accumulate(result_counts.begin(), result_counts.end(), 0) / result_counts.size();
        }

        result.memory_usage_mb = measureMemoryUsage();

        return result;
    }

    std::vector<JsonValue> runFullBenchmark() {
        std::vector<JsonValue> results;

        std::vector<std::string> index_types = {"s2", "ogr"};
        std::vector<std::string> operations = {"build", "query"};

        for (const auto& size : test_sizes_) {
            for (const auto& index_type : index_types) {
                for (const auto& operation : operations) {
                    std::cout << "Running benchmark: " << index_type << " " << operation << " scale=" << size << std::endl;

                    BenchmarkResult result = runBenchmark(index_type, operation, size);

                    JsonValue result_json("result");
                    result_json.addChild(JsonValue("index_type", "\"" + result.index_type + "\""));
                    result_json.addChild(JsonValue("operation", "\"" + result.operation + "\""));
                    result_json.addChild(JsonValue("data_size", std::to_string(result.data_size)));
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
        }

        return results;
    }

    void printResults(const std::vector<JsonValue>& results) {
        std::cout << "\n=== Benchmark Results ===" << std::endl;

        for (const auto& result : results) {
            std::cout << std::fixed << std::setprecision(2);
            // Extract values from JsonValue for display
            std::string index_type, operation;
            int data_size;
            double avg_time, std_dev, memory_usage;
            int result_count;

            for (const auto& child : result.children) {
                if (child.key == "index_type")
                    index_type = child.value.substr(1, child.value.length() - 2);
                else if (child.key == "operation")
                    operation = child.value.substr(1, child.value.length() - 2);
                else if (child.key == "data_size")
                    data_size = std::stoi(child.value);
                else if (child.key == "avg_time_ms")
                    avg_time = std::stod(child.value);
                else if (child.key == "std_dev_ms")
                    std_dev = std::stod(child.value);
                else if (child.key == "memory_usage_mb")
                    memory_usage = std::stod(child.value);
                else if (child.key == "result_count")
                    result_count = std::stoi(child.value);
            }

            std::cout << index_type << " " << operation << " " << "scale=" << data_size << ": " << avg_time << "ms " << "(±" << std_dev << "ms) " << "memory=" << memory_usage << "MB";

            if (operation == "query") {
                std::cout << " results=" << result_count;
            }
            std::cout << std::endl;
        }
    }

    void saveResults(const std::vector<JsonValue>& results, const std::string& output_file) {
        std::ofstream file(output_file);
        if (!file.is_open()) {
            std::cerr << "Failed to open output file: " << output_file << std::endl;
            return;
        }

        // 使用流式JSON输出以减少内存使用
        file << "{\n";
        file << "  \"benchmark_info\": {\n";
        file << "    \"data_path\": \"" << data_path_ << "\",\n";
        file << "    \"total_features\": " << total_features_ << ",\n";
        file << "    \"processed_features\": " << processed_features_ << ",\n";
        file << "    \"memory_usage_mb\": " << getCurrentMemoryUsage() << "\n";
        file << "  },\n";
        file << "  \"results\": [\n";

        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0)
                file << ",\n";
            file << "    {\n";

            for (size_t j = 0; j < results[i].children.size(); ++j) {
                file << "      \"" << results[i].children[j].key << "\": " << results[i].children[j].value;
                if (j < results[i].children.size() - 1)
                    file << ",";
                file << "\n";
            }

            file << "    }";
        }

        file << "\n  ]\n";
        file << "}\n";

        file.close();
        std::cout << "Results saved to: " << output_file << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <data_path> <output_file>" << std::endl;
        return 1;
    }

    std::string data_path = argv[1];
    std::string output_file = argv[2];

    std::cout << "Starting Spatial Index Benchmark..." << std::endl;
    std::cout << "Data path: " << data_path << std::endl;
    std::cout << "Output file: " << output_file << std::endl;

    SpatialIndexBenchmark benchmark(data_path);

    if (!benchmark.loadData(data_path)) {
        std::cerr << "Failed to load data" << std::endl;
        return 1;
    }

    auto results = benchmark.runFullBenchmark();
    benchmark.printResults(results);
    benchmark.saveResults(results, output_file);

    std::cout << "\nBenchmark completed successfully!" << std::endl;
    return 0;
}
