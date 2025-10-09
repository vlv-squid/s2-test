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
        , string_pool_file_(string_pool_file) {
        // 创建目录
        std::filesystem::path file_path(attribute_file);
        std::filesystem::create_directories(file_path.parent_path());
    }

    AttributeStorage::~AttributeStorage() {
        CleanupMmap();
    }

    int64_t AttributeStorage::WriteAttribute(const AttributeData& attribute) {
        // 使用追加模式
        std::ofstream file(attribute_file_, std::ios::binary | std::ios::app);
        if (!file) {
            throw std::runtime_error("无法打开属性文件进行写入: " + attribute_file_);
        }

        int64_t offset = file.tellp();
        std::vector<uint8_t> attr_binary = serializer_.SerializeAttributes(attribute);

        if (!attr_binary.empty()) {
            file.write(reinterpret_cast<const char*>(attr_binary.data()), attr_binary.size());
            file.flush(); // 确保数据写入磁盘
            // 清除缓存
            cache_.clear();
            return offset;
        } else {
            throw std::runtime_error("属性数据序列化失败 for FID " + std::to_string(attribute.GetFeatureId()));
        }
    }

    std::unique_ptr<AttributeData> AttributeStorage::ReadAttribute(uint64_t feature_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. 先检查缓存
        auto cache_it = cache_.find(feature_id);
        if (cache_it != cache_.end()) {
            return std::make_unique<AttributeData>(*cache_it->second);
        }

        // 2. 使用分块索引获取偏移量
        int64_t offset = -1;
        if (use_chunked_mode_) {
            offset = GetChunkedOffset(feature_id);
        } else {
            BuildChunkedIndex();
            if (use_chunked_mode_) {
                offset = GetChunkedOffset(feature_id);
            }
        }

        if (offset < 0) {
            return nullptr; // 要素不存在
        }

        // 3. 读取属性数据
        std::unique_ptr<AttributeData> result = ReadAttributeAtOffset(feature_id, offset);

        // 4. 更新缓存
        if (result) {
            UpdateCache(feature_id, std::make_unique<AttributeData>(*result));
        }

        return result;
    }

    std::unique_ptr<AttributeData> AttributeStorage::ReadAttributeOnDemand(uint64_t feature_id) {
        // 按需读取，只使用分块索引
        int64_t offset = -1;

        // 尝试使用分块索引获取偏移量
        if (use_chunked_mode_) {
            offset = GetChunkedOffset(feature_id);
        } else {
            BuildChunkedIndex();
            if (use_chunked_mode_) {
                offset = GetChunkedOffset(feature_id);
            }
        }

        if (offset < 0) {
            return nullptr; // 要素不存在
        }

        // 使用偏移量直接读取
        return ReadAttributeAtOffset(feature_id, offset);
    }

    std::unique_ptr<AttributeData> AttributeStorage::ReadAttributeAtOffset(uint64_t feature_id, int64_t offset) {
        // 确保字符串池已加载（按需加载）
        if (serializer_.GetPoolSize() == 0) {
            LoadStringPool();
        }

        // 使用持久文件句柄而不是每次重新打开文件
        if (attr_fd_ == -1) {
            attr_fd_ = open(attribute_file_.c_str(), O_RDONLY);
            if (attr_fd_ == -1) {
                return nullptr;
            }
        }

        // 使用pread进行原子读取，避免文件指针操作
        uint64_t stored_fid;
        uint32_t prop_count;

        // 读取feature_id
        if (pread(attr_fd_, &stored_fid, sizeof(uint64_t), offset) != sizeof(uint64_t)) {
            return nullptr;
        }

        // 读取属性数量
        if (pread(attr_fd_, &prop_count, sizeof(uint32_t), offset + sizeof(uint64_t)) != sizeof(uint32_t)) {
            return nullptr;
        }

        // 计算需要读取的数据大小（包括FID、属性数量和属性数据）
        size_t data_size = sizeof(uint64_t) + sizeof(uint32_t) + prop_count * sizeof(uint32_t) * 2;

        // 读取完整的数据
        std::vector<uint8_t> data(data_size);
        if (pread(attr_fd_, data.data(), data_size, offset) != static_cast<ssize_t>(data_size)) {
            throw std::runtime_error("属性数据不完整 for FID " + std::to_string(feature_id));
        }

        return serializer_.DeserializeAttributes(data);
    }

    bool AttributeStorage::HasFeature(uint64_t feature_id) {
        // 流式读取模式：通过尝试读取来判断要素是否存在
        return ReadAttributeOnDemand(feature_id) != nullptr;
    }

    void AttributeStorage::ClearCache() {
        cache_.clear();
    }

    void AttributeStorage::SaveStringPool() {
        std::vector<uint8_t> pool_data = serializer_.SerializeStringPool();

        std::ofstream file(string_pool_file_, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(pool_data.data()), pool_data.size());
            file.close();
            // std::cout << "字符串池已保存到: " << string_pool_file_ << std::endl;
        } else {
            std::cerr << "无法保存字符串池到: " << string_pool_file_ << std::endl;
        }
    }

    void AttributeStorage::LoadStringPool() {
        // 使用静态变量确保字符串池只加载一次
        static std::once_flag pool_loaded_flag;
        static std::string last_pool_file;

        // 如果已经加载过相同的字符串池文件，直接返回
        if (last_pool_file == string_pool_file_ && serializer_.GetPoolSize() > 0) {
            return;
        }

        std::call_once(pool_loaded_flag, [this]() {
            if (!std::filesystem::exists(string_pool_file_)) {
                std::cout << "字符串池文件不存在: " << string_pool_file_ << std::endl;
                return;
            }

            // 根据配置决定是否使用mmap方式加载字符串池
            if (use_mmap_mode_ && serializer_.LoadStringPoolFromFile(string_pool_file_)) {
                auto stats = serializer_.GetCompressionStats();
                std::cout << "字符串池已使用mmap加载: " << stats.unique_strings << " 个唯一字符串" << std::endl;
                last_pool_file = string_pool_file_;
                return;
            }

            // 如果mmap失败，回退到传统方式
            std::cout << "mmap加载失败，回退到传统方式加载字符串池..." << std::endl;

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
            serializer_.DeserializeStringPool(pool_data);

            auto stats = serializer_.GetCompressionStats();
            std::cout << "字符串池已使用传统方式加载: " << stats.unique_strings << " 个唯一字符串" << std::endl;
            last_pool_file = string_pool_file_;
        });
    }

    AttributeSerializer::CompressionStats AttributeStorage::GetCompressionStats() const {
        return serializer_.GetCompressionStats();
    }

    AttributeStorage::StorageStats AttributeStorage::GetStorageStats() const {
        StorageStats stats;
        auto compression_stats = serializer_.GetCompressionStats();

        stats.total_features = 0; // 流式读取模式：不支持统计总要素数
        stats.total_original_size = compression_stats.original_size;
        stats.total_compressed_size = compression_stats.compressed_size;
        stats.compression_ratio = compression_stats.compression_ratio;
        stats.string_pool_size = serializer_.GetPoolSize();
        stats.string_pool_saved_bytes = compression_stats.original_size - compression_stats.compressed_size;

        return stats;
    }

    void AttributeStorage::BuildChunkedIndex() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (use_chunked_mode_) {
            return; // 已经构建过
        }

        // 尝试从文件加载分块索引
        std::string index_file = attribute_file_ + ".chunked_idx";
        if (std::filesystem::exists(index_file)) {
            std::cout << "发现属性分块索引文件，正在加载..." << std::endl;
            LoadChunkedIndex(index_file);
            if (use_chunked_mode_) {
                return; // 成功加载，直接返回
            }
        }

        // 如果加载失败或文件不存在，重新构建
        std::cout << "重新构建属性分块索引..." << std::endl;
        std::ifstream file(attribute_file_, std::ios::binary);
        if (!file) {
            std::cout << "无法打开属性文件构建分块索引: " << attribute_file_ << std::endl;
            return;
        }

        index_chunks_.clear();
        IndexChunk current_chunk;
        current_chunk.start_fid = 0;
        current_chunk.end_fid = 0;
        current_chunk.loaded = true; // 构建时直接加载

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
            size_t data_size = prop_count * sizeof(uint32_t) * 2;
            file.seekg(data_size, std::ios::cur);

            if (file.good()) {
                // 检查是否需要创建新块
                if (current_chunk.offset_map.size() >= chunk_size_) {
                    current_chunk.end_fid = feature_id - 1;
                    index_chunks_.push_back(current_chunk);

                    // 开始新块
                    current_chunk.offset_map.clear();
                    current_chunk.start_fid = feature_id;
                    current_chunk.end_fid = feature_id;
                }

                current_chunk.offset_map[feature_id] = current_offset;
                current_chunk.end_fid = feature_id; // 更新结束FID
            }
        }

        // 添加最后一个块
        if (!current_chunk.offset_map.empty()) {
            index_chunks_.push_back(current_chunk);
        }

        use_chunked_mode_ = true;
        std::cout << "构建了 " << index_chunks_.size() << " 个索引块" << std::endl;

        // 打印调试信息
        for (size_t i = 0; i < index_chunks_.size() && i < 3; ++i) {
            const auto& chunk = index_chunks_[i];
            std::cout << "属性块 " << i << ": FID范围 [" << chunk.start_fid << "-" << chunk.end_fid << "], 条目数: " << chunk.offset_map.size() << std::endl;
        }

        // 构建完成后保存到文件
        SaveChunkedIndex(index_file);
    }

    int64_t AttributeStorage::GetChunkedOffset(uint64_t feature_id) const {
        // 二分查找对应的块
        for (auto& chunk : index_chunks_) {
            if (feature_id >= chunk.start_fid && feature_id <= chunk.end_fid) {
                // 加载块（如果未加载）
                if (!chunk.loaded) {
                    LoadIndexChunk(const_cast<IndexChunk*>(&chunk));
                }

                auto it = chunk.offset_map.find(feature_id);
                if (it != chunk.offset_map.end()) {
                    return it->second;
                }
                break;
            }
        }
        return -1;
    }

    void AttributeStorage::LoadIndexChunk(IndexChunk* chunk) const {
        if (chunk->loaded) {
            return;
        }

        chunk->loaded = true;
    }

    void AttributeStorage::UpdateCache(uint64_t fid, std::unique_ptr<AttributeData> data) const {
        // 如果缓存已满，清除一半缓存
        if (cache_.size() >= max_cache_size_) {
            auto it = cache_.begin();
            for (size_t i = 0; i < max_cache_size_ / 2 && it != cache_.end(); ++i) {
                it = cache_.erase(it);
            }
        }

        // 添加新条目到缓存
        cache_[fid] = std::move(data);
    }

    void AttributeStorage::CleanupMmap() {
        if (attr_mmap_ && attr_mmap_ != MAP_FAILED) {
            munmap(attr_mmap_, attr_size_);
            attr_mmap_ = nullptr;
        }
        if (attr_fd_ >= 0) {
            close(attr_fd_);
            attr_fd_ = -1;
        }
        attr_size_ = 0;
    }

    void AttributeStorage::SaveChunkedIndex(const std::string& index_file) {
        if (!use_chunked_mode_ || index_chunks_.empty()) {
            std::cout << "没有属性分块索引需要保存" << std::endl;
            return;
        }

        std::ofstream file(index_file, std::ios::binary);
        if (!file) {
            std::cout << "无法创建属性分块索引文件: " << index_file << std::endl;
            return;
        }

        try {
            // 写入文件头
            struct ChunkedIndexHeader {
                uint32_t version = 1; // 版本号
                uint32_t chunk_count;
                uint64_t chunk_size;
                uint64_t reserved = 0; // 保留字段
            } header;

            header.chunk_count = static_cast<uint32_t>(index_chunks_.size());
            header.chunk_size = chunk_size_;
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));

            // 写入每个索引块
            for (const auto& chunk : index_chunks_) {
                // 写入块头
                struct ChunkHeader {
                    uint64_t start_fid;
                    uint64_t end_fid;
                    uint32_t entry_count;
                    uint32_t reserved = 0;
                } chunk_header;

                chunk_header.start_fid = chunk.start_fid;
                chunk_header.end_fid = chunk.end_fid;
                chunk_header.entry_count = static_cast<uint32_t>(chunk.offset_map.size());
                file.write(reinterpret_cast<const char*>(&chunk_header), sizeof(chunk_header));

                // 写入偏移映射
                for (const auto& [fid, offset] : chunk.offset_map) {
                    struct IndexEntry {
                        uint64_t feature_id;
                        int64_t offset;
                    } entry;

                    entry.feature_id = fid;
                    entry.offset = offset;
                    file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
                }
            }

            file.flush();
            std::cout << "属性分块索引已保存到: " << index_file << " (块数: " << index_chunks_.size() << ")" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "保存属性分块索引文件失败: " << e.what() << std::endl;
        }
    }

    void AttributeStorage::LoadChunkedIndex(const std::string& index_file) {
        if (!std::filesystem::exists(index_file)) {
            std::cout << "属性分块索引文件不存在: " << index_file << std::endl;
            return;
        }

        std::ifstream file(index_file, std::ios::binary);
        if (!file) {
            std::cout << "无法打开属性分块索引文件: " << index_file << std::endl;
            return;
        }

        try {
            // 读取文件头
            struct ChunkedIndexHeader {
                uint32_t version;
                uint32_t chunk_count;
                uint64_t chunk_size;
                uint64_t reserved;
            } header;

            if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
                std::cout << "无法读取属性分块索引文件头" << std::endl;
                return;
            }

            // 检查版本号
            if (header.version != 1) {
                std::cout << "不支持的属性分块索引版本: " << header.version << std::endl;
                return;
            }

            std::cout << "加载属性分块索引: 版本=" << header.version << ", 块数=" << header.chunk_count << ", 块大小=" << header.chunk_size << std::endl;

            // 设置块大小
            chunk_size_ = header.chunk_size;

            // 清空现有索引
            index_chunks_.clear();
            index_chunks_.reserve(header.chunk_count);

            // 读取每个索引块
            for (uint32_t i = 0; i < header.chunk_count; ++i) {
                IndexChunk chunk;

                // 读取块头
                struct ChunkHeader {
                    uint64_t start_fid;
                    uint64_t end_fid;
                    uint32_t entry_count;
                    uint32_t reserved;
                } chunk_header;

                if (!file.read(reinterpret_cast<char*>(&chunk_header), sizeof(chunk_header))) {
                    std::cout << "读取属性索引块 " << i << " 头失败" << std::endl;
                    break;
                }

                chunk.start_fid = chunk_header.start_fid;
                chunk.end_fid = chunk_header.end_fid;
                chunk.loaded = true; // 从文件加载的块直接标记为已加载

                // 读取偏移映射
                chunk.offset_map.reserve(chunk_header.entry_count);
                for (uint32_t j = 0; j < chunk_header.entry_count; ++j) {
                    struct IndexEntry {
                        uint64_t feature_id;
                        int64_t offset;
                    } entry;

                    if (!file.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
                        std::cout << "读取属性索引条目 " << j << " 失败" << std::endl;
                        break;
                    }

                    chunk.offset_map[entry.feature_id] = entry.offset;
                }

                index_chunks_.push_back(std::move(chunk));
            }

            use_chunked_mode_ = true;
            std::cout << "属性分块索引加载完成，共 " << index_chunks_.size() << " 个块" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "加载属性分块索引文件失败: " << e.what() << std::endl;
        }
    }

} // namespace GisStorage
