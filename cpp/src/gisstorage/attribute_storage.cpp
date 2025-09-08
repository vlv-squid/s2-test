//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/attribute_storage.h"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace GisStorage {

    // AttributeStorage 实现
    AttributeStorage::AttributeStorage(const std::string& attribute_file, const std::string& string_pool_file)
        : attribute_file_(attribute_file)
        , string_pool_file_(string_pool_file)
        , index_built_(false) {
        // 创建目录
        std::filesystem::path file_path(attribute_file);
        std::filesystem::create_directories(file_path.parent_path());

        // 只在文件不存在时创建新文件，不删除已存在的文件
        // if (std::filesystem::exists(attribute_file)) {
        //     std::filesystem::remove(attribute_file);
        // }

        // 尝试加载字符串池
        if (std::filesystem::exists(string_pool_file_)) {
            loadStringPool();
        }
    }

    int64_t AttributeStorage::writeAttribute(const AttributeData& attribute) {
        // 使用追加模式
        std::ofstream file(attribute_file_, std::ios::binary | std::ios::app);
        if (!file) {
            throw std::runtime_error("无法打开属性文件进行写入: " + attribute_file_);
        }

        int64_t offset = file.tellp();
        std::vector<uint8_t> attr_binary = serializer_.serializeAttributes(attribute);

        if (!attr_binary.empty()) {
            file.write(reinterpret_cast<const char*>(attr_binary.data()), attr_binary.size());
            file.flush(); // 确保数据写入磁盘
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

        // 读取feature_id和属性数量
        uint64_t stored_fid;
        uint32_t prop_count;

        if (!file.read(reinterpret_cast<char*>(&stored_fid), sizeof(uint64_t))) {
            throw std::runtime_error("属性数据不完整 for FID " + std::to_string(feature_id));
        }

        if (!file.read(reinterpret_cast<char*>(&prop_count), sizeof(uint32_t))) {
            throw std::runtime_error("属性数据不完整 for FID " + std::to_string(feature_id));
        }

        // 计算需要读取的数据大小（包括FID、属性数量和属性数据）
        size_t data_size = sizeof(uint64_t) + sizeof(uint32_t) + prop_count * sizeof(uint32_t) * 2;

        // 重新定位到数据开始位置
        file.seekg(it->second);

        // 读取完整的数据
        std::vector<uint8_t> data(data_size);
        if (!file.read(reinterpret_cast<char*>(data.data()), data_size)) {
            throw std::runtime_error("属性数据不完整 for FID " + std::to_string(feature_id));
        }

        return serializer_.deserializeAttributes(data);
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

    void AttributeStorage::loadIndexFromFile(const std::string& index_file) {
        if (!std::filesystem::exists(index_file)) {
            std::cout << "索引文件不存在: " << index_file << std::endl;
            return;
        }

        offset_index_.clear();

        // 首先尝试作为JSON格式读取
        std::ifstream json_file(index_file);
        if (json_file) {
            try {
                nlohmann::json index_data = nlohmann::json::parse(json_file);

                // 验证JSON结构
                if (index_data.contains("version") && index_data.contains("data") && index_data["data"].contains("features")) {
                    std::cout << "索引文件格式: JSON" << std::endl;
                    std::cout << "版本: " << index_data["version"] << std::endl;
                    std::cout << "要素数量: " << index_data["data"]["features"].size() << std::endl;

                    // 读取每个索引条目
                    int count = 0;
                    for (const auto& feature : index_data["data"]["features"].items()) {
                        uint64_t fid = std::stoull(feature.key());
                        int64_t attr_offset = feature.value()["attr_offset"];

                        offset_index_[fid] = attr_offset;

                        if (count < 5) { // 只显示前5个条目
                            std::cout << "索引条目 " << count << ": FID=" << fid << ", attr_offset=" << attr_offset << std::endl;
                        }
                        count++;
                    }

                    std::cout << "加载的索引条目数量: " << offset_index_.size() << std::endl;
                    index_built_ = true;
                    return;
                }
            } catch (const nlohmann::json::exception& e) {
                // JSON解析失败，继续尝试二进制格式
            }
        }

        // 尝试作为二进制格式读取
        std::ifstream file(index_file, std::ios::binary);
        if (!file) {
            std::cout << "无法打开索引文件: " << index_file << std::endl;
            return;
        }

        try {
            // 读取文件头
            struct IndexHeader {
                uint32_t version;
                uint32_t feature_count;
                uint64_t reserved; // 保留字段，用于对齐
            } header;

            if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
                std::cout << "无法读取索引文件头" << std::endl;
                return;
            }

            // 检查版本号是否合理（1-1000之间）
            if (header.version < 1 || header.version > 1000) {
                std::cout << "二进制索引文件版本号异常: " << header.version << std::endl;
                return;
            }

            std::cout << "索引文件格式: 二进制" << std::endl;
            std::cout << "版本: " << header.version << std::endl;
            std::cout << "要素数量: " << header.feature_count << std::endl;

            // 预分配内存以提高性能
            offset_index_.reserve(header.feature_count);

            // 读取索引条目
            struct IndexEntry {
                uint64_t feature_id;
                int64_t attr_offset;
            } entry;

            int count = 0;
            for (uint32_t i = 0; i < header.feature_count; ++i) {
                if (!file.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
                    std::cout << "索引条目读取不完整，已读取 " << i << " 个条目" << std::endl;
                    break;
                }

                offset_index_[entry.feature_id] = entry.attr_offset;

                if (count < 5) { // 只显示前5个条目
                    std::cout << "索引条目 " << count << ": FID=" << entry.feature_id << ", attr_offset=" << entry.attr_offset << std::endl;
                }
                count++;
            }

            std::cout << "加载的索引条目数量: " << offset_index_.size() << std::endl;
            index_built_ = true;

        } catch (const std::exception& e) {
            std::cout << "二进制索引文件解析失败: " << e.what() << std::endl;
        }
    }

    void AttributeStorage::saveIndexToFile(const std::string& index_file) {
        const auto& offsets = getOffsetIndex();
        if (offsets.empty()) {
            std::cout << "没有索引数据需要保存" << std::endl;
            return;
        }

        std::ofstream file(index_file, std::ios::binary);
        if (!file) {
            std::cout << "无法创建索引文件: " << index_file << std::endl;
            return;
        }

        try {
            // 写入文件头
            struct IndexHeader {
                uint32_t version = 1; // 版本号
                uint32_t feature_count;
                uint64_t reserved = 0; // 保留字段，用于对齐
            } header;

            header.feature_count = static_cast<uint32_t>(offsets.size());
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));

            // 写入索引条目
            struct IndexEntry {
                uint64_t feature_id;
                int64_t attr_offset;
            } entry;

            for (const auto& [feature_id, attr_offset] : offsets) {
                entry.feature_id = feature_id;
                entry.attr_offset = attr_offset;
                file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
            }

            file.flush();
            std::cout << "索引已保存到: " << index_file << " (条目数: " << offsets.size() << ")" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "保存索引文件失败: " << e.what() << std::endl;
        }
    }

    void AttributeStorage::saveStringPool() {
        std::vector<uint8_t> pool_data = serializer_.serializeStringPool();

        std::ofstream file(string_pool_file_, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(pool_data.data()), pool_data.size());
            file.close();
            std::cout << "字符串池已保存到: " << string_pool_file_ << std::endl;
        } else {
            std::cerr << "无法保存字符串池到: " << string_pool_file_ << std::endl;
        }
    }

    void AttributeStorage::loadStringPool() {
        if (!std::filesystem::exists(string_pool_file_)) {
            std::cout << "字符串池文件不存在: " << string_pool_file_ << std::endl;
            return;
        }

        std::ifstream file(string_pool_file_, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "无法打开字符串池文件: " << string_pool_file_ << std::endl;
            return;
        }

        // 读取文件内容
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> pool_data(file_size);
        file.read(reinterpret_cast<char*>(pool_data.data()), file_size);
        file.close();

        // 反序列化字符串池
        serializer_.deserializeStringPool(pool_data);

        auto stats = serializer_.getCompressionStats();
        std::cout << "字符串池已加载: " << stats.unique_strings << " 个唯一字符串" << std::endl;
    }

    AttributeSerializer::CompressionStats AttributeStorage::getCompressionStats() const {
        return serializer_.getCompressionStats();
    }

    AttributeStorage::StorageStats AttributeStorage::getStorageStats() const {
        StorageStats stats;
        auto compression_stats = serializer_.getCompressionStats();

        stats.total_features = offset_index_.size();
        stats.total_original_size = compression_stats.original_size;
        stats.total_compressed_size = compression_stats.compressed_size;
        stats.compression_ratio = compression_stats.compression_ratio;
        stats.string_pool_size = serializer_.getPoolSize();
        stats.string_pool_saved_bytes = compression_stats.original_size - compression_stats.compressed_size;

        return stats;
    }

    void AttributeStorage::buildOffsetIndex() {
        if (index_built_) {
            return;
        }

        std::ifstream file(attribute_file_, std::ios::binary);
        if (!file) {
            std::cout << "无法打开属性文件构建索引: " << attribute_file_ << std::endl;
            return;
        }

        offset_index_.clear();

        while (file.good()) {
            int64_t current_offset = file.tellg();

            // 读取feature_id
            uint64_t feature_id;
            if (!file.read(reinterpret_cast<char*>(&feature_id), sizeof(uint64_t))) {
                break;
            }

            // 读取属性数量
            uint32_t prop_count;
            if (!file.read(reinterpret_cast<char*>(&prop_count), sizeof(uint32_t))) {
                break;
            }

            // 计算数据大小并跳过
            size_t data_size = prop_count * sizeof(uint32_t) * 2; // key_id + value_id
            file.seekg(data_size, std::ios::cur);

            if (file.good()) {
                offset_index_[feature_id] = current_offset;
            }
        }

        index_built_ = true;
        std::cout << "构建了 " << offset_index_.size() << " 个属性索引条目" << std::endl;
    }

    const std::unordered_map<uint64_t, int64_t>& AttributeStorage::getOffsetIndex() {
        if (!index_built_) {
            buildOffsetIndex();
        }
        return offset_index_;
    }

} // namespace GisStorage
