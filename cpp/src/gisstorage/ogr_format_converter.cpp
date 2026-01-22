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

    OGRFormatConverter::OGRFormatConverter(const std::string& ogr_file_path, const std::string& output_dir)
        : ogr_file_path_(ogr_file_path)
        , output_dir_(output_dir) {
        std::filesystem::path path(ogr_file_path);
        ogr_file_name_ = path.stem().string();

        stats_.total_features = 0;
        stats_.valid_features = 0;
        stats_.geometry_size = 0;
        stats_.attribute_original_size = 0;
        stats_.attribute_compressed_size = 0;
        stats_.compression_ratio = 0.0;
        stats_.string_pool_size = 0;
        stats_.string_pool_saved_bytes = 0;
        stats_.conversion_time_seconds = 0.0;

        dataset_spatial_extent_.min_x = 0.0;
        dataset_spatial_extent_.min_y = 0.0;
        dataset_spatial_extent_.max_x = 0.0;
        dataset_spatial_extent_.max_y = 0.0;

        InitializeStorageFiles();
    }

    void OGRFormatConverter::InitializeStorageFiles() {
        std::filesystem::create_directories(output_dir_);

        std::string geom_file = output_dir_ + "/" + ogr_file_name_ + ".geom";
        std::string attr_file = output_dir_ + "/" + ogr_file_name_ + ".attr";
        std::string pool_file = output_dir_ + "/" + ogr_file_name_ + ".pool";

        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file, pool_file);
    }

    std::vector<uint64_t> OGRFormatConverter::Convert() {
        auto start_time = std::chrono::high_resolution_clock::now();

        std::cout << "开始转换OGR格式（优化版本）: " << ogr_file_path_ << std::endl;

        GDALAllRegister();

        std::string file_extension = std::filesystem::path(ogr_file_path_).extension().string();
        std::transform(file_extension.begin(), file_extension.end(), file_extension.begin(), ::tolower);

        if (file_extension == ".shp") {
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

        OGRFeatureDefn* feature_defn = layer->GetLayerDefn();
        int field_count = feature_defn->GetFieldCount();

        nlohmann::json field_info;
        for (int i = 0; i < field_count; ++i) {
            OGRFieldDefn* field_defn = feature_defn->GetFieldDefn(i);
            field_info[field_defn->GetNameRef()] = field_defn->GetType();
        }

        std::string source_crs_info = "";
        std::string target_crs_info = "";
        std::string transformation_info = "";

        OGRSpatialReference* spatial_ref = layer->GetSpatialRef();
        if (spatial_ref) {
            const char* authority_name = spatial_ref->GetAuthorityName(nullptr);
            const char* authority_code = spatial_ref->GetAuthorityCode(nullptr);
            if (authority_name && authority_code) {
                source_crs_info = std::string(authority_name) + ":" + std::string(authority_code);
            }

            const char* crs_name = spatial_ref->GetName();
            if (crs_name) {
                if (!source_crs_info.empty()) {
                    source_crs_info += " - " + std::string(crs_name);
                } else {
                    source_crs_info = std::string(crs_name);
                }
            }

            if (source_crs_info.empty()) {
                char* wkt = nullptr;
                if (spatial_ref->exportToWkt(&wkt) == OGRERR_NONE) {
                    source_crs_info = std::string(wkt);
                    CPLFree(wkt);
                }
            }

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

        nlohmann::json index_data;
        index_data["version"] = 1;
        index_data["data"]["features"] = nlohmann::json::object();

        std::vector<uint64_t> valid_fids;
        int total_features = layer->GetFeatureCount();
        stats_.total_features = total_features;
        std::cout << "共 " << total_features << " 个要素" << std::endl;

        layer->ResetReading();

        int processed_count = 0;
        OGRFeature* feature;

        std::set<uint64_t> used_fids;
        uint64_t next_available_fid = 1;

        while ((feature = layer->GetNextFeature()) != nullptr) {
            uint64_t original_fid = feature->GetFID();
            uint64_t fid = original_fid;

            if (fid == 0 || used_fids.find(fid) != used_fids.end()) {
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

            used_fids.insert(fid);

            try {
                OGRGeometry* geometry = feature->GetGeometryRef();
                if (!geometry || geometry->IsEmpty()) {
                    OGRFeature::DestroyFeature(feature);
                    continue;
                }

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

                std::vector<uint8_t> coord_data;
                BBox bbox;
                uint32_t num_rings = 0;

                OGREnvelope envelope;
                geometry->getEnvelope(&envelope);
                bbox = BBox(envelope.MinX, envelope.MinY, envelope.MaxX, envelope.MaxY);

                if (geom_type == GeometryType::POLYGON) {
                    auto rings = ExtractMultiRingPolygonCoordinates(geometry);
                    if (rings.empty()) {
                        OGRFeature::DestroyFeature(feature);
                        continue;
                    }

                    num_rings = static_cast<uint32_t>(rings.size());
                    coord_data = GeometrySerializer::SerializeMultiRingPolygon(rings);
                } else {
                    std::vector<Coordinate> coordinates = ExtractGeometryCoordinates(geometry);
                    if (coordinates.empty()) {
                        OGRFeature::DestroyFeature(feature);
                        continue;
                    }

                    bbox = CalculateBBox(coordinates);
                    coord_data = EncodeCoordinatesDelta(coordinates);
                }

                if (processed_count % 100000 == 0) {
                    std::cout << "\r已处理 " << processed_count << " 个要素" << std::flush;
                }

                GeometryData geom_data(fid, geom_type, coord_data, bbox, num_rings);
                int64_t geom_offset = geometry_storage_->WriteGeometry(geom_data);
                std::map<std::string, std::optional<std::string>> properties;
                for (int i = 0; i < field_count; ++i) {
                    OGRFieldDefn* field_defn = feature_defn->GetFieldDefn(i);
                    std::string field_name = field_defn->GetNameRef();

                    std::optional<std::string> field_value;
                    if (feature->IsFieldSetAndNotNull(i)) {
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
                            case OFTDate:
                            case OFTTime:
                            case OFTDateTime: {
                                const char* str_value = feature->GetFieldAsString(i);
                                field_value = str_value ? std::optional<std::string>(str_value) : std::nullopt;
                                break;
                            }
                            default: {
                                const char* str_value = feature->GetFieldAsString(i);
                                field_value = str_value ? std::optional<std::string>(str_value) : std::nullopt;
                                break;
                            }
                        }
                    } else {
                        // 字段未设置或为NULL
                        field_value = std::nullopt;
                    }
                    properties[field_name] = field_value;
                }

                AttributeData attr_data(fid, properties);
                int64_t attr_offset = attribute_storage_->WriteAttribute(attr_data);
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

        SaveStringPool();
        stats_.valid_features = valid_fids.size();
        auto compression_stats = attribute_storage_->GetCompressionStats();
        stats_.string_pool_size = compression_stats.unique_strings;
        stats_.string_pool_saved_bytes = compression_stats.original_size - compression_stats.compressed_size;
        stats_.compression_ratio = compression_stats.compression_ratio;

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        stats_.conversion_time_seconds = duration.count() / 1000.0;

        std::cout << std::endl;
        std::cout << "转换完成！" << std::endl;
        std::cout << "  有效要素: " << stats_.valid_features << "/" << stats_.total_features << std::endl;
        std::cout << "  字符串池大小: " << stats_.string_pool_size << " 个唯一字符串" << std::endl;
        std::cout << "  压缩率: " << stats_.compression_ratio << "%" << std::endl;
        std::cout << "  节省空间: " << stats_.string_pool_saved_bytes << " 字节" << std::endl;
        std::cout << "  转换时间: " << stats_.conversion_time_seconds << " 秒" << std::endl;
        GDALClose(dataset);

        SaveMetadata(field_info, source_crs_info, target_crs_info);

        std::cout << "构建分块索引以支持流式读取..." << std::endl;
        BuildChunkedIndexes();
        SaveChunckedIndexes();

        std::cout << "创建字符串池索引文件..." << std::endl;
        attribute_storage_->CreateStringPoolIndex();

        return valid_fids;
    }

    std::string OGRFormatConverter::GetGeometryFilePath() const {
        return geometry_storage_->GetGeometryFilePath();
    }

    std::string OGRFormatConverter::GetAttributeFilePath() const {
        return attribute_storage_->GetAttributeFilePath();
    }

    std::string OGRFormatConverter::GetStringPoolFilePath() const {
        return attribute_storage_->GetStringPoolFilePath();
    }

    AttributeSerializer::CompressionStats OGRFormatConverter::GetCompressionStats() const {
        return attribute_storage_->GetCompressionStats();
    }

    OGRFormatConverter::ConversionStats OGRFormatConverter::GetConversionStats() const {
        return stats_;
    }

    bool OGRFormatConverter::ConvertToOGR(const std::string& output_path, const std::string& output_format) {
        std::cout << "开始逆向转换到OGR格式: " << output_format << std::endl;
        std::cout << "输出路径: " << output_path << std::endl;

        try {
            if (loaded_geometries_.empty() || loaded_attributes_.empty() || metadata_.empty()) {
                if (!LoadCustomFormatData()) {
                    std::cerr << "加载自定义格式数据失败" << std::endl;
                    return false;
                }
            }

            GDALAllRegister();

            if (output_format == "ESRI Shapefile") {
                CPLSetConfigOption("SHAPE_ENCODING", "UTF-8");
                std::cout << "设置Shapefile字符编码为UTF-8" << std::endl;
            }

            GDALDriver* driver = GetGDALDriverManager()->GetDriverByName(output_format.c_str());
            if (!driver) {
                std::cerr << "不支持的输出格式: " << output_format << std::endl;
                return false;
            }

            GDALDataset* dataset = driver->Create(output_path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
            if (!dataset) {
                std::cerr << "无法创建输出数据集: " << output_path << std::endl;
                return false;
            }

            OGRSpatialReference* spatial_ref = nullptr;

            if (metadata_.contains("source_coordinate_system")) {
                std::string crs_info = metadata_["source_coordinate_system"];
                spatial_ref = new OGRSpatialReference();

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

            OGRwkbGeometryType geom_type = wkbUnknown;
            if (!feature_ids_.empty()) {
                auto geom_it = loaded_geometries_.find(feature_ids_[0]);
                if (geom_it != loaded_geometries_.end()) {
                    switch (geom_it->second->GetGeometryType()) {
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

            if (metadata_.contains("field_definitions")) {
                auto field_defs = metadata_["field_definitions"];
                for (auto& [field_name, field_type] : field_defs.items()) {
                    OGRFieldType ogr_field_type = OFTString;

                    if (field_type.is_number_integer()) {
                        if (field_type.get<int>() == 0) {
                            ogr_field_type = OFTInteger;
                        } else if (field_type.get<int>() == 1) {
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

            OGRFeatureDefn* feature_defn = layer->GetLayerDefn();

            int success_count = 0;
            for (uint64_t fid : feature_ids_) {
                auto geom_it = loaded_geometries_.find(fid);
                auto attr_it = loaded_attributes_.find(fid);

                if (geom_it == loaded_geometries_.end() || attr_it == loaded_attributes_.end()) {
                    continue;
                }

                OGRFeature* feature = CreateOGRFeature(*geom_it->second, *attr_it->second, feature_defn, output_format);
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

            if (output_format == "ESRI Shapefile") {
                CreateCPGFile(output_path);
            }

            if (spatial_ref)
                delete spatial_ref;
            GDALClose(dataset);

            return success_count > 0;

        } catch (const std::exception& e) {
            std::cerr << "逆向转换过程中出错: " << e.what() << std::endl;
            return false;
        }
    }

    void OGRFormatConverter::ClearLoadedData() {
        loaded_geometries_.clear();
        loaded_attributes_.clear();
        feature_ids_.clear();
        metadata_.clear();

        InitializeStorageFiles();
    }

    bool OGRFormatConverter::ConvertToMultipleFormats(const std::string& output_dir, const std::vector<std::string>& formats) {
        std::cout << "开始批量逆向转换到多种格式" << std::endl;
        if (loaded_geometries_.empty() || loaded_attributes_.empty() || metadata_.empty()) {
            if (!LoadCustomFormatData()) {
                std::cerr << "加载自定义格式数据失败" << std::endl;
                return false;
            }
        }

        bool all_success = true;
        for (const std::string& format : formats) {
            std::string output_path;

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
            if (!ConvertToOGR(output_path, format)) {
                std::cerr << "转换到 " << format << " 失败" << std::endl;
                all_success = false;
            }
        }

        return all_success;
    }

    BBox OGRFormatConverter::CalculateBBox(const std::vector<Coordinate>& coordinates) {
        return GeometrySerializer::CalculateBBox(coordinates);
    }

    std::vector<uint8_t> OGRFormatConverter::EncodeCoordinatesDelta(const std::vector<Coordinate>& coordinates) {
        return GeometrySerializer::EncodeCoordinatesDelta(coordinates);
    }

    std::vector<Coordinate> OGRFormatConverter::ExtractGeometryCoordinates(OGRGeometry* geometry) {
        if (!geometry) {
            return {};
        }

        std::vector<Coordinate> coordinates;
        ExtractCoordinatesRecursive(geometry, coordinates);
        return coordinates;
    }

    std::vector<Coordinate> OGRFormatConverter::ExtractPointCoordinates(OGRGeometry* geometry) {
        if (!geometry || geometry->getGeometryType() != wkbPoint) {
            return {};
        }

        OGRPoint* point = static_cast<OGRPoint*>(geometry);
        return {{point->getX(), point->getY()}};
    }

    std::vector<Coordinate> OGRFormatConverter::ExtractLineCoordinates(OGRGeometry* geometry) {
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

    std::vector<Coordinate> OGRFormatConverter::ExtractPolygonCoordinates(OGRGeometry* geometry) {
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

    // 新的多环多边形坐标提取函数
    std::vector<std::vector<Coordinate>> OGRFormatConverter::ExtractMultiRingPolygonCoordinates(OGRGeometry* geometry) {
        std::vector<std::vector<Coordinate>> rings;

        if (!geometry || geometry->getGeometryType() != wkbPolygon) {
            return rings;
        }

        OGRPolygon* polygon = static_cast<OGRPolygon*>(geometry);

        // 提取外环
        OGRLinearRing* exterior_ring = polygon->getExteriorRing();
        if (exterior_ring) {
            std::vector<Coordinate> exterior_coords;
            exterior_coords.reserve(exterior_ring->getNumPoints());

            for (int i = 0; i < exterior_ring->getNumPoints(); ++i) {
                exterior_coords.push_back({exterior_ring->getX(i), exterior_ring->getY(i)});
            }
            rings.push_back(exterior_coords);
        }

        // 提取内环
        int num_interior_rings = polygon->getNumInteriorRings();
        for (int i = 0; i < num_interior_rings; ++i) {
            OGRLinearRing* interior_ring = polygon->getInteriorRing(i);
            if (interior_ring) {
                std::vector<Coordinate> interior_coords;
                interior_coords.reserve(interior_ring->getNumPoints());

                for (int j = 0; j < interior_ring->getNumPoints(); ++j) {
                    interior_coords.push_back({interior_ring->getX(j), interior_ring->getY(j)});
                }
                rings.push_back(interior_coords);
            }
        }

        return rings;
    }

    void OGRFormatConverter::ExtractCoordinatesRecursive(OGRGeometry* geometry, std::vector<Coordinate>& coordinates) {
        if (!geometry) {
            return;
        }

        OGRwkbGeometryType geom_type = geometry->getGeometryType();

        switch (geom_type) {
            case wkbPoint:
            case wkbPoint25D: {
                auto point_coords = ExtractPointCoordinates(geometry);
                coordinates.insert(coordinates.end(), point_coords.begin(), point_coords.end());
                break;
            }
            case wkbLineString:
            case wkbLineString25D: {
                auto line_coords = ExtractLineCoordinates(geometry);
                coordinates.insert(coordinates.end(), line_coords.begin(), line_coords.end());
                break;
            }
            case wkbPolygon:
            case wkbPolygon25D: {
                auto polygon_coords = ExtractPolygonCoordinates(geometry);
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
                    ExtractCoordinatesRecursive(collection->getGeometryRef(i), coordinates);
                }
                break;
            }
            default:
                break;
        }
    }

    void OGRFormatConverter::SaveStringPool() {
        attribute_storage_->SaveStringPool();
    }

    void OGRFormatConverter::UpdateStats(size_t geom_size, size_t attr_original_size, size_t attr_compressed_size) {
        stats_.geometry_size += geom_size;
        stats_.attribute_original_size += attr_original_size;
        stats_.attribute_compressed_size += attr_compressed_size;
    }

    void OGRFormatConverter::SaveMetadata(const nlohmann::json& field_info, const std::string& source_crs, const std::string& target_crs) {
        std::string metadata_file = output_dir_ + "/" + ogr_file_name_ + "_meta.json";

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

        metadata["field_definitions"] = field_info;
        metadata["source_file"] = std::filesystem::path(ogr_file_path_).filename().string();

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
        metadata["source_coordinate_system"] = source_crs;
        metadata["target_coordinate_system"] = target_crs;

        nlohmann::json spatial_extent_json;
        spatial_extent_json["min_x"] = dataset_spatial_extent_.min_x;
        spatial_extent_json["min_y"] = dataset_spatial_extent_.min_y;
        spatial_extent_json["max_x"] = dataset_spatial_extent_.max_x;
        spatial_extent_json["max_y"] = dataset_spatial_extent_.max_y;
        metadata["spatial_extent"] = spatial_extent_json;

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        metadata["creation_date"] = ss.str();

        auto compression_stats = attribute_storage_->GetCompressionStats();
        nlohmann::json compression_info;
        compression_info["compression_ratio"] = compression_stats.compression_ratio;
        compression_info["original_size"] = compression_stats.original_size;
        compression_info["compressed_size"] = compression_stats.compressed_size;
        compression_info["unique_strings"] = compression_stats.unique_strings;
        compression_info["total_strings"] = compression_stats.total_strings;
        compression_info["saved_bytes"] = compression_stats.original_size - compression_stats.compressed_size;
        metadata["compression_info"] = compression_info;
        metadata["total_features"] = stats_.total_features;
        metadata["valid_features"] = stats_.valid_features;
        metadata["conversion_time_seconds"] = stats_.conversion_time_seconds;

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

    bool OGRFormatConverter::LoadCustomFormatData() {
        std::cout << "加载自定义格式数据..." << std::endl;
        try {
            loaded_geometries_.clear();
            loaded_attributes_.clear();
            feature_ids_.clear();

            metadata_ = LoadMetadata();
            if (metadata_.empty()) {
                std::cerr << "无法加载元数据" << std::endl;
                return false;
            }

            size_t total_features = 0;
            if (metadata_.contains("total_features")) {
                total_features = metadata_["total_features"];
            } else {
                std::cerr << "元数据中缺少total_features信息" << std::endl;
                return false;
            }

            feature_ids_.reserve(total_features);
            for (uint64_t i = 0; i < total_features; ++i) {
                feature_ids_.push_back(i);
            }

            std::cout << "找到 " << feature_ids_.size() << " 个要素" << std::endl;
            for (uint64_t fid : feature_ids_) {
                auto geometry = geometry_storage_->ReadGeometry(fid);
                if (geometry) {
                    loaded_geometries_[fid] = std::move(geometry);
                }

                auto attribute = attribute_storage_->ReadAttribute(fid);
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

    OGRGeometry* OGRFormatConverter::CreateOGRGeometry(const GeometryData& geom_data) {
        try {
            std::vector<Coordinate> coordinates = geom_data.DecodeCoordinates();
            if (coordinates.empty()) {
                std::cerr << "警告: 几何数据解码后为空，数据大小: " << geom_data.GetCoordinates().size() << std::endl;
                return nullptr;
            }

            if (coordinates.size() == 1) {
                std::cout << "解码得到单点: (" << coordinates[0].x << ", " << coordinates[0].y << ")，几何类型: " << static_cast<int>(geom_data.GetGeometryType()) << std::endl;
            } else {
                std::cout << "解码得到 " << coordinates.size() << " 个坐标点，几何类型: " << static_cast<int>(geom_data.GetGeometryType()) << std::endl;
            }

            OGRGeometry* geometry = nullptr;

            switch (geom_data.GetGeometryType()) {
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
                    auto rings = geom_data.DecodeMultiRingCoordinates();
                    if (!rings.empty() && rings[0].size() >= 3) {
                        OGRPolygon* polygon = new OGRPolygon();

                        for (size_t ring_idx = 0; ring_idx < rings.size(); ++ring_idx) {
                            const auto& ring_coords = rings[ring_idx];
                            if (ring_coords.size() >= 3) {
                                OGRLinearRing* ring = new OGRLinearRing();

                                for (const auto& coord : ring_coords) {
                                    ring->addPoint(coord.x, coord.y);
                                }

                                if (ring_coords.size() > 0) {
                                    const auto& first = ring_coords[0];
                                    const auto& last = ring_coords.back();
                                    if (first.x != last.x || first.y != last.y) {
                                        ring->addPoint(first.x, first.y);
                                    }
                                }
                                ring->closeRings();

                                if (ring_idx == 0) {
                                    polygon->addRingDirectly(ring);
                                } else {
                                    polygon->addRingDirectly(ring);
                                }
                            }
                        }

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
                    if (coordinates.size() >= 3) {
                        OGRMultiPolygon* multi_polygon = new OGRMultiPolygon();
                        OGRPolygon* polygon = new OGRPolygon();
                        OGRLinearRing* ring = new OGRLinearRing();
                        for (const auto& coord : coordinates) {
                            ring->addPoint(coord.x, coord.y);
                        }
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

            if (geometry && !geometry->IsValid()) {
                if (geometry->getGeometryType() == wkbPolygon || geometry->getGeometryType() == wkbMultiPolygon) {
                    if (geometry->IsEmpty()) {
                        std::cerr << "警告: 多边形几何为空，跳过" << std::endl;
                        delete geometry;
                        return nullptr;
                    }
                } else {
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

    OGRFeature* OGRFormatConverter::CreateOGRFeature(const GeometryData& geom_data, const AttributeData& attr_data, OGRFeatureDefn* feature_defn, const std::string& output_format) {
        try {
            OGRFeature* feature = OGRFeature::CreateFeature(feature_defn);
            if (!feature) {
                return nullptr;
            }

            uint64_t fid = geom_data.GetFeatureId();

            if (fid == 0) {
                fid = 1;
            }

            if (fid > 2147483647) {
                fid = (fid % 2147483647) + 1;
            }

            if (output_format == "OpenFileGDB" || output_format == "FileGDB") {
                if (fid < 1) {
                    fid = 1;
                }
            }

            feature->SetFID(static_cast<long>(fid));

            OGRGeometry* geometry = CreateOGRGeometry(geom_data);
            if (geometry) {
                feature->SetGeometryDirectly(geometry);
            }

            const auto& properties = attr_data.GetProperties();
            for (const auto& [field_name, field_value_opt] : properties) {
                int field_index = feature_defn->GetFieldIndex(field_name.c_str());
                if (field_index >= 0) {
                    if (field_value_opt.has_value()) {
                        const std::string& field_value = field_value_opt.value();
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
                    } else {
                        // 属性值为NULL，不设置字段值，让OGR保持默认状态
                        // feature->SetFieldNull(field_index); // 如果需要明确设置为NULL
                    }
                }
            }

            return feature;

        } catch (const std::exception& e) {
            std::cerr << "创建OGR要素时出错: " << e.what() << std::endl;
            return nullptr;
        }
    }

    std::string OGRFormatConverter::GetMetadataFilePath() const {
        return output_dir_ + "/" + ogr_file_name_ + "_meta.json";
    }

    nlohmann::json OGRFormatConverter::LoadMetadata() {
        std::string metadata_file = GetMetadataFilePath();
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

    void OGRFormatConverter::BuildChunkedIndexes() {
        try {
            std::cout << "开始构建分块索引..." << std::endl;
            auto start_time = std::chrono::high_resolution_clock::now();

            if (geometry_storage_) {
                std::cout << "构建几何数据分块索引..." << std::endl;
                geometry_storage_->BuildChunkedIndex();
            }

            if (attribute_storage_) {
                std::cout << "构建属性数据分块索引..." << std::endl;
                attribute_storage_->BuildChunkedIndex();
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            std::cout << "分块索引构建完成，耗时: " << duration.count() << " 毫秒" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "构建分块索引时出错: " << e.what() << std::endl;
        }
    }

    void OGRFormatConverter::SaveChunckedIndexes() {
        geometry_storage_->SaveChunkedIndex();
        attribute_storage_->SaveChunkedIndex();
    }

    void OGRFormatConverter::CreateCPGFile(const std::string& shapefile_path) {
        try {
            std::filesystem::path path(shapefile_path);
            std::string cpg_path = path.parent_path().string() + "/" + path.stem().string() + ".cpg";

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
