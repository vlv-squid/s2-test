//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/optimized_shapefile_converter.h"
#include "gisstorage/geometry_serializer.h"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <chrono>

namespace GisStorage {

    // OptimizedShapefileConverter 实现
    OptimizedShapefileConverter::OptimizedShapefileConverter(const std::string& shapefile_path, const std::string& output_dir)
        : shapefile_path_(shapefile_path)
        , output_dir_(output_dir) {
        // 提取Shapefile名称
        std::filesystem::path path(shapefile_path);
        shapefile_name_ = path.stem().string();

        // 初始化统计信息
        stats_.total_features = 0;
        stats_.valid_features = 0;
        stats_.geometry_size = 0;
        stats_.attribute_original_size = 0;
        stats_.attribute_compressed_size = 0;
        stats_.compression_ratio = 0.0;
        stats_.string_pool_size = 0;
        stats_.string_pool_saved_bytes = 0;
        stats_.conversion_time_seconds = 0.0;

        // 初始化存储文件
        initializeStorageFiles();
    }

    void OptimizedShapefileConverter::initializeStorageFiles() {
        // 创建输出目录
        std::filesystem::create_directories(output_dir_);

        // 初始化存储对象
        std::string geom_file = output_dir_ + "/" + shapefile_name_ + "_geom.dat";
        std::string attr_file = output_dir_ + "/" + shapefile_name_ + "_attr.dat";
        std::string pool_file = output_dir_ + "/" + shapefile_name_ + "_pool.dat";
        index_file_ = output_dir_ + "/" + shapefile_name_ + "_index.dat";

        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file);
        attribute_storage_ = std::make_unique<OptimizedAttributeStorage>(attr_file, pool_file);
    }

    std::vector<uint64_t> OptimizedShapefileConverter::convert() {
        auto start_time = std::chrono::high_resolution_clock::now();

        std::cout << "开始转换Shapefile（优化版本）: " << shapefile_path_ << std::endl;

        // 打开Shapefile
        GDALAllRegister();
        GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(shapefile_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        if (!dataset) {
            throw std::runtime_error("无法打开Shapefile: " + shapefile_path_);
        }

        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            GDALClose(dataset);
            throw std::runtime_error("无法获取图层");
        }

        // 获取字段信息
        OGRFeatureDefn* feature_defn = layer->GetLayerDefn();
        int field_count = feature_defn->GetFieldCount();

        // 构建字段信息JSON
        nlohmann::json field_info;
        for (int i = 0; i < field_count; ++i) {
            OGRFieldDefn* field_defn = feature_defn->GetFieldDefn(i);
            field_info[field_defn->GetNameRef()] = field_defn->GetType();
        }

        // 初始化索引数据
        nlohmann::json index_data;
        index_data["version"] = 1;
        index_data["data"]["features"] = nlohmann::json::object();

        std::vector<uint64_t> valid_fids;
        int total_features = layer->GetFeatureCount();
        stats_.total_features = total_features;
        std::cout << "共 " << total_features << " 个要素" << std::endl;

        // 重置图层
        layer->ResetReading();

        int processed_count = 0;
        OGRFeature* feature;

        while ((feature = layer->GetNextFeature()) != nullptr) {
            uint64_t fid = feature->GetFID();

            try {
                // 提取几何数据
                OGRGeometry* geometry = feature->GetGeometryRef();
                if (!geometry || geometry->IsEmpty()) {
                    OGRFeature::DestroyFeature(feature);
                    continue;
                }

                std::vector<Coordinate> coordinates = extractGeometryCoordinates(geometry);
                if (coordinates.empty()) {
                    OGRFeature::DestroyFeature(feature);
                    continue;
                }

                // 计算边界框
                BBox bbox = calculateBBox(coordinates);

                // 压缩坐标数据
                std::vector<uint8_t> coord_data = encodeCoordinatesDeltaOptimized(coordinates);

                // 确定几何类型
                GeometryType geom_type;
                switch (geometry->getGeometryType()) {
                    case wkbPoint:
                    case wkbPoint25D:
                        geom_type = GeometryType::POINT;
                        break;
                    case wkbLineString:
                    case wkbLineString25D:
                        geom_type = GeometryType::LINE;
                        break;
                    case wkbPolygon:
                    case wkbPolygon25D:
                        geom_type = GeometryType::POLYGON;
                        break;
                    case wkbMultiPoint:
                    case wkbMultiPoint25D:
                        geom_type = GeometryType::MULTIPOINT;
                        break;
                    case wkbMultiLineString:
                    case wkbMultiLineString25D:
                        geom_type = GeometryType::MULTILINE;
                        break;
                    case wkbMultiPolygon:
                    case wkbMultiPolygon25D:
                        geom_type = GeometryType::MULTIPOLYGON;
                        break;
                    default:
                        geom_type = GeometryType::POINT;
                        break;
                }

                // 创建几何数据对象
                GeometryData geom_data(fid, geom_type, coord_data, bbox);

                // 写入几何数据
                int64_t geom_offset = geometry_storage_->writeGeometry(geom_data);

                // 提取属性数据
                std::map<std::string, std::string> properties;
                for (int i = 0; i < field_count; ++i) {
                    OGRFieldDefn* field_defn = feature_defn->GetFieldDefn(i);
                    std::string field_name = field_defn->GetNameRef();

                    if (feature->IsFieldSetAndNotNull(i)) {
                        std::string field_value;
                        switch (field_defn->GetType()) {
                            case OFTInteger:
                                field_value = std::to_string(feature->GetFieldAsInteger(i));
                                break;
                            case OFTInteger64:
                                field_value = std::to_string(feature->GetFieldAsInteger64(i));
                                break;
                            case OFTReal:
                                field_value = std::to_string(feature->GetFieldAsDouble(i));
                                break;
                            case OFTString:
                                field_value = feature->GetFieldAsString(i);
                                break;
                            case OFTDate:
                            case OFTTime:
                            case OFTDateTime:
                                field_value = feature->GetFieldAsString(i);
                                break;
                            default:
                                field_value = feature->GetFieldAsString(i);
                                break;
                        }
                        properties[field_name] = field_value;
                    }
                }

                // 创建属性数据对象
                AttributeData attr_data(fid, properties);

                // 写入属性数据（使用字符串池优化）
                int64_t attr_offset = attribute_storage_->writeAttribute(attr_data);

                // 添加到索引
                std::string fid_str = std::to_string(fid);
                index_data["data"]["features"][fid_str]["geom_offset"] = geom_offset;
                index_data["data"]["features"][fid_str]["attr_offset"] = attr_offset;

                valid_fids.push_back(fid);

            } catch (const std::exception& e) {
                std::cerr << "处理要素 " << fid << " 时出错: " << e.what() << std::endl;
            }

            OGRFeature::DestroyFeature(feature);

            processed_count++;
            if (processed_count % 1000 == 0) {
                std::cout << "已处理 " << processed_count << " 个要素" << std::endl;
            }
        }

        // 保存索引数据
        saveIndexData(index_data);

        // 保存字符串池
        saveStringPool();

        // 更新统计信息
        stats_.valid_features = valid_fids.size();
        auto compression_stats = attribute_storage_->getCompressionStats();
        stats_.string_pool_size = compression_stats.unique_strings;
        stats_.string_pool_saved_bytes = compression_stats.original_size - compression_stats.compressed_size;
        stats_.compression_ratio = compression_stats.compression_ratio;

        // 计算转换时间
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        stats_.conversion_time_seconds = duration.count() / 1000.0;

        // 输出统计信息
        std::cout << "转换完成！" << std::endl;
        std::cout << "  有效要素: " << stats_.valid_features << "/" << stats_.total_features << std::endl;
        std::cout << "  字符串池大小: " << stats_.string_pool_size << " 个唯一字符串" << std::endl;
        std::cout << "  压缩率: " << stats_.compression_ratio << "%" << std::endl;
        std::cout << "  节省空间: " << stats_.string_pool_saved_bytes << " 字节" << std::endl;
        std::cout << "  转换时间: " << stats_.conversion_time_seconds << " 秒" << std::endl;

        // 清理
        GDALClose(dataset);

        return valid_fids;
    }

    std::string OptimizedShapefileConverter::getGeometryFilePath() const {
        return geometry_storage_->getGeometryFilePath();
    }

    std::string OptimizedShapefileConverter::getAttributeFilePath() const {
        return attribute_storage_->getAttributeFilePath();
    }

    std::string OptimizedShapefileConverter::getIndexFilePath() const {
        return index_file_;
    }

    std::string OptimizedShapefileConverter::getStringPoolFilePath() const {
        return attribute_storage_->getStringPoolFilePath();
    }

    OptimizedAttributeSerializer::CompressionStats OptimizedShapefileConverter::getCompressionStats() const {
        return attribute_storage_->getCompressionStats();
    }

    OptimizedShapefileConverter::ConversionStats OptimizedShapefileConverter::getConversionStats() const {
        return stats_;
    }

    BBox OptimizedShapefileConverter::calculateBBox(const std::vector<Coordinate>& coordinates) {
        return GeometrySerializer::calculateBBox(coordinates);
    }

    std::vector<uint8_t> OptimizedShapefileConverter::encodeCoordinatesDelta(const std::vector<Coordinate>& coordinates) {
        return GeometrySerializer::encodeCoordinatesDelta(coordinates);
    }

    std::vector<uint8_t> OptimizedShapefileConverter::encodeCoordinatesDeltaOptimized(const std::vector<Coordinate>& coordinates) {
        if (coordinates.empty()) {
            return {};
        }

        if (coordinates.size() == 1) {
            // 单点情况，直接存储绝对坐标
            std::vector<uint8_t> data(16);
            std::memcpy(&data[0], &coordinates[0].x, sizeof(double));
            std::memcpy(&data[8], &coordinates[0].y, sizeof(double));
            return data;
        }

        // 计算偏移量范围，决定使用哪种数据类型
        double max_delta = 0;
        std::vector<std::pair<float, float>> deltas;
        for (size_t i = 1; i < coordinates.size(); ++i) {
            float dx = static_cast<float>(coordinates[i].x - coordinates[i - 1].x);
            float dy = static_cast<float>(coordinates[i].y - coordinates[i - 1].y);
            deltas.emplace_back(dx, dy);
            max_delta = std::max(max_delta, static_cast<double>(std::max(std::abs(dx), std::abs(dy))));
        }

        // 存储第一个点的绝对坐标
        std::vector<uint8_t> data(16);
        std::memcpy(&data[0], &coordinates[0].x, sizeof(double));
        std::memcpy(&data[8], &coordinates[0].y, sizeof(double));

        // 根据偏移量范围选择合适的存储格式
        if (max_delta < 32767) { // short类型范围
            // 使用short类型存储偏移量（2字节/坐标）
            data.push_back(0); // 标记使用short类型
            for (const auto& delta : deltas) {
                int16_t dx = static_cast<int16_t>(delta.first);
                int16_t dy = static_cast<int16_t>(delta.second);
                data.insert(data.end(), reinterpret_cast<uint8_t*>(&dx), reinterpret_cast<uint8_t*>(&dx) + sizeof(int16_t));
                data.insert(data.end(), reinterpret_cast<uint8_t*>(&dy), reinterpret_cast<uint8_t*>(&dy) + sizeof(int16_t));
            }
        } else if (max_delta < 2147483647) { // int类型范围
            // 使用int类型存储偏移量（4字节/坐标）
            data.push_back(1); // 标记使用int类型
            for (const auto& delta : deltas) {
                int32_t dx = static_cast<int32_t>(delta.first);
                int32_t dy = static_cast<int32_t>(delta.second);
                data.insert(data.end(), reinterpret_cast<uint8_t*>(&dx), reinterpret_cast<uint8_t*>(&dx) + sizeof(int32_t));
                data.insert(data.end(), reinterpret_cast<uint8_t*>(&dy), reinterpret_cast<uint8_t*>(&dy) + sizeof(int32_t));
            }
        } else {
            // 使用float类型存储偏移量（4字节/坐标）
            data.push_back(2); // 标记使用float类型
            for (const auto& delta : deltas) {
                data.insert(data.end(), reinterpret_cast<const uint8_t*>(&delta.first), reinterpret_cast<const uint8_t*>(&delta.first) + sizeof(float));
                data.insert(data.end(), reinterpret_cast<const uint8_t*>(&delta.second), reinterpret_cast<const uint8_t*>(&delta.second) + sizeof(float));
            }
        }

        return data;
    }

    std::vector<Coordinate> OptimizedShapefileConverter::extractGeometryCoordinates(OGRGeometry* geometry) {
        if (!geometry) {
            return {};
        }

        std::vector<Coordinate> coordinates;
        extractCoordinatesRecursive(geometry, coordinates);
        return coordinates;
    }

    std::vector<Coordinate> OptimizedShapefileConverter::extractPointCoordinates(OGRGeometry* geometry) {
        if (!geometry || geometry->getGeometryType() != wkbPoint) {
            return {};
        }

        OGRPoint* point = static_cast<OGRPoint*>(geometry);
        return {{point->getX(), point->getY()}};
    }

    std::vector<Coordinate> OptimizedShapefileConverter::extractLineCoordinates(OGRGeometry* geometry) {
        if (!geometry || geometry->getGeometryType() != wkbLineString) {
            return {};
        }

        OGRLineString* line = static_cast<OGRLineString*>(geometry);
        std::vector<Coordinate> coordinates;
        coordinates.reserve(line->getNumPoints());

        for (int i = 0; i < line->getNumPoints(); ++i) {
            coordinates.push_back({line->getX(i), line->getY(i)});
        }

        return coordinates;
    }

    std::vector<Coordinate> OptimizedShapefileConverter::extractPolygonCoordinates(OGRGeometry* geometry) {
        if (!geometry || geometry->getGeometryType() != wkbPolygon) {
            return {};
        }

        OGRPolygon* polygon = static_cast<OGRPolygon*>(geometry);
        OGRLinearRing* ring = polygon->getExteriorRing();

        if (!ring) {
            return {};
        }

        std::vector<Coordinate> coordinates;
        coordinates.reserve(ring->getNumPoints());

        for (int i = 0; i < ring->getNumPoints(); ++i) {
            coordinates.push_back({ring->getX(i), ring->getY(i)});
        }

        return coordinates;
    }

    void OptimizedShapefileConverter::extractCoordinatesRecursive(OGRGeometry* geometry, std::vector<Coordinate>& coordinates) {
        if (!geometry) {
            return;
        }

        OGRwkbGeometryType geom_type = geometry->getGeometryType();

        switch (geom_type) {
            case wkbPoint:
            case wkbPoint25D: {
                auto point_coords = extractPointCoordinates(geometry);
                coordinates.insert(coordinates.end(), point_coords.begin(), point_coords.end());
                break;
            }
            case wkbLineString:
            case wkbLineString25D: {
                auto line_coords = extractLineCoordinates(geometry);
                coordinates.insert(coordinates.end(), line_coords.begin(), line_coords.end());
                break;
            }
            case wkbPolygon:
            case wkbPolygon25D: {
                auto polygon_coords = extractPolygonCoordinates(geometry);
                coordinates.insert(coordinates.end(), polygon_coords.begin(), polygon_coords.end());
                break;
            }
            case wkbMultiPoint:
            case wkbMultiPoint25D:
            case wkbMultiLineString:
            case wkbMultiLineString25D:
            case wkbMultiPolygon:
            case wkbMultiPolygon25D: {
                OGRGeometryCollection* collection = static_cast<OGRGeometryCollection*>(geometry);
                for (int i = 0; i < collection->getNumGeometries(); ++i) {
                    extractCoordinatesRecursive(collection->getGeometryRef(i), coordinates);
                }
                break;
            }
            default:
                break;
        }
    }

    void OptimizedShapefileConverter::saveIndexData(const nlohmann::json& index_data) {
        std::ofstream file(index_file_);
        if (file.is_open()) {
            file << index_data.dump(4);
            file.close();
            std::cout << "索引文件已保存: " << index_file_ << std::endl;
        } else {
            std::cerr << "无法保存索引文件: " << index_file_ << std::endl;
        }
    }

    void OptimizedShapefileConverter::saveStringPool() {
        attribute_storage_->saveStringPool();
    }

    void OptimizedShapefileConverter::updateStats(size_t geom_size, size_t attr_original_size, size_t attr_compressed_size) {
        stats_.geometry_size += geom_size;
        stats_.attribute_original_size += attr_original_size;
        stats_.attribute_compressed_size += attr_compressed_size;
    }

} // namespace GisStorage
