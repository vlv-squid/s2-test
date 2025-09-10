//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/attribute_serializer.h"

#include <stdexcept>
#include <cstring>

namespace GisStorage {

    AttributeSerializer::AttributeSerializer() {
        stats_.original_size = 0;
        stats_.compressed_size = 0;
        stats_.compression_ratio = 0.0;
        stats_.unique_strings = 0;
        stats_.total_strings = 0;
    }

    std::vector<uint8_t> AttributeSerializer::SerializeAttributes(const AttributeData& attribute) {
        // 获取属性
        auto properties = attribute.GetProperties();

        // 预计算数据大小以提高性能
        size_t data_size = sizeof(uint64_t) + sizeof(uint32_t) + properties.size() * sizeof(uint32_t) * 2;
        std::vector<uint8_t> data;
        data.reserve(data_size);

        // 写入feature_id
        uint64_t feature_id = attribute.GetFeatureId();
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&feature_id), reinterpret_cast<const uint8_t*>(&feature_id) + sizeof(uint64_t));

        // 写入属性数量
        uint32_t prop_count = static_cast<uint32_t>(properties.size());
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&prop_count), reinterpret_cast<const uint8_t*>(&prop_count) + sizeof(uint32_t));

        // 计算原始大小（用于统计）
        size_t original_size = sizeof(uint64_t) + sizeof(uint32_t); // feature_id + prop_count

        // 序列化每个属性对
        for (const auto& [key, value] : properties) {
            // 获取字符串ID（如果不存在则添加到池中）
            uint32_t key_id = string_pool_.GetStringId(key);
            uint32_t value_id = string_pool_.GetStringId(value);

            // 写入key_id和value_id
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&key_id), reinterpret_cast<const uint8_t*>(&key_id) + sizeof(uint32_t));
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&value_id), reinterpret_cast<const uint8_t*>(&value_id) + sizeof(uint32_t));

            // 计算原始大小
            original_size += key.length() + value.length() + 2; // 字符串长度 + 引号
        }

        // 更新统计信息
        UpdateStats(original_size, data.size());

        return data;
    }

    std::unique_ptr<AttributeData> AttributeSerializer::DeserializeAttributes(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(uint64_t) + sizeof(uint32_t)) {
            throw std::runtime_error("属性数据长度不足");
        }

        size_t offset = 0;

        // 读取feature_id
        uint64_t feature_id;
        std::memcpy(&feature_id, &data[offset], sizeof(uint64_t));
        offset += sizeof(uint64_t);

        // 读取属性数量
        uint32_t prop_count;
        std::memcpy(&prop_count, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // 创建属性映射
        std::map<std::string, std::string> properties;

        // 重建属性映射
        for (uint32_t i = 0; i < prop_count; ++i) {
            if (offset + sizeof(uint32_t) * 2 > data.size()) {
                throw std::runtime_error("属性数据不完整");
            }

            // 读取key_id和value_id
            uint32_t key_id, value_id;
            std::memcpy(&key_id, &data[offset], sizeof(uint32_t));
            offset += sizeof(uint32_t);
            std::memcpy(&value_id, &data[offset], sizeof(uint32_t));
            offset += sizeof(uint32_t);

            // 从字符串池中获取字符串
            std::string key = string_pool_.GetString(key_id);
            std::string value = string_pool_.GetString(value_id);

            // 检查字符串ID是否超出字符串池范围
            if (key_id >= string_pool_.GetPoolSize() || value_id >= string_pool_.GetPoolSize()) {
                throw std::runtime_error("字符串池中找不到对应的字符串 - key_id: " + std::to_string(key_id) + ", value_id: " + std::to_string(value_id) + ", 字符串池大小: " + std::to_string(string_pool_.GetPoolSize()));
            }

            // 允许空字符串，因为某些属性字段可能确实为空
            properties[key] = value;
        }

        return std::make_unique<AttributeData>(feature_id, properties);
    }

    std::vector<uint8_t> AttributeSerializer::SerializeStringPool() const {
        return string_pool_.Serialize();
    }

    void AttributeSerializer::DeserializeStringPool(const std::vector<uint8_t>& data) {
        string_pool_.Deserialize(data);
    }

    AttributeSerializer::CompressionStats AttributeSerializer::GetCompressionStats() const {
        stats_.unique_strings = string_pool_.GetPoolSize();
        stats_.total_strings = string_pool_.GetTotalSize();
        return stats_;
    }

    void AttributeSerializer::UpdateStats(size_t original_size, size_t compressed_size) {
        stats_.original_size += original_size;
        stats_.compressed_size += compressed_size;

        if (stats_.original_size > 0) {
            stats_.compression_ratio = (1.0 - (double)stats_.compressed_size / stats_.original_size) * 100.0;
        }
    }

} // namespace GisStorage
