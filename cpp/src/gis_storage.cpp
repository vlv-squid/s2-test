#include "gis_storage.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <future>
#include <nlohmann/json.hpp>
#include <ogrsf_frmts.h>

namespace GisStorage {

    // GeometryData 实现
    GeometryData::GeometryData(uint64_t feature_id, GeometryType geometry_type, const std::vector<uint8_t>& coordinates, const BBox& bbox)
        : feature_id_(feature_id)
        , geometry_type_(geometry_type)
        , coordinates_(coordinates)
        , bbox_(bbox) {}

    std::vector<Coordinate> GeometryData::decodeCoordinates() const {
        if (coordinates_.empty()) {
            return {};
        }

        // 如果是单点情况
        if (coordinates_.size() == 16) {
            double x, y;
            std::memcpy(&x, &coordinates_[0], sizeof(double));
            std::memcpy(&y, &coordinates_[8], sizeof(double));
            return {Coordinate(x, y)};
        }

        // 差分编码的情况
        if (coordinates_.size() < 17) {
            return {};
        }

        std::vector<Coordinate> coordinates;

        // 读取第一个点的绝对坐标
        double x, y;
        std::memcpy(&x, &coordinates_[0], sizeof(double));
        std::memcpy(&y, &coordinates_[8], sizeof(double));
        coordinates.emplace_back(x, y);

        if (coordinates_.size() <= 16) {
            return coordinates;
        }

        // 读取数据类型标记
        uint8_t type_flag = coordinates_[16];
        size_t pos = 17;

        if (type_flag == 0) { // short类型
            while (pos + 4 <= coordinates_.size()) {
                int16_t dx, dy;
                std::memcpy(&dx, &coordinates_[pos], sizeof(int16_t));
                std::memcpy(&dy, &coordinates_[pos + 2], sizeof(int16_t));
                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx, prev.y + dy);
                pos += 4;
            }
        } else if (type_flag == 1) { // int类型
            while (pos + 8 <= coordinates_.size()) {
                int32_t dx, dy;
                std::memcpy(&dx, &coordinates_[pos], sizeof(int32_t));
                std::memcpy(&dy, &coordinates_[pos + 4], sizeof(int32_t));
                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx, prev.y + dy);
                pos += 8;
            }
        } else if (type_flag == 2) { // float类型
            while (pos + 8 <= coordinates_.size()) {
                float dx, dy;
                std::memcpy(&dx, &coordinates_[pos], sizeof(float));
                std::memcpy(&dy, &coordinates_[pos + 4], sizeof(float));
                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx, prev.y + dy);
                pos += 8;
            }
        }

        return coordinates;
    }

    size_t GeometryData::getSerializedSize() const {
        // feature_id(8) + geometry_type(1) + 7字节填充 + bbox(32) + coord_size(4) + coordinates
        return 8 + 1 + 7 + 32 + 4 + coordinates_.size();
    }

    // AttributeData 实现
    AttributeData::AttributeData(uint64_t feature_id, const std::map<std::string, std::string>& properties)
        : feature_id_(feature_id)
        , properties_(properties) {}

    std::string AttributeData::getProperty(const std::string& key, const std::string& default_value) const {
        auto it = properties_.find(key);
        return (it != properties_.end()) ? it->second : default_value;
    }

    size_t AttributeData::getSerializedSize() const {
        // 计算JSON字符串长度
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& prop : properties_) {
            if (!first)
                oss << ",";
            oss << "\"" << prop.first << "\":\"" << prop.second << "\"";
            first = false;
        }
        oss << "}";
        std::string json_str = oss.str();

        // feature_id(8) + json_length(4) + json_data
        return 8 + 4 + json_str.length();
    }

    // GeometrySerializer 实现
    std::vector<uint8_t> GeometrySerializer::serializeGeometry(const GeometryData& geometry) {
        std::vector<uint8_t> data;
        data.reserve(geometry.getSerializedSize());

        // 写入feature_id (8字节)
        uint64_t feature_id = geometry.getFeatureId();
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&feature_id), reinterpret_cast<uint8_t*>(&feature_id) + sizeof(uint64_t));

        // 写入geometry_type (1字节)
        uint8_t geom_type = static_cast<uint8_t>(geometry.getGeometryType());
        data.push_back(geom_type);

        // 写入7字节填充（与Python版本保持一致）
        std::vector<uint8_t> padding(7, 0);
        data.insert(data.end(), padding.begin(), padding.end());

        // 写入bbox (32字节)
        const BBox& bbox = geometry.getBBox();
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.min_x), reinterpret_cast<const uint8_t*>(&bbox.min_x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.min_y), reinterpret_cast<const uint8_t*>(&bbox.min_y) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.max_x), reinterpret_cast<const uint8_t*>(&bbox.max_x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&bbox.max_y), reinterpret_cast<const uint8_t*>(&bbox.max_y) + sizeof(double));

        // 写入坐标数据大小 (4字节)
        uint32_t coord_size = static_cast<uint32_t>(geometry.getCoordinates().size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&coord_size), reinterpret_cast<uint8_t*>(&coord_size) + sizeof(uint32_t));

        // 写入坐标数据
        const auto& coords = geometry.getCoordinates();
        data.insert(data.end(), coords.begin(), coords.end());

        return data;
    }

    std::unique_ptr<GeometryData> GeometrySerializer::deserializeGeometry(const std::vector<uint8_t>& data) {
        if (data.size() < 48) {
            throw std::runtime_error("数据长度不足，无法反序列化几何对象");
        }

        size_t offset = 0;

        // 读取feature_id
        uint64_t feature_id;
        std::memcpy(&feature_id, &data[offset], sizeof(uint64_t));
        offset += sizeof(uint64_t);

        // 读取geometry_type
        uint8_t geom_type_byte;
        std::memcpy(&geom_type_byte, &data[offset], sizeof(uint8_t));
        GeometryType geometry_type = static_cast<GeometryType>(geom_type_byte);
        offset += sizeof(uint8_t);

        // 跳过7字节填充（与Python版本保持一致）
        offset += 7;

        // 读取bbox
        BBox bbox;
        std::memcpy(&bbox.min_x, &data[offset], sizeof(double));
        offset += sizeof(double);
        std::memcpy(&bbox.min_y, &data[offset], sizeof(double));
        offset += sizeof(double);
        std::memcpy(&bbox.max_x, &data[offset], sizeof(double));
        offset += sizeof(double);
        std::memcpy(&bbox.max_y, &data[offset], sizeof(double));
        offset += sizeof(double);

        // 读取坐标数据大小
        uint32_t coord_size;
        std::memcpy(&coord_size, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (offset + coord_size > data.size()) {
            throw std::runtime_error("坐标数据不完整");
        }

        // 读取坐标数据
        std::vector<uint8_t> coordinates(data.begin() + offset, data.begin() + offset + coord_size);

        return std::make_unique<GeometryData>(feature_id, geometry_type, coordinates, bbox);
    }

    std::vector<uint8_t> GeometrySerializer::serializeCoordinates(const std::vector<Coordinate>& coordinates) {
        std::vector<uint8_t> data;
        data.reserve(4 + coordinates.size() * 16); // 4字节数量 + 每个坐标16字节

        // 写入点的数量
        uint32_t num_points = static_cast<uint32_t>(coordinates.size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&num_points), reinterpret_cast<uint8_t*>(&num_points) + sizeof(uint32_t));

        // 写入所有坐标点
        for (const auto& coord : coordinates) {
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coord.x), reinterpret_cast<const uint8_t*>(&coord.x) + sizeof(double));
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coord.y), reinterpret_cast<const uint8_t*>(&coord.y) + sizeof(double));
        }

        return data;
    }

    std::vector<Coordinate> GeometrySerializer::deserializeCoordinates(const std::vector<uint8_t>& data) {
        if (data.size() < 4) {
            return {};
        }

        std::vector<Coordinate> coordinates;

        // 读取点的数量
        uint32_t num_points;
        std::memcpy(&num_points, &data[0], sizeof(uint32_t));

        size_t offset = 4;
        coordinates.reserve(num_points);

        for (uint32_t i = 0; i < num_points && offset + 16 <= data.size(); ++i) {
            Coordinate coord;
            std::memcpy(&coord.x, &data[offset], sizeof(double));
            offset += sizeof(double);
            std::memcpy(&coord.y, &data[offset], sizeof(double));
            offset += sizeof(double);
            coordinates.push_back(coord);
        }

        return coordinates;
    }

    std::vector<uint8_t> GeometrySerializer::encodeCoordinatesDelta(const std::vector<Coordinate>& coordinates) {
        if (coordinates.empty()) {
            return {};
        }

        std::vector<uint8_t> data;
        data.reserve(16 + (coordinates.size() - 1) * 8); // 第一个点16字节，后续每点8字节

        // 第一个点存储绝对坐标
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].x), reinterpret_cast<const uint8_t*>(&coordinates[0].x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].y), reinterpret_cast<const uint8_t*>(&coordinates[0].y) + sizeof(double));

        // 后续点存储相对于前一个点的偏移量
        for (size_t i = 1; i < coordinates.size(); ++i) {
            float dx = static_cast<float>(coordinates[i].x - coordinates[i - 1].x);
            float dy = static_cast<float>(coordinates[i].y - coordinates[i - 1].y);

            data.insert(data.end(), reinterpret_cast<uint8_t*>(&dx), reinterpret_cast<uint8_t*>(&dx) + sizeof(float));
            data.insert(data.end(), reinterpret_cast<uint8_t*>(&dy), reinterpret_cast<uint8_t*>(&dy) + sizeof(float));
        }

        return data;
    }

    std::vector<Coordinate> GeometrySerializer::decodeCoordinatesDelta(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            return {};
        }

        std::vector<Coordinate> coordinates;

        // 读取第一个点的绝对坐标
        if (data.size() >= 16) {
            Coordinate first_point;
            std::memcpy(&first_point.x, &data[0], sizeof(double));
            std::memcpy(&first_point.y, &data[8], sizeof(double));
            coordinates.push_back(first_point);

            // 读取后续点的偏移量
            size_t offset = 16;
            while (offset + 8 <= data.size()) {
                float dx, dy;
                std::memcpy(&dx, &data[offset], sizeof(float));
                offset += sizeof(float);
                std::memcpy(&dy, &data[offset], sizeof(float));
                offset += sizeof(float);

                Coordinate prev = coordinates.back();
                coordinates.emplace_back(prev.x + dx, prev.y + dy);
            }
        }

        return coordinates;
    }

    BBox GeometrySerializer::calculateBBox(const std::vector<Coordinate>& coordinates) {
        if (coordinates.empty()) {
            return BBox();
        }

        double min_x = coordinates[0].x;
        double min_y = coordinates[0].y;
        double max_x = coordinates[0].x;
        double max_y = coordinates[0].y;

        for (const auto& coord : coordinates) {
            min_x = std::min(min_x, coord.x);
            min_y = std::min(min_y, coord.y);
            max_x = std::max(max_x, coord.x);
            max_y = std::max(max_y, coord.y);
        }

        return BBox(min_x, min_y, max_x, max_y);
    }

    // AttributeSerializer 实现
    std::vector<uint8_t> AttributeSerializer::serializeAttributes(const AttributeData& attribute) {
        std::vector<uint8_t> data;

        // 构建JSON字符串
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& prop : attribute.getProperties()) {
            if (!first)
                oss << ",";
            oss << "\"" << prop.first << "\":\"" << prop.second << "\"";
            first = false;
        }
        oss << "}";
        std::string json_str = oss.str();

        // 写入feature_id
        uint64_t feature_id = attribute.getFeatureId();
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&feature_id), reinterpret_cast<uint8_t*>(&feature_id) + sizeof(uint64_t));

        // 写入JSON长度
        uint32_t json_length = static_cast<uint32_t>(json_str.length());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&json_length), reinterpret_cast<uint8_t*>(&json_length) + sizeof(uint32_t));

        // 写入JSON数据
        data.insert(data.end(), json_str.begin(), json_str.end());

        return data;
    }

    std::unique_ptr<AttributeData> AttributeSerializer::deserializeAttributes(const std::vector<uint8_t>& data) {
        if (data.size() < 12) {
            throw std::runtime_error("属性数据长度不足");
        }

        size_t offset = 0;

        // 读取feature_id
        uint64_t feature_id;
        std::memcpy(&feature_id, &data[offset], sizeof(uint64_t));
        offset += sizeof(uint64_t);

        // 读取JSON长度
        uint32_t json_length;
        std::memcpy(&json_length, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (offset + json_length > data.size()) {
            throw std::runtime_error("JSON数据不完整");
        }

        // 读取JSON字符串
        std::string json_str(data.begin() + offset, data.begin() + offset + json_length);

        // 使用nlohmann/json解析JSON
        std::map<std::string, std::string> properties;
        try {
            nlohmann::json j = nlohmann::json::parse(json_str);
            for (auto it = j.begin(); it != j.end(); ++it) {
                properties[it.key()] = it.value().dump();
            }
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "JSON解析失败: " << e.what() << std::endl;
        }

        return std::make_unique<AttributeData>(feature_id, properties);
    }

    // GeometryStorage 实现
    GeometryStorage::GeometryStorage(const std::string& geometry_file)
        : geometry_file_(geometry_file)
        , index_built_(false) {
        // 创建目录
        std::filesystem::path file_path(geometry_file);
        std::filesystem::create_directories(file_path.parent_path());
    }

    int64_t GeometryStorage::writeGeometry(const GeometryData& geometry) {
        std::ofstream file(geometry_file_, std::ios::binary | std::ios::app);
        if (!file) {
            throw std::runtime_error("无法打开几何文件进行写入: " + geometry_file_);
        }

        int64_t offset = file.tellp();
        std::vector<uint8_t> geom_binary = GeometrySerializer::serializeGeometry(geometry);

        if (!geom_binary.empty()) {
            file.write(reinterpret_cast<const char*>(geom_binary.data()), geom_binary.size());
            // 清除索引缓存
            offset_index_.clear();
            index_built_ = false;
            return offset;
        } else {
            throw std::runtime_error("几何数据序列化失败 for FID " + std::to_string(geometry.getFeatureId()));
        }
    }

    std::unique_ptr<GeometryData> GeometryStorage::readGeometry(uint64_t feature_id) {
        const auto& offsets = getOffsetIndex();
        auto it = offsets.find(feature_id);
        if (it == offsets.end()) {
            throw std::runtime_error("Feature ID " + std::to_string(feature_id) + " not found");
        }

        std::ifstream file(geometry_file_, std::ios::binary);
        if (!file) {
            throw std::runtime_error("无法打开几何文件: " + geometry_file_);
        }

        file.seekg(it->second);

        // 先读取头部数据以确定需要读取的总大小（与Python版本保持一致）
        std::vector<uint8_t> header_data(48);
        if (!file.read(reinterpret_cast<char*>(header_data.data()), 48)) {
            throw std::runtime_error("几何数据不完整 for FID " + std::to_string(feature_id));
        }

        // 获取坐标数据大小
        uint32_t coord_size = 0;
        if (!file.read(reinterpret_cast<char*>(&coord_size), sizeof(uint32_t))) {
            throw std::runtime_error("几何数据不完整 for FID " + std::to_string(feature_id));
        }

        // 读取坐标数据
        std::vector<uint8_t> coord_data(coord_size);
        if (coord_size > 0) {
            if (!file.read(reinterpret_cast<char*>(coord_data.data()), coord_size)) {
                throw std::runtime_error("几何坐标数据不完整 for FID " + std::to_string(feature_id));
            }
        }

        // 组合所有数据进行反序列化
        std::vector<uint8_t> geom_data = header_data;
        geom_data.insert(geom_data.end(), reinterpret_cast<uint8_t*>(&coord_size), reinterpret_cast<uint8_t*>(&coord_size) + sizeof(uint32_t));
        geom_data.insert(geom_data.end(), coord_data.begin(), coord_data.end());

        return GeometrySerializer::deserializeGeometry(geom_data);
    }

    std::vector<uint64_t> GeometryStorage::getAllFeatureIds() {
        const auto& offsets = getOffsetIndex();
        std::vector<uint64_t> feature_ids;
        feature_ids.reserve(offsets.size());
        for (const auto& pair : offsets) {
            feature_ids.push_back(pair.first);
        }
        return feature_ids;
    }

    bool GeometryStorage::hasFeature(uint64_t feature_id) {
        const auto& offsets = getOffsetIndex();
        return offsets.find(feature_id) != offsets.end();
    }

    void GeometryStorage::clearCache() {
        offset_index_.clear();
        index_built_ = false;
    }

    void GeometryStorage::buildOffsetIndex() {
        offset_index_.clear();

        std::ifstream file(geometry_file_, std::ios::binary);
        if (!file) {
            return;
        }

        while (true) {
            int64_t current_pos = file.tellg();

            // 读取feature_id
            uint64_t fid;
            file.read(reinterpret_cast<char*>(&fid), sizeof(uint64_t));
            if (file.gcount() < sizeof(uint64_t)) {
                break; // 文件结束
            }

            offset_index_[fid] = current_pos;

            // 手动解析记录结构（与Python版本保持一致）
            try {
                // 跳过geometry_type(1B) + 7字节填充 + bbox(32B) = 40字节
                file.seekg(40, std::ios::cur);

                // 读取坐标大小(4B)
                uint32_t coord_size;
                file.read(reinterpret_cast<char*>(&coord_size), sizeof(uint32_t));
                if (file.gcount() < sizeof(uint32_t)) {
                    break;
                }

                // 跳过坐标数据
                file.seekg(coord_size, std::ios::cur);

            } catch (const std::exception& e) {
                std::cerr << "解析几何记录失败 at FID " << fid << ": " << e.what() << std::endl;
                break;
            }
        }
    }

    const std::unordered_map<uint64_t, int64_t>& GeometryStorage::getOffsetIndex() {
        if (!index_built_) {
            buildOffsetIndex();
            index_built_ = true;
        }
        return offset_index_;
    }

    void GeometryStorage::loadIndexFromFile(const std::string& index_file) {
        if (!std::filesystem::exists(index_file)) {
            std::cout << "索引文件不存在: " << index_file << std::endl;
            return;
        }

        offset_index_.clear();

        // 尝试读取JSON格式的索引文件
        std::ifstream file(index_file);
        if (!file) {
            std::cout << "无法打开索引文件: " << index_file << std::endl;
            return;
        }

        try {
            nlohmann::json index_data = nlohmann::json::parse(file);

            // 验证JSON结构
            if (!index_data.contains("version") || !index_data.contains("data") || !index_data["data"].contains("features")) {
                std::cout << "索引文件格式不正确" << std::endl;
                return;
            }

            std::cout << "索引文件格式: JSON" << std::endl;
            std::cout << "版本: " << index_data["version"] << std::endl;
            std::cout << "要素数量: " << index_data["data"]["features"].size() << std::endl;

            // 读取每个索引条目
            int count = 0;
            for (const auto& feature : index_data["data"]["features"].items()) {
                uint64_t fid = std::stoull(feature.key());
                int64_t geom_offset = feature.value()["geom_offset"];
                int64_t attr_offset = feature.value()["attr_offset"];

                offset_index_[fid] = geom_offset;

                if (count < 5) { // 只显示前5个条目
                    std::cout << "索引条目 " << count << ": FID=" << fid << ", geom_offset=" << geom_offset << ", attr_offset=" << attr_offset << std::endl;
                }
                count++;
            }

            std::cout << "加载的索引条目数量: " << offset_index_.size() << std::endl;
            index_built_ = true;

        } catch (const nlohmann::json::exception& e) {
            std::cout << "JSON索引文件解析失败: " << e.what() << std::endl;
            std::cout << "尝试读取旧格式的二进制索引文件..." << std::endl;

            // 如果JSON解析失败，尝试读取旧的二进制格式
            file.close();
            file.open(index_file, std::ios::binary);
            if (!file) {
                std::cout << "无法以二进制模式打开索引文件" << std::endl;
                return;
            }

            // 读取索引条目数量
            uint32_t count;
            file.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
            if (file.gcount() < sizeof(uint32_t)) {
                std::cout << "无法读取索引条目数量" << std::endl;
                return;
            }

            std::cout << "索引条目数量: " << count << std::endl;

            // 读取每个索引条目：FID + 几何偏移 + 属性偏移
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t fid = 0;
                int64_t geom_offset = 0;
                int64_t attr_offset = 0;

                if (!file.read(reinterpret_cast<char*>(&fid), sizeof(uint64_t))) {
                    std::cout << "读取索引条目 " << i << " 的FID失败" << std::endl;
                    break;
                }
                if (!file.read(reinterpret_cast<char*>(&geom_offset), sizeof(int64_t))) {
                    std::cout << "读取索引条目 " << i << " 的geom_offset失败" << std::endl;
                    break;
                }
                if (!file.read(reinterpret_cast<char*>(&attr_offset), sizeof(int64_t))) {
                    std::cout << "读取索引条目 " << i << " 的attr_offset失败" << std::endl;
                    break;
                }

                offset_index_[fid] = geom_offset;

                if (i < 5) { // 只显示前5个条目
                    std::cout << "索引条目 " << i << ": FID=" << fid << ", geom_offset=" << geom_offset << ", attr_offset=" << attr_offset << std::endl;
                }
            }

            std::cout << "加载的索引条目数量: " << offset_index_.size() << std::endl;
            index_built_ = true;
        }
    }

    // AttributeStorage 实现
    AttributeStorage::AttributeStorage(const std::string& attribute_file)
        : attribute_file_(attribute_file)
        , index_built_(false) {
        // 创建目录
        std::filesystem::path file_path(attribute_file);
        std::filesystem::create_directories(file_path.parent_path());
    }

    int64_t AttributeStorage::writeAttribute(const AttributeData& attribute) {
        std::ofstream file(attribute_file_, std::ios::binary | std::ios::app);
        if (!file) {
            throw std::runtime_error("无法打开属性文件进行写入: " + attribute_file_);
        }

        int64_t offset = file.tellp();
        std::vector<uint8_t> attr_binary = AttributeSerializer::serializeAttributes(attribute);
        file.write(reinterpret_cast<const char*>(attr_binary.data()), attr_binary.size());

        // 清除索引缓存
        offset_index_.clear();
        index_built_ = false;
        return offset;
    }

    std::unique_ptr<AttributeData> AttributeStorage::readAttribute(uint64_t feature_id) {
        if (!std::filesystem::exists(attribute_file_)) {
            return nullptr;
        }

        const auto& offsets = getOffsetIndex();
        auto it = offsets.find(feature_id);
        if (it == offsets.end()) {
            return nullptr;
        }

        std::ifstream file(attribute_file_, std::ios::binary);
        if (!file) {
            return nullptr;
        }

        file.seekg(it->second);

        // 读取feature_id和json长度（与Python版本保持一致）
        std::vector<uint8_t> header_data(12);
        if (!file.read(reinterpret_cast<char*>(header_data.data()), 12)) {
            return nullptr;
        }

        uint64_t fid = 0;
        uint32_t json_length = 0;
        std::memcpy(&fid, &header_data[0], sizeof(uint64_t));
        std::memcpy(&json_length, &header_data[8], sizeof(uint32_t));

        if (fid != feature_id) {
            return nullptr;
        }

        // 读取属性数据
        std::vector<uint8_t> props_data(json_length);
        if (json_length > 0) {
            if (!file.read(reinterpret_cast<char*>(props_data.data()), json_length)) {
                return nullptr;
            }
        }

        // 解析JSON
        std::map<std::string, std::string> properties;
        try {
            if (!props_data.empty()) {
                std::string props_json(props_data.begin(), props_data.end());
                nlohmann::json j = nlohmann::json::parse(props_json);
                for (auto itj = j.begin(); itj != j.end(); ++itj) {
                    properties[itj.key()] = itj.value().dump();
                }
            }
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "JSON解析失败: " << e.what() << std::endl;
        }

        return std::make_unique<AttributeData>(feature_id, properties);
    }

    std::vector<uint64_t> AttributeStorage::getAllFeatureIds() {
        const auto& offsets = getOffsetIndex();
        std::vector<uint64_t> feature_ids;
        feature_ids.reserve(offsets.size());
        for (const auto& pair : offsets) {
            feature_ids.push_back(pair.first);
        }
        return feature_ids;
    }

    bool AttributeStorage::hasFeature(uint64_t feature_id) {
        const auto& offsets = getOffsetIndex();
        return offsets.find(feature_id) != offsets.end();
    }

    void AttributeStorage::clearCache() {
        offset_index_.clear();
        index_built_ = false;
    }

    void AttributeStorage::buildOffsetIndex() {
        offset_index_.clear();

        if (!std::filesystem::exists(attribute_file_)) {
            return;
        }

        std::ifstream file(attribute_file_, std::ios::binary);
        if (!file) {
            return;
        }

        // 跳过文件开头的字段信息（与Python版本保持一致）
        try {
            // 读取字段信息长度
            uint32_t field_info_length;
            file.read(reinterpret_cast<char*>(&field_info_length), sizeof(uint32_t));
            if (file.gcount() >= sizeof(uint32_t)) {
                // 跳过字段信息
                file.seekg(field_info_length, std::ios::cur);
            }
        } catch (...) {
            // 如果读取字段信息失败，重置文件指针到开头
            file.seekg(0);
        }

        while (true) {
            int64_t current_pos = file.tellg();

            // 读取feature_id
            uint64_t fid;
            file.read(reinterpret_cast<char*>(&fid), sizeof(uint64_t));
            if (file.gcount() < sizeof(uint64_t)) {
                break;
            }

            offset_index_[fid] = current_pos;

            // 读取json长度
            uint32_t json_length;
            file.read(reinterpret_cast<char*>(&json_length), sizeof(uint32_t));
            if (file.gcount() < sizeof(uint32_t)) {
                break;
            }

            // 跳过JSON数据
            file.seekg(json_length, std::ios::cur);
        }
    }

    const std::unordered_map<uint64_t, int64_t>& AttributeStorage::getOffsetIndex() {
        if (!index_built_) {
            buildOffsetIndex();
            index_built_ = true;
        }
        return offset_index_;
    }

    void AttributeStorage::loadIndexFromFile(const std::string& index_file) {
        if (!std::filesystem::exists(index_file)) {
            return;
        }

        offset_index_.clear();

        // 尝试读取JSON格式的索引文件
        std::ifstream file(index_file);
        if (!file) {
            return;
        }

        try {
            nlohmann::json index_data = nlohmann::json::parse(file);

            // 验证JSON结构
            if (!index_data.contains("version") || !index_data.contains("data") || !index_data["data"].contains("features")) {
                return;
            }

            // 读取每个索引条目
            for (const auto& feature : index_data["data"]["features"].items()) {
                uint64_t fid = std::stoull(feature.key());
                int64_t attr_offset = feature.value()["attr_offset"];

                offset_index_[fid] = attr_offset;
            }

            index_built_ = true;

        } catch (const nlohmann::json::exception& e) {
            // 如果JSON解析失败，尝试读取旧的二进制格式
            file.close();
            file.open(index_file, std::ios::binary);
            if (!file) {
                return;
            }

            // 读取索引条目数量
            uint32_t count;
            file.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
            if (file.gcount() < sizeof(uint32_t)) {
                return;
            }

            // 读取每个索引条目：FID + 几何偏移 + 属性偏移
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t fid;
                int64_t geom_offset;
                int64_t attr_offset;

                file.read(reinterpret_cast<char*>(&fid), sizeof(uint64_t));
                file.read(reinterpret_cast<char*>(&geom_offset), sizeof(int64_t));
                file.read(reinterpret_cast<char*>(&attr_offset), sizeof(int64_t));

                if (file.gcount() < sizeof(uint64_t) + 2 * sizeof(int64_t)) {
                    break;
                }

                offset_index_[fid] = attr_offset;
            }

            index_built_ = true;
        }
    }

    // GisStorageSystem 实现
    GisStorageSystem::GisStorageSystem(const std::string& output_dir)
        : output_dir_(output_dir) {
        std::filesystem::create_directories(output_dir);
    }

    std::unique_ptr<GeometryData> GisStorageSystem::readGeometry(uint64_t feature_id) {
        if (!geometry_storage_) {
            throw std::runtime_error("几何存储未初始化");
        }
        return geometry_storage_->readGeometry(feature_id);
    }

    std::unique_ptr<AttributeData> GisStorageSystem::readAttribute(uint64_t feature_id) {
        if (!attribute_storage_) {
            throw std::runtime_error("属性存储未初始化");
        }
        return attribute_storage_->readAttribute(feature_id);
    }

    std::vector<uint64_t> GisStorageSystem::getAllFeatureIds() {
        if (!geometry_storage_) {
            return {};
        }
        return geometry_storage_->getAllFeatureIds();
    }

    std::map<uint64_t, std::unique_ptr<GeometryData>> GisStorageSystem::readGeometries(const std::vector<uint64_t>& feature_ids) {
        std::map<uint64_t, std::unique_ptr<GeometryData>> results;

        // 使用多线程并行读取
        std::vector<std::future<std::pair<uint64_t, std::unique_ptr<GeometryData>>>> futures;

        for (uint64_t fid : feature_ids) {
            futures.push_back(std::async(std::launch::async, [this, fid]() {
                try {
                    auto geom = this->readGeometry(fid);
                    return std::make_pair(fid, std::move(geom));
                } catch (const std::exception& e) {
                    std::cerr << "读取几何数据失败 FID " << fid << ": " << e.what() << std::endl;
                    return std::make_pair(fid, std::unique_ptr<GeometryData>(nullptr));
                }
            }));
        }

        for (auto& future : futures) {
            auto result = future.get();
            if (result.second) {
                results[result.first] = std::move(result.second);
            }
        }

        return results;
    }

    std::map<uint64_t, std::unique_ptr<AttributeData>> GisStorageSystem::readAttributes(const std::vector<uint64_t>& feature_ids) {
        std::map<uint64_t, std::unique_ptr<AttributeData>> results;

        // 使用多线程并行读取
        std::vector<std::future<std::pair<uint64_t, std::unique_ptr<AttributeData>>>> futures;

        for (uint64_t fid : feature_ids) {
            futures.push_back(std::async(std::launch::async, [this, fid]() {
                try {
                    auto attr = this->readAttribute(fid);
                    return std::make_pair(fid, std::move(attr));
                } catch (const std::exception& e) {
                    std::cerr << "读取属性数据失败 FID " << fid << ": " << e.what() << std::endl;
                    return std::make_pair(fid, std::unique_ptr<AttributeData>(nullptr));
                }
            }));
        }

        for (auto& future : futures) {
            auto result = future.get();
            if (result.second) {
                results[result.first] = std::move(result.second);
            }
        }

        return results;
    }

    void GisStorageSystem::initializeStorageFiles(const std::string& shapefile_path) {
        std::filesystem::path path(shapefile_path);
        shapefile_name_ = path.stem().string();

        std::string geom_file = output_dir_ + "/" + shapefile_name_ + "_geom.dat";
        std::string attr_file = output_dir_ + "/" + shapefile_name_ + "_attr.dat";

        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file);
    }

    // ShapefileConverter 实现
    ShapefileConverter::ShapefileConverter(const std::string& shapefile_path, const std::string& output_dir)
        : shapefile_path_(shapefile_path)
        , output_dir_(output_dir) {
        // 提取shapefile文件名（不含扩展名）作为前缀
        std::filesystem::path path(shapefile_path);
        shapefile_name_ = path.stem().string();

        // 创建输出目录
        std::filesystem::create_directories(output_dir);

        // 构造文件路径
        std::string geom_file_path = output_dir + "/" + shapefile_name_ + "_geom.dat";
        std::string attr_file_path = output_dir + "/" + shapefile_name_ + "_attr.dat";
        index_file_ = output_dir + "/" + shapefile_name_ + "_index.dat";

        // 清空现有文件（如果存在）
        std::ofstream(geom_file_path, std::ios::binary).close();
        std::ofstream(attr_file_path, std::ios::binary).close();
        std::ofstream(index_file_, std::ios::binary).close();

        // 初始化存储管理器
        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file_path);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file_path);
    }

    std::vector<uint64_t> ShapefileConverter::convert() {
        std::cout << "开始转换Shapefile: " << shapefile_path_ << std::endl;

        // 注册所有GDAL驱动
        GDALAllRegister();

        // 打开Shapefile
        GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(shapefile_path_.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        if (!dataset) {
            throw std::runtime_error("无法打开Shapefile: " + shapefile_path_);
        }

        OGRLayer* layer = dataset->GetLayer(0);
        if (!layer) {
            GDALClose(dataset);
            throw std::runtime_error("无法获取图层");
        }

        int feature_count = layer->GetFeatureCount();
        std::cout << "共 " << feature_count << " 个要素" << std::endl;

        // 获取字段定义
        OGRFeatureDefn* layer_defn = layer->GetLayerDefn();
        std::vector<std::string> field_names;
        std::vector<OGRFieldType> field_types;

        for (int i = 0; i < layer_defn->GetFieldCount(); ++i) {
            OGRFieldDefn* field_defn = layer_defn->GetFieldDefn(i);
            field_names.push_back(field_defn->GetNameRef());
            field_types.push_back(field_defn->GetType());
        }

        // 创建字段信息并序列化存储
        nlohmann::json field_info;
        field_info["names"] = field_names;
        field_info["types"] = field_types;

        std::string field_info_str = field_info.dump();
        std::vector<uint8_t> field_info_bytes(field_info_str.begin(), field_info_str.end());

        // 将字段信息写入属性存储的开头
        std::ofstream attr_file(attribute_storage_->getAttributeFilePath(), std::ios::binary);
        uint32_t field_info_length = static_cast<uint32_t>(field_info_bytes.size());
        attr_file.write(reinterpret_cast<const char*>(&field_info_length), sizeof(uint32_t));
        attr_file.write(reinterpret_cast<const char*>(field_info_bytes.data()), field_info_bytes.size());
        attr_file.close();

        // 创建索引结构（与Python版本保持一致）
        nlohmann::json index_data;
        index_data["version"] = 1;
        index_data["data"]["features"] = nlohmann::json::object();
        std::vector<uint64_t> valid_fids;

        int processed_count = 0;
        OGRFeature* feature;
        layer->ResetReading();

        while ((feature = layer->GetNextFeature()) != nullptr) {
            uint64_t fid = feature->GetFID();
            OGRGeometry* geom = feature->GetGeometryRef();

            if (!geom) {
                OGRFeature::DestroyFeature(feature);
                continue;
            }

            // 处理几何数据
            std::vector<Coordinate> coords = extractGeometryCoordinates(geom);
            if (coords.empty()) {
                OGRFeature::DestroyFeature(feature);
                continue;
            }

            // 确定几何类型
            GeometryType geom_type = GeometryType::POINT;
            OGRwkbGeometryType geom_type_code = geom->getGeometryType();

            if (geom_type_code == wkbPoint || geom_type_code == wkbPoint25D || geom_type_code == wkbMultiPoint || geom_type_code == wkbMultiPoint25D) {
                geom_type = GeometryType::POINT;
            } else if (geom_type_code == wkbLineString || geom_type_code == wkbLineString25D || geom_type_code == wkbMultiLineString || geom_type_code == wkbMultiLineString25D) {
                geom_type = GeometryType::LINE;
            } else if (geom_type_code == wkbPolygon || geom_type_code == wkbPolygon25D || geom_type_code == wkbMultiPolygon || geom_type_code == wkbMultiPolygon25D) {
                geom_type = GeometryType::POLYGON;
            } else {
                std::cout << "跳过不支持的几何类型: " << geom_type_code << std::endl;
                OGRFeature::DestroyFeature(feature);
                continue;
            }

            // 计算边界框
            BBox bbox = calculateBBox(coords);

            // 使用优化的差分编码压缩坐标数据
            std::vector<uint8_t> coord_data = encodeCoordinatesDeltaOptimized(coords);

            // 创建几何数据对象
            GeometryData geom_data(fid, geom_type, coord_data, bbox);

            // 写入几何文件并记录偏移位置
            int64_t geom_offset = -1;
            try {
                geom_offset = geometry_storage_->writeGeometry(geom_data);
            } catch (const std::exception& e) {
                std::cout << "几何数据写入失败 for FID " << fid << ": " << e.what() << std::endl;
            }

            // 处理属性数据
            std::map<std::string, std::string> props;
            for (const auto& field_name : field_names) {
                try {
                    OGRFieldDefn* field_defn = layer_defn->GetFieldDefn(layer_defn->GetFieldIndex(field_name.c_str()));
                    if (field_defn) {
                        switch (field_defn->GetType()) {
                            case OFTInteger:
                                props[field_name] = std::to_string(feature->GetFieldAsInteger(field_name.c_str()));
                                break;
                            case OFTInteger64:
                                props[field_name] = std::to_string(feature->GetFieldAsInteger64(field_name.c_str()));
                                break;
                            case OFTReal:
                                props[field_name] = std::to_string(feature->GetFieldAsDouble(field_name.c_str()));
                                break;
                            case OFTString:
                                props[field_name] = feature->GetFieldAsString(field_name.c_str());
                                break;
                            default:
                                props[field_name] = feature->GetFieldAsString(field_name.c_str());
                                break;
                        }
                    }
                } catch (...) {
                    props[field_name] = "";
                }
            }

            AttributeData attr_data(fid, props);
            int64_t attr_offset = -1;
            try {
                attr_offset = attribute_storage_->writeAttribute(attr_data);
            } catch (const std::exception& e) {
                std::cout << "属性数据写入失败 for FID " << fid << ": " << e.what() << std::endl;
            }

            processed_count++;
            if (processed_count % 1000 == 0) {
                std::cout << "已处理 " << processed_count << " 个要素" << std::endl;
            }

            // 更新索引（与Python版本保持一致的结构）
            if (geom_offset != -1 && attr_offset != -1) {
                index_data["data"]["features"][std::to_string(fid)] = {{"geom_offset", geom_offset}, {"attr_offset", attr_offset}};
                valid_fids.push_back(fid);
            } else {
                std::cout << "FID " << fid << " 写入失败: geom_offset=" << geom_offset << ", attr_offset=" << attr_offset << std::endl;
            }

            OGRFeature::DestroyFeature(feature);
        }

        // 保存索引数据
        saveIndexData(index_data);

        GDALClose(dataset);

        std::cout << "转换完成，共处理 " << processed_count << " 个要素" << std::endl;
        std::cout << "- 几何数据: " << geometry_storage_->getGeometryFilePath() << std::endl;
        std::cout << "- 属性数据: " << attribute_storage_->getAttributeFilePath() << std::endl;
        std::cout << "- 索引数据: " << index_file_ << std::endl;

        return valid_fids;
    }

    std::string ShapefileConverter::getGeometryFilePath() const {
        return geometry_storage_->getGeometryFilePath();
    }

    std::string ShapefileConverter::getAttributeFilePath() const {
        return attribute_storage_->getAttributeFilePath();
    }

    std::string ShapefileConverter::getIndexFilePath() const {
        return index_file_;
    }

    BBox ShapefileConverter::calculateBBox(const std::vector<Coordinate>& coordinates) {
        if (coordinates.empty()) {
            return BBox();
        }

        double min_x = coordinates[0].x;
        double min_y = coordinates[0].y;
        double max_x = coordinates[0].x;
        double max_y = coordinates[0].y;

        for (const auto& coord : coordinates) {
            min_x = std::min(min_x, coord.x);
            min_y = std::min(min_y, coord.y);
            max_x = std::max(max_x, coord.x);
            max_y = std::max(max_y, coord.y);
        }

        return BBox(min_x, min_y, max_x, max_y);
    }

    std::vector<uint8_t> ShapefileConverter::encodeCoordinatesDelta(const std::vector<Coordinate>& coordinates) {
        if (coordinates.empty()) {
            return {};
        }

        std::vector<uint8_t> data;
        data.reserve(16 + (coordinates.size() - 1) * 8);

        // 第一个点存储绝对坐标
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].x), reinterpret_cast<const uint8_t*>(&coordinates[0].x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].y), reinterpret_cast<const uint8_t*>(&coordinates[0].y) + sizeof(double));

        // 后续点存储相对于前一个点的偏移量
        for (size_t i = 1; i < coordinates.size(); ++i) {
            float dx = static_cast<float>(coordinates[i].x - coordinates[i - 1].x);
            float dy = static_cast<float>(coordinates[i].y - coordinates[i - 1].y);

            data.insert(data.end(), reinterpret_cast<uint8_t*>(&dx), reinterpret_cast<uint8_t*>(&dx) + sizeof(float));
            data.insert(data.end(), reinterpret_cast<uint8_t*>(&dy), reinterpret_cast<uint8_t*>(&dy) + sizeof(float));
        }

        return data;
    }

    std::vector<uint8_t> ShapefileConverter::encodeCoordinatesDeltaOptimized(const std::vector<Coordinate>& coordinates) {
        if (coordinates.empty()) {
            return {};
        }

        if (coordinates.size() == 1) {
            // 单点情况，直接存储绝对坐标
            std::vector<uint8_t> data;
            data.reserve(16);
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].x), reinterpret_cast<const uint8_t*>(&coordinates[0].x) + sizeof(double));
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].y), reinterpret_cast<const uint8_t*>(&coordinates[0].y) + sizeof(double));
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

        std::vector<uint8_t> data;

        // 存储第一个点的绝对坐标
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].x), reinterpret_cast<const uint8_t*>(&coordinates[0].x) + sizeof(double));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&coordinates[0].y), reinterpret_cast<const uint8_t*>(&coordinates[0].y) + sizeof(double));

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

    std::vector<Coordinate> ShapefileConverter::extractGeometryCoordinates(OGRGeometry* geometry) {
        std::vector<Coordinate> coordinates;
        extractCoordinatesRecursive(geometry, coordinates);
        return coordinates;
    }

    std::vector<Coordinate> ShapefileConverter::extractPointCoordinates(OGRGeometry* geometry) {
        std::vector<Coordinate> coordinates;

        if (geometry->getGeometryType() == wkbPoint || geometry->getGeometryType() == wkbPoint25D) {
            OGRPoint* point = static_cast<OGRPoint*>(geometry);
            coordinates.emplace_back(point->getX(), point->getY());
        } else if (geometry->getGeometryType() == wkbMultiPoint || geometry->getGeometryType() == wkbMultiPoint25D) {
            OGRMultiPoint* multi_point = static_cast<OGRMultiPoint*>(geometry);
            for (int i = 0; i < multi_point->getNumGeometries(); ++i) {
                OGRPoint* point = static_cast<OGRPoint*>(multi_point->getGeometryRef(i));
                coordinates.emplace_back(point->getX(), point->getY());
            }
        }

        return coordinates;
    }

    std::vector<Coordinate> ShapefileConverter::extractLineCoordinates(OGRGeometry* geometry) {
        std::vector<Coordinate> coordinates;
        extractCoordinatesRecursive(geometry, coordinates);
        return coordinates;
    }

    std::vector<Coordinate> ShapefileConverter::extractPolygonCoordinates(OGRGeometry* geometry) {
        std::vector<Coordinate> coordinates;
        extractCoordinatesRecursive(geometry, coordinates);
        return coordinates;
    }

    void ShapefileConverter::extractCoordinatesRecursive(OGRGeometry* geometry, std::vector<Coordinate>& coordinates) {
        if (!geometry) {
            return;
        }

        OGRwkbGeometryType geom_type = geometry->getGeometryType();

        if (geom_type == wkbPoint || geom_type == wkbPoint25D) {
            OGRPoint* point = static_cast<OGRPoint*>(geometry);
            coordinates.emplace_back(point->getX(), point->getY());
        } else if (geom_type == wkbLineString || geom_type == wkbLineString25D) {
            OGRLineString* line = static_cast<OGRLineString*>(geometry);
            for (int i = 0; i < line->getNumPoints(); ++i) {
                coordinates.emplace_back(line->getX(i), line->getY(i));
            }
        } else if (geom_type == wkbPolygon || geom_type == wkbPolygon25D) {
            OGRPolygon* polygon = static_cast<OGRPolygon*>(geometry);
            OGRLinearRing* ring = polygon->getExteriorRing();
            if (ring) {
                for (int i = 0; i < ring->getNumPoints(); ++i) {
                    coordinates.emplace_back(ring->getX(i), ring->getY(i));
                }
            }
            // 处理内环（洞）
            for (int j = 0; j < polygon->getNumInteriorRings(); ++j) {
                OGRLinearRing* inner_ring = polygon->getInteriorRing(j);
                if (inner_ring) {
                    for (int i = 0; i < inner_ring->getNumPoints(); ++i) {
                        coordinates.emplace_back(inner_ring->getX(i), inner_ring->getY(i));
                    }
                }
            }
        } else if (geometry->IsEmpty() == FALSE) {
            // 复合几何类型，递归处理
            OGRGeometryCollection* collection = dynamic_cast<OGRGeometryCollection*>(geometry);
            if (collection) {
                for (int i = 0; i < collection->getNumGeometries(); ++i) {
                    extractCoordinatesRecursive(collection->getGeometryRef(i), coordinates);
                }
            }
        }
    }

    void ShapefileConverter::saveIndexData(const nlohmann::json& index_data) {
        // 保存索引数据到JSON文件（与Python版本保持一致）
        std::ofstream index_file(index_file_);
        if (!index_file.is_open()) {
            throw std::runtime_error("无法创建索引文件: " + index_file_);
        }

        // 写入JSON格式的索引数据
        index_file << index_data.dump(4); // 使用4空格缩进，便于阅读
        index_file.close();
    }

} // namespace GisStorage
