//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/ogr_format_converter.h"
#include "gisstorage/geometry_serializer.h"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <set>
#include <algorithm>

namespace GisStorage {

    // OGRFormatConverter 实现
    OGRFormatConverter::OGRFormatConverter(const std::string& ogr_file_path, const std::string& output_dir)
        : ogr_file_path_(ogr_file_path)
        , output_dir_(output_dir) {
        // 提取OGR文件名称
        std::filesystem::path path(ogr_file_path);
        ogr_file_name_ = path.stem().string();

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

        // 初始化空间范围（将从图层直接获取）
        dataset_spatial_extent_.min_x = 0.0;
        dataset_spatial_extent_.min_y = 0.0;
        dataset_spatial_extent_.max_x = 0.0;
        dataset_spatial_extent_.max_y = 0.0;

        // 初始化存储文件
        initializeStorageFiles();
    }

    void OGRFormatConverter::initializeStorageFiles() {
        // 创建输出目录
        std::filesystem::create_directories(output_dir_);

        // 初始化存储对象 - 使用标准扩展名（与GisStorageSystem保持一致）
        std::string geom_file = output_dir_ + "/" + ogr_file_name_ + ".geom";
        std::string attr_file = output_dir_ + "/" + ogr_file_name_ + ".attr";
        std::string pool_file = output_dir_ + "/" + ogr_file_name_ + ".pool";
        index_file_ = output_dir_ + "/" + ogr_file_name_ + ".idx";

        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file, pool_file);
    }

    std::vector<uint64_t> OGRFormatConverter::convert() {
        auto start_time = std::chrono::high_resolution_clock::now();

        std::cout << "开始转换OGR格式（优化版本）: " << ogr_file_path_ << std::endl;

        // 打开OGR文件
        GDALAllRegister();

        // 设置字符编码选项，特别是对于Shapefile格式
        std::string file_extension = std::filesystem::path(ogr_file_path_).extension().string();
        std::transform(file_extension.begin(), file_extension.end(), file_extension.begin(), ::tolower);

        if (file_extension == ".shp") {
            // 设置Shapefile的字符编码为UTF-8
            CPLSetConfigOption("SHAPE_ENCODING", "UTF-8");
            std::cout << "设置输入Shapefile字符编码为UTF-8" << std::endl;
        }

        GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(ogr_file_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        if (!dataset) {
            throw std::runtime_error("无法打开OGR格式文件: " + ogr_file_path_);
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

        // 提取坐标系统信息
        std::string source_crs_info = "";
        std::string target_crs_info = "";
        std::string transformation_info = "";

        OGRSpatialReference* spatial_ref = layer->GetSpatialRef();
        if (spatial_ref) {
            // 获取EPSG代码
            const char* authority_name = spatial_ref->GetAuthorityName(nullptr);
            const char* authority_code = spatial_ref->GetAuthorityCode(nullptr);
            if (authority_name && authority_code) {
                source_crs_info = std::string(authority_name) + ":" + std::string(authority_code);
            }

            // 获取坐标系统名称
            const char* crs_name = spatial_ref->GetName();
            if (crs_name) {
                if (!source_crs_info.empty()) {
                    source_crs_info += " - " + std::string(crs_name);
                } else {
                    source_crs_info = std::string(crs_name);
                }
            }

            // 如果没有获取到坐标系统信息，尝试从WKT获取
            if (source_crs_info.empty()) {
                char* wkt = nullptr;
                if (spatial_ref->exportToWkt(&wkt) == OGRERR_NONE) {
                    source_crs_info = std::string(wkt);
                    CPLFree(wkt);
                }
            }

            // 目标坐标系统与源坐标系统相同（没有进行坐标转换）
            target_crs_info = source_crs_info;
            transformation_info = "未进行坐标转换，保持原始坐标系统";
        } else {
            source_crs_info = "未知坐标系统";
            target_crs_info = "未知坐标系统";
            transformation_info = "未进行坐标转换";
        }

        std::cout << "源坐标系统: " << source_crs_info << std::endl;
        std::cout << "目标坐标系统: " << target_crs_info << std::endl;
        std::cout << "转换信息: " << transformation_info << std::endl;

        // 从图层直接获取空间范围（原始数据源的bbox）
        OGREnvelope layer_extent;
        if (layer->GetExtent(&layer_extent) == OGRERR_NONE) {
            dataset_spatial_extent_.min_x = layer_extent.MinX;
            dataset_spatial_extent_.min_y = layer_extent.MinY;
            dataset_spatial_extent_.max_x = layer_extent.MaxX;
            dataset_spatial_extent_.max_y = layer_extent.MaxY;
            std::cout << "数据源空间范围: [" << dataset_spatial_extent_.min_x << ", " << dataset_spatial_extent_.min_y << " - " << dataset_spatial_extent_.max_x << ", " << dataset_spatial_extent_.max_y << "]"
                      << std::endl;
        } else {
            std::cout << "警告: 无法获取图层空间范围" << std::endl;
        }

        // 字段定义将在转换结束后保存

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

        // 用于跟踪FID映射，避免冲突
        std::set<uint64_t> used_fids;
        uint64_t next_available_fid = 1;

        while ((feature = layer->GetNextFeature()) != nullptr) {
            uint64_t original_fid = feature->GetFID();
            uint64_t fid = original_fid;

            // 处理FID为0或重复的情况
            if (fid == 0 || used_fids.find(fid) != used_fids.end()) {
                // 找到下一个可用的FID
                while (used_fids.find(next_available_fid) != used_fids.end()) {
                    next_available_fid++;
                }
                fid = next_available_fid;
                next_available_fid++;

                if (original_fid == 0) {
                    std::cout << "警告: FID为0的要素映射为 " << fid << std::endl;
                } else {
                    std::cout << "警告: 重复FID " << original_fid << " 映射为 " << fid << std::endl;
                }
            }

            // 记录使用的FID
            used_fids.insert(fid);

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

                // 计算边界框（仅用于几何数据存储）
                BBox bbox = calculateBBox(coordinates);

                // 调试输出（每100000个要素刷新一次进度）
                if (processed_count % 100000 == 0) {
                    std::cout << "\r已处理 " << processed_count << " 个要素" << std::flush;
                }

                // 压缩坐标数据
                std::vector<uint8_t> coord_data = encodeCoordinatesDelta(coordinates);

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
        std::cout << std::endl; // 换行，结束进度显示
        std::cout << "转换完成！" << std::endl;
        std::cout << "  有效要素: " << stats_.valid_features << "/" << stats_.total_features << std::endl;
        std::cout << "  字符串池大小: " << stats_.string_pool_size << " 个唯一字符串" << std::endl;
        std::cout << "  压缩率: " << stats_.compression_ratio << "%" << std::endl;
        std::cout << "  节省空间: " << stats_.string_pool_saved_bytes << " 字节" << std::endl;
        std::cout << "  转换时间: " << stats_.conversion_time_seconds << " 秒" << std::endl;

        // 清理
        GDALClose(dataset);

        // 保存元数据（包括字段定义、空间范围和坐标系统信息）
        saveMetadata(field_info, source_crs_info, target_crs_info);

        return valid_fids;
    }

    std::string OGRFormatConverter::getGeometryFilePath() const {
        return geometry_storage_->getGeometryFilePath();
    }

    std::string OGRFormatConverter::getAttributeFilePath() const {
        return attribute_storage_->getAttributeFilePath();
    }

    std::string OGRFormatConverter::getIndexFilePath() const {
        return index_file_;
    }

    std::string OGRFormatConverter::getStringPoolFilePath() const {
        return attribute_storage_->getStringPoolFilePath();
    }

    AttributeSerializer::CompressionStats OGRFormatConverter::getCompressionStats() const {
        return attribute_storage_->getCompressionStats();
    }

    OGRFormatConverter::ConversionStats OGRFormatConverter::getConversionStats() const {
        return stats_;
    }

    bool OGRFormatConverter::convertToOGR(const std::string& output_path, const std::string& output_format) {
        std::cout << "开始逆向转换到OGR格式: " << output_format << std::endl;
        std::cout << "输出路径: " << output_path << std::endl;

        try {
            // 加载自定义格式数据（如果尚未加载）
            if (loaded_geometries_.empty() || loaded_attributes_.empty() || metadata_.empty()) {
                if (!loadCustomFormatData()) {
                    std::cerr << "加载自定义格式数据失败" << std::endl;
                    return false;
                }
            }

            // 注册GDAL驱动
            GDALAllRegister();

            // 设置字符编码选项，特别是对于Shapefile格式
            if (output_format == "ESRI Shapefile") {
                // 设置Shapefile的字符编码为UTF-8
                CPLSetConfigOption("SHAPE_ENCODING", "UTF-8");
                std::cout << "设置Shapefile字符编码为UTF-8" << std::endl;
            }

            // 获取输出驱动
            GDALDriver* driver = GetGDALDriverManager()->GetDriverByName(output_format.c_str());
            if (!driver) {
                std::cerr << "不支持的输出格式: " << output_format << std::endl;
                return false;
            }

            // 创建输出数据集
            GDALDataset* dataset = driver->Create(output_path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
            if (!dataset) {
                std::cerr << "无法创建输出数据集: " << output_path << std::endl;
                return false;
            }

            // 创建图层
            OGRSpatialReference* spatial_ref = nullptr;

            // 尝试从元数据恢复坐标系统
            if (metadata_.contains("source_coordinate_system")) {
                std::string crs_info = metadata_["source_coordinate_system"];
                spatial_ref = new OGRSpatialReference();

                // 尝试解析EPSG代码
                if (crs_info.find("EPSG:") != std::string::npos) {
                    size_t pos = crs_info.find("EPSG:");
                    std::string epsg_code = crs_info.substr(pos + 5);
                    size_t space_pos = epsg_code.find(' ');
                    if (space_pos != std::string::npos) {
                        epsg_code = epsg_code.substr(0, space_pos);
                    }
                    try {
                        int epsg = std::stoi(epsg_code);
                        spatial_ref->importFromEPSG(epsg);
                    } catch (const std::exception& e) {
                        std::cerr << "无法解析EPSG代码: " << epsg_code << std::endl;
                        delete spatial_ref;
                        spatial_ref = nullptr;
                    }
                }
            }

            // 确定几何类型（从第一个要素推断）
            OGRwkbGeometryType geom_type = wkbUnknown;
            if (!feature_ids_.empty()) {
                auto geom_it = loaded_geometries_.find(feature_ids_[0]);
                if (geom_it != loaded_geometries_.end()) {
                    switch (geom_it->second->getGeometryType()) {
                        case GeometryType::POINT:
                            geom_type = wkbPoint;
                            break;
                        case GeometryType::LINE:
                            geom_type = wkbLineString;
                            break;
                        case GeometryType::POLYGON:
                            geom_type = wkbPolygon;
                            break;
                        case GeometryType::MULTIPOINT:
                            geom_type = wkbMultiPoint;
                            break;
                        case GeometryType::MULTILINE:
                            geom_type = wkbMultiLineString;
                            break;
                        case GeometryType::MULTIPOLYGON:
                            geom_type = wkbMultiPolygon;
                            break;
                    }
                }
            }

            OGRLayer* layer = dataset->CreateLayer(ogr_file_name_.c_str(), spatial_ref, geom_type, nullptr);
            if (!layer) {
                std::cerr << "无法创建图层" << std::endl;
                if (spatial_ref)
                    delete spatial_ref;
                GDALClose(dataset);
                return false;
            }

            // 创建字段定义
            if (metadata_.contains("field_definitions")) {
                auto field_defs = metadata_["field_definitions"];
                for (auto& [field_name, field_type] : field_defs.items()) {
                    OGRFieldType ogr_field_type = OFTString; // 默认字符串类型

                    // 根据存储的类型转换为OGR类型
                    if (field_type.is_number_integer()) {
                        if (field_type.get<int>() == 0) { // OFTInteger
                            ogr_field_type = OFTInteger;
                        } else if (field_type.get<int>() == 1) { // OFTInteger64
                            ogr_field_type = OFTInteger64;
                        }
                    } else if (field_type.is_number_float()) {
                        ogr_field_type = OFTReal;
                    }

                    OGRFieldDefn field_defn(field_name.c_str(), ogr_field_type);
                    if (layer->CreateField(&field_defn) != OGRERR_NONE) {
                        std::cerr << "创建字段失败: " << field_name << std::endl;
                    }
                }
            }

            // 获取字段定义
            OGRFeatureDefn* feature_defn = layer->GetLayerDefn();

            // 写入要素
            int success_count = 0;
            for (uint64_t fid : feature_ids_) {
                auto geom_it = loaded_geometries_.find(fid);
                auto attr_it = loaded_attributes_.find(fid);

                if (geom_it == loaded_geometries_.end() || attr_it == loaded_attributes_.end()) {
                    continue;
                }

                OGRFeature* feature = createOGRFeature(*geom_it->second, *attr_it->second, feature_defn, output_format);
                if (feature) {
                    OGRErr err = layer->CreateFeature(feature);
                    if (err == OGRERR_NONE) {
                        success_count++;
                    } else {
                        std::cerr << "创建要素失败，错误代码: " << err << "，FID: " << fid << std::endl;
                    }
                    OGRFeature::DestroyFeature(feature);
                } else {
                    std::cerr << "创建OGR要素对象失败，FID: " << fid << std::endl;
                }
            }

            std::cout << "逆向转换完成！成功转换 " << success_count << " 个要素" << std::endl;

            // 对于Shapefile格式，生成CPG文件以指定字符编码
            if (output_format == "ESRI Shapefile") {
                createCPGFile(output_path);
            }

            // 清理
            if (spatial_ref)
                delete spatial_ref;
            GDALClose(dataset);

            return success_count > 0;

        } catch (const std::exception& e) {
            std::cerr << "逆向转换过程中出错: " << e.what() << std::endl;
            return false;
        }
    }

    void OGRFormatConverter::clearLoadedData() {
        loaded_geometries_.clear();
        loaded_attributes_.clear();
        feature_ids_.clear();
        metadata_.clear();

        // 重新初始化存储对象，确保从干净状态开始
        initializeStorageFiles();
    }

    bool OGRFormatConverter::convertToMultipleFormats(const std::string& output_dir, const std::vector<std::string>& formats) {
        std::cout << "开始批量逆向转换到多种格式" << std::endl;

        // 确保数据已加载
        if (loaded_geometries_.empty() || loaded_attributes_.empty() || metadata_.empty()) {
            if (!loadCustomFormatData()) {
                std::cerr << "加载自定义格式数据失败" << std::endl;
                return false;
            }
        }

        bool all_success = true;
        for (const std::string& format : formats) {
            std::string output_path;

            // 根据格式确定输出路径和扩展名，使用格式名称作为后缀
            if (format == "ESRI Shapefile") {
                output_path = output_dir + "/" + ogr_file_name_ + "_reverse.shp";
            } else if (format == "OpenFileGDB" || format == "FileGDB") {
                output_path = output_dir + "/" + ogr_file_name_ + "_reverse.gdb";
            } else if (format == "GPKG") {
                output_path = output_dir + "/" + ogr_file_name_ + "_reverse.gpkg";
            } else if (format == "GeoJSON") {
                output_path = output_dir + "/" + ogr_file_name_ + "_reverse.geojson";
            } else {
                output_path = output_dir + "/" + ogr_file_name_ + "_reverse.out";
            }

            std::cout << "转换到格式: " << format << " -> " << output_path << std::endl;

            // 直接使用当前实例进行转换，避免重复创建转换器
            if (!convertToOGR(output_path, format)) {
                std::cerr << "转换到 " << format << " 失败" << std::endl;
                all_success = false;
            }
        }

        return all_success;
    }

    BBox OGRFormatConverter::calculateBBox(const std::vector<Coordinate>& coordinates) {
        return GeometrySerializer::calculateBBox(coordinates);
    }

    std::vector<uint8_t> OGRFormatConverter::encodeCoordinatesDelta(const std::vector<Coordinate>& coordinates) {
        return GeometrySerializer::encodeCoordinatesDelta(coordinates);
    }

    std::vector<Coordinate> OGRFormatConverter::extractGeometryCoordinates(OGRGeometry* geometry) {
        if (!geometry) {
            return {};
        }

        std::vector<Coordinate> coordinates;
        extractCoordinatesRecursive(geometry, coordinates);
        return coordinates;
    }

    std::vector<Coordinate> OGRFormatConverter::extractPointCoordinates(OGRGeometry* geometry) {
        if (!geometry || geometry->getGeometryType() != wkbPoint) {
            return {};
        }

        OGRPoint* point = static_cast<OGRPoint*>(geometry);
        return {{point->getX(), point->getY()}};
    }

    std::vector<Coordinate> OGRFormatConverter::extractLineCoordinates(OGRGeometry* geometry) {
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

    std::vector<Coordinate> OGRFormatConverter::extractPolygonCoordinates(OGRGeometry* geometry) {
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

    void OGRFormatConverter::extractCoordinatesRecursive(OGRGeometry* geometry, std::vector<Coordinate>& coordinates) {
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

    void OGRFormatConverter::saveIndexData(const nlohmann::json& index_data) {
        std::ofstream file(index_file_);
        if (file.is_open()) {
            file << index_data.dump(4);
            file.close();
            std::cout << "索引文件已保存: " << index_file_ << std::endl;
        } else {
            std::cerr << "无法保存索引文件: " << index_file_ << std::endl;
        }
    }

    void OGRFormatConverter::saveStringPool() {
        attribute_storage_->saveStringPool();
    }

    void OGRFormatConverter::updateStats(size_t geom_size, size_t attr_original_size, size_t attr_compressed_size) {
        stats_.geometry_size += geom_size;
        stats_.attribute_original_size += attr_original_size;
        stats_.attribute_compressed_size += attr_compressed_size;
    }

    void OGRFormatConverter::saveMetadata(const nlohmann::json& field_info, const std::string& source_crs, const std::string& target_crs) {
        // 创建元数据文件路径
        std::string metadata_file = output_dir_ + "/" + ogr_file_name_ + "_meta.json";

        // 读取现有元数据（如果存在）
        nlohmann::json metadata;
        if (std::filesystem::exists(metadata_file)) {
            std::ifstream file(metadata_file);
            if (file.is_open()) {
                try {
                    file >> metadata;
                } catch (const std::exception& e) {
                    std::cerr << "读取元数据文件失败: " << e.what() << std::endl;
                }
            }
        }

        // 更新字段定义
        metadata["field_definitions"] = field_info;

        // 更新源文件信息
        metadata["source_file"] = std::filesystem::path(ogr_file_path_).filename().string();

        // 获取实际的驱动名称
        std::string source_format = "Unknown";
        GDALDataset* temp_dataset = static_cast<GDALDataset*>(GDALOpenEx(ogr_file_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        if (temp_dataset) {
            GDALDriver* driver = temp_dataset->GetDriver();
            if (driver) {
                const char* driver_name = driver->GetDescription();
                if (driver_name) {
                    source_format = std::string(driver_name);
                }
            }
            GDALClose(temp_dataset);
        }
        metadata["source_format"] = source_format;

        // 更新坐标系统信息
        metadata["source_coordinate_system"] = source_crs;
        metadata["target_coordinate_system"] = target_crs;

        // 更新空间范围
        nlohmann::json spatial_extent_json;
        spatial_extent_json["min_x"] = dataset_spatial_extent_.min_x;
        spatial_extent_json["min_y"] = dataset_spatial_extent_.min_y;
        spatial_extent_json["max_x"] = dataset_spatial_extent_.max_x;
        spatial_extent_json["max_y"] = dataset_spatial_extent_.max_y;
        metadata["spatial_extent"] = spatial_extent_json;

        // 添加创建时间
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        metadata["creation_date"] = ss.str();

        // 添加压缩信息
        auto compression_stats = attribute_storage_->getCompressionStats();
        nlohmann::json compression_info;
        compression_info["compression_ratio"] = compression_stats.compression_ratio;
        compression_info["original_size"] = compression_stats.original_size;
        compression_info["compressed_size"] = compression_stats.compressed_size;
        compression_info["unique_strings"] = compression_stats.unique_strings;
        compression_info["total_strings"] = compression_stats.total_strings;
        compression_info["saved_bytes"] = compression_stats.original_size - compression_stats.compressed_size;
        metadata["compression_info"] = compression_info;

        // 添加基本统计信息
        metadata["total_features"] = stats_.total_features;
        metadata["valid_features"] = stats_.valid_features;
        metadata["conversion_time_seconds"] = stats_.conversion_time_seconds;

        // 保存元数据
        std::ofstream file(metadata_file);
        if (file.is_open()) {
            file << metadata.dump(4);
            file.close();
            std::cout << "元数据已保存到文件: " << metadata_file << std::endl;
            std::cout << "  源格式: " << source_format << std::endl;
            std::cout << "  字段定义: " << field_info.size() << " 个字段" << std::endl;
            std::cout << "  源坐标系统: " << source_crs << std::endl;
            std::cout << "  目标坐标系统: " << target_crs << std::endl;
            std::cout << "  空间范围: [" << std::fixed << std::setprecision(6) << dataset_spatial_extent_.min_x << ", " << dataset_spatial_extent_.min_y << " - " << dataset_spatial_extent_.max_x << ", "
                      << dataset_spatial_extent_.max_y << "]" << std::endl;
            std::cout << "  创建时间: " << metadata["creation_date"] << std::endl;
            std::cout << "  压缩率: " << compression_stats.compression_ratio << "%" << std::endl;
        } else {
            std::cerr << "无法保存元数据文件: " << metadata_file << std::endl;
        }
    }

    bool OGRFormatConverter::loadCustomFormatData() {
        std::cout << "加载自定义格式数据..." << std::endl;

        try {
            // 清理之前的数据
            loaded_geometries_.clear();
            loaded_attributes_.clear();
            feature_ids_.clear();

            // 加载元数据
            metadata_ = loadMetadata();
            if (metadata_.empty()) {
                std::cerr << "无法加载元数据" << std::endl;
                return false;
            }

            // 加载索引数据
            std::ifstream index_file(index_file_);
            if (!index_file.is_open()) {
                std::cerr << "无法打开索引文件: " << index_file_ << std::endl;
                return false;
            }

            nlohmann::json index_data;
            index_file >> index_data;
            index_file.close();

            // 获取所有要素ID
            if (index_data.contains("data") && index_data["data"].contains("features")) {
                auto features = index_data["data"]["features"];
                for (auto& [fid_str, feature_info] : features.items()) {
                    uint64_t fid = std::stoull(fid_str);
                    feature_ids_.push_back(fid);
                }
            }

            std::cout << "找到 " << feature_ids_.size() << " 个要素" << std::endl;

            // 批量加载几何和属性数据
            for (uint64_t fid : feature_ids_) {
                auto geometry = geometry_storage_->readGeometry(fid);
                if (geometry) {
                    loaded_geometries_[fid] = std::move(geometry);
                }

                auto attribute = attribute_storage_->readAttribute(fid);
                if (attribute) {
                    loaded_attributes_[fid] = std::move(attribute);
                }
            }

            std::cout << "成功加载 " << loaded_geometries_.size() << " 个几何要素" << std::endl;
            std::cout << "成功加载 " << loaded_attributes_.size() << " 个属性要素" << std::endl;

            return !loaded_geometries_.empty() && !loaded_attributes_.empty();

        } catch (const std::exception& e) {
            std::cerr << "加载自定义格式数据时出错: " << e.what() << std::endl;
            return false;
        }
    }

    OGRGeometry* OGRFormatConverter::createOGRGeometry(const GeometryData& geom_data) {
        try {
            // 解码坐标数据
            std::vector<Coordinate> coordinates = geom_data.decodeCoordinates();
            if (coordinates.empty()) {
                std::cerr << "警告: 几何数据解码后为空，数据大小: " << geom_data.getCoordinates().size() << std::endl;
                return nullptr;
            }

            // 调试信息
            if (coordinates.size() == 1) {
                std::cout << "解码得到单点: (" << coordinates[0].x << ", " << coordinates[0].y << ")，几何类型: " << static_cast<int>(geom_data.getGeometryType()) << std::endl;
            } else {
                std::cout << "解码得到 " << coordinates.size() << " 个坐标点，几何类型: " << static_cast<int>(geom_data.getGeometryType()) << std::endl;
            }

            OGRGeometry* geometry = nullptr;

            switch (geom_data.getGeometryType()) {
                case GeometryType::POINT: {
                    if (coordinates.size() >= 1) {
                        geometry = new OGRPoint(coordinates[0].x, coordinates[0].y);
                    }
                    break;
                }
                case GeometryType::LINE: {
                    OGRLineString* line = new OGRLineString();
                    for (const auto& coord : coordinates) {
                        line->addPoint(coord.x, coord.y);
                    }
                    geometry = line;
                    break;
                }
                case GeometryType::POLYGON: {
                    if (coordinates.size() >= 3) {
                        OGRPolygon* polygon = new OGRPolygon();
                        OGRLinearRing* ring = new OGRLinearRing();
                        for (const auto& coord : coordinates) {
                            ring->addPoint(coord.x, coord.y);
                        }
                        // 确保环是闭合的
                        if (coordinates.size() > 0) {
                            const auto& first = coordinates[0];
                            const auto& last = coordinates.back();
                            if (first.x != last.x || first.y != last.y) {
                                ring->addPoint(first.x, first.y);
                            }
                        }
                        ring->closeRings();
                        polygon->addRingDirectly(ring);
                        geometry = polygon;
                    }
                    break;
                }
                case GeometryType::MULTIPOINT: {
                    OGRMultiPoint* multi_point = new OGRMultiPoint();
                    for (const auto& coord : coordinates) {
                        OGRPoint* point = new OGRPoint(coord.x, coord.y);
                        multi_point->addGeometryDirectly(point);
                    }
                    geometry = multi_point;
                    break;
                }
                case GeometryType::MULTILINE: {
                    // 简化处理：将所有坐标作为一条线
                    OGRMultiLineString* multi_line = new OGRMultiLineString();
                    OGRLineString* line = new OGRLineString();
                    for (const auto& coord : coordinates) {
                        line->addPoint(coord.x, coord.y);
                    }
                    multi_line->addGeometryDirectly(line);
                    geometry = multi_line;
                    break;
                }
                case GeometryType::MULTIPOLYGON: {
                    // 简化处理：将所有坐标作为一个多边形
                    if (coordinates.size() >= 3) {
                        OGRMultiPolygon* multi_polygon = new OGRMultiPolygon();
                        OGRPolygon* polygon = new OGRPolygon();
                        OGRLinearRing* ring = new OGRLinearRing();
                        for (const auto& coord : coordinates) {
                            ring->addPoint(coord.x, coord.y);
                        }
                        // 确保环是闭合的
                        if (coordinates.size() > 0) {
                            const auto& first = coordinates[0];
                            const auto& last = coordinates.back();
                            if (first.x != last.x || first.y != last.y) {
                                ring->addPoint(first.x, first.y);
                            }
                        }
                        ring->closeRings();
                        polygon->addRingDirectly(ring);
                        multi_polygon->addGeometryDirectly(polygon);
                        geometry = multi_polygon;
                    }
                    break;
                }
                default:
                    return nullptr;
            }

            // 验证几何对象是否有效（对于复杂几何，跳过严格检查）
            if (geometry && !geometry->IsValid()) {
                // 对于多边形等复杂几何，OGR的IsValid检查可能过于严格
                // 我们只对明显错误的几何进行修复
                if (geometry->getGeometryType() == wkbPolygon || geometry->getGeometryType() == wkbMultiPolygon) {
                    // 对于多边形，只检查是否为空
                    if (geometry->IsEmpty()) {
                        std::cerr << "警告: 多边形几何为空，跳过" << std::endl;
                        delete geometry;
                        return nullptr;
                    }
                } else {
                    // 对于其他几何类型，尝试修复
                    std::cerr << "警告: 创建的几何对象无效，尝试修复..." << std::endl;
                    OGRGeometry* fixed_geometry = geometry->MakeValid();
                    if (fixed_geometry) {
                        delete geometry;
                        geometry = fixed_geometry;
                    }
                }
            }

            return geometry;

        } catch (const std::exception& e) {
            std::cerr << "创建OGR几何对象时出错: " << e.what() << std::endl;
            return nullptr;
        }
    }

    OGRFeature* OGRFormatConverter::createOGRFeature(const GeometryData& geom_data, const AttributeData& attr_data, OGRFeatureDefn* feature_defn, const std::string& output_format) {
        try {
            OGRFeature* feature = OGRFeature::CreateFeature(feature_defn);
            if (!feature) {
                return nullptr;
            }

            // 设置FID - 确保FID是32位正整数且从1开始
            uint64_t fid = geom_data.getFeatureId();

            // 对于所有格式，确保FID不为0且为正整数
            if (fid == 0) {
                fid = 1; // 将0映射为1
            }

            // 确保FID在32位正整数范围内
            if (fid > 2147483647) {           // 2^31 - 1
                fid = (fid % 2147483647) + 1; // 映射到有效范围
            }

            // 对于FileGDB，额外确保FID从1开始
            if (output_format == "OpenFileGDB" || output_format == "FileGDB") {
                if (fid < 1) {
                    fid = 1;
                }
            }

            // 注意：这里不需要重新映射FID，因为在正向转换时已经处理了FID冲突
            // 直接使用存储的FID即可

            feature->SetFID(static_cast<long>(fid));

            // 设置几何
            OGRGeometry* geometry = createOGRGeometry(geom_data);
            if (geometry) {
                feature->SetGeometryDirectly(geometry);
            }

            // 设置属性
            const auto& properties = attr_data.getProperties();
            for (const auto& [field_name, field_value] : properties) {
                int field_index = feature_defn->GetFieldIndex(field_name.c_str());
                if (field_index >= 0) {
                    OGRFieldDefn* field_defn = feature_defn->GetFieldDefn(field_index);
                    OGRFieldType field_type = field_defn->GetType();

                    switch (field_type) {
                        case OFTInteger:
                            try {
                                feature->SetField(field_index, std::stoi(field_value));
                            } catch (const std::exception&) {
                                feature->SetField(field_index, 0);
                            }
                            break;
                        case OFTInteger64:
                            try {
                                feature->SetField(field_index, std::stoll(field_value));
                            } catch (const std::exception&) {
                                feature->SetField(field_index, 0LL);
                            }
                            break;
                        case OFTReal:
                            try {
                                feature->SetField(field_index, std::stod(field_value));
                            } catch (const std::exception&) {
                                feature->SetField(field_index, 0.0);
                            }
                            break;
                        default:
                            feature->SetField(field_index, field_value.c_str());
                            break;
                    }
                }
            }

            return feature;

        } catch (const std::exception& e) {
            std::cerr << "创建OGR要素时出错: " << e.what() << std::endl;
            return nullptr;
        }
    }

    std::string OGRFormatConverter::getMetadataFilePath() const {
        return output_dir_ + "/" + ogr_file_name_ + "_meta.json";
    }

    nlohmann::json OGRFormatConverter::loadMetadata() {
        std::string metadata_file = getMetadataFilePath();
        std::ifstream file(metadata_file);

        if (!file.is_open()) {
            std::cerr << "无法打开元数据文件: " << metadata_file << std::endl;
            return nlohmann::json();
        }

        try {
            nlohmann::json metadata;
            file >> metadata;
            file.close();
            return metadata;
        } catch (const std::exception& e) {
            std::cerr << "解析元数据文件时出错: " << e.what() << std::endl;
            file.close();
            return nlohmann::json();
        }
    }

    void OGRFormatConverter::createCPGFile(const std::string& shapefile_path) {
        try {
            // 从shapefile路径生成CPG文件路径
            std::filesystem::path path(shapefile_path);
            std::string cpg_path = path.parent_path().string() + "/" + path.stem().string() + ".cpg";

            // 创建CPG文件，指定UTF-8编码
            std::ofstream cpg_file(cpg_path);
            if (cpg_file.is_open()) {
                cpg_file << "UTF-8";
                cpg_file.close();
                std::cout << "已创建CPG文件: " << cpg_path << " (编码: UTF-8)" << std::endl;
            } else {
                std::cerr << "无法创建CPG文件: " << cpg_path << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "创建CPG文件时出错: " << e.what() << std::endl;
        }
    }

} // namespace GisStorage
