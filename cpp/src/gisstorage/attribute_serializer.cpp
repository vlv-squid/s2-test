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

        // 紧凑格式：使用变长编码优化存储
        std::vector<uint8_t> data;

        // 写入feature_id (8字节)
        uint64_t feature_id = attribute.GetFeatureId();
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&feature_id), reinterpret_cast<const uint8_t*>(&feature_id) + sizeof(uint64_t));

        // 写入属性数量 (变长编码，通常1-2字节)
        uint32_t prop_count = static_cast<uint32_t>(properties.size());
        EncodeVarint(data, prop_count);

        // 计算原始大小（用于统计）
        size_t original_size = sizeof(uint64_t) + sizeof(uint32_t); // feature_id + prop_count

        // 序列化每个属性对 - 使用紧凑格式
        for (const auto& [key, value] : properties) {
            // 获取字符串ID（如果不存在则添加到池中）
            uint32_t key_id = string_pool_.GetStringId(key);
            uint32_t value_id = string_pool_.GetStringId(value);

            // 使用变长编码存储ID，通常可以节省1-2字节
            EncodeVarint(data, key_id);
            EncodeVarint(data, value_id);

            // 计算原始大小
            original_size += key.length() + value.length() + 2; // 字符串长度 + 引号
        }

        // 更新统计信息
        UpdateStats(original_size, data.size());

        return data;
    }

    std::unique_ptr<AttributeData> AttributeSerializer::DeserializeAttributes(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(uint64_t) + 1) { // 至少需要feature_id + 1字节的属性数量
            throw std::runtime_error("属性数据长度不足");
        }

        size_t offset = 0;

        // 读取feature_id
        uint64_t feature_id;
        std::memcpy(&feature_id, &data[offset], sizeof(uint64_t));
        offset += sizeof(uint64_t);

        // 读取属性数量 (变长编码)
        uint32_t prop_count;
        offset = DecodeVarint(data, offset, prop_count);

        // 创建属性映射
        std::map<std::string, std::string> properties;

        // 重建属性映射
        for (uint32_t i = 0; i < prop_count; ++i) {
            if (offset >= data.size()) {
                throw std::runtime_error("属性数据不完整");
            }

            // 读取key_id和value_id (变长编码)
            uint32_t key_id, value_id;
            offset = DecodeVarint(data, offset, key_id);
            if (offset >= data.size()) {
                throw std::runtime_error("属性数据不完整");
            }
            offset = DecodeVarint(data, offset, value_id);

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

    void AttributeSerializer::EncodeVarint(std::vector<uint8_t>& data, uint32_t value) {
        // 使用变长编码，每个字节的最高位表示是否还有后续字节
        while (value >= 0x80) {
            data.push_back(static_cast<uint8_t>(value | 0x80));
            value >>= 7;
        }
        data.push_back(static_cast<uint8_t>(value));
    }

    size_t AttributeSerializer::DecodeVarint(const std::vector<uint8_t>& data, size_t offset, uint32_t& value) {
        value = 0;
        int shift = 0;

        while (offset < data.size()) {
            uint8_t byte = data[offset++];
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;

            if ((byte & 0x80) == 0) {
                break; // 最后一个字节
            }

            shift += 7;
            if (shift >= 32) {
                throw std::runtime_error("变长编码值过大");
            }
        }

        return offset;
    }

} // namespace GisStorage
