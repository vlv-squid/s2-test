#include "gisstorage/attribute_serializer.h"

#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <iostream>

namespace GisStorage {

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

} // namespace GisStorage
