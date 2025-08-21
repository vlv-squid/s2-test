#include "attribute_storage.h"
#include "attribute_serializer.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace GisStorage {

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

        if (!attr_binary.empty()) {
            file.write(reinterpret_cast<const char*>(attr_binary.data()), attr_binary.size());
            // 清除索引缓存
            offset_index_.clear();
            index_built_ = false;
            return offset;
        } else {
            throw std::runtime_error("属性数据序列化失败 for FID " + std::to_string(attribute.getFeatureId()));
        }
    }

    std::unique_ptr<AttributeData> AttributeStorage::readAttribute(uint64_t feature_id) {
        const auto& offsets = getOffsetIndex();
        auto it = offsets.find(feature_id);
        if (it == offsets.end()) {
            throw std::runtime_error("Feature ID " + std::to_string(feature_id) + " not found");
        }

        std::ifstream file(attribute_file_, std::ios::binary);
        if (!file) {
            throw std::runtime_error("无法打开属性文件: " + attribute_file_);
        }

        file.seekg(it->second);

        // 读取头部数据（与Python版本保持一致）
        std::vector<uint8_t> header_data(12);
        if (!file.read(reinterpret_cast<char*>(header_data.data()), 12)) {
            throw std::runtime_error("属性数据不完整 for FID " + std::to_string(feature_id));
        }

        // 从头部数据中提取feature_id和json_length
        uint64_t stored_fid;
        uint32_t json_length;
        std::memcpy(&stored_fid, &header_data[0], sizeof(uint64_t));
        std::memcpy(&json_length, &header_data[8], sizeof(uint32_t));

        // 读取JSON数据
        std::vector<uint8_t> json_data(json_length);
        if (json_length > 0) {
            if (!file.read(reinterpret_cast<char*>(json_data.data()), json_length)) {
                throw std::runtime_error("属性JSON数据不完整 for FID " + std::to_string(feature_id));
            }
        }

        // 组合所有数据进行反序列化
        std::vector<uint8_t> attr_data = header_data;
        attr_data.insert(attr_data.end(), json_data.begin(), json_data.end());

        return AttributeSerializer::deserializeAttributes(attr_data);
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

        std::ifstream file(attribute_file_, std::ios::binary);
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

            // 读取JSON长度
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

} // namespace GisStorage
