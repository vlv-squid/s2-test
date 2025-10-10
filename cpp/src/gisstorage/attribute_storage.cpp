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
        // 确保字符串池已加载
        if (serializer_.GetPoolSize() == 0) {
            LoadStringPool();
        }

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

    std::vector<std::unique_ptr<AttributeData>> AttributeStorage::ReadAttributesBatch(const std::vector<uint64_t>& feature_ids) {
        std::vector<std::unique_ptr<AttributeData>> results;
        results.reserve(feature_ids.size());

        // 确保字符串池已加载
        if (serializer_.GetPoolSize() == 0) {
            LoadStringPool();
        }

        // 使用持久文件句柄
        if (attr_fd_ == -1) {
            attr_fd_ = open(attribute_file_.c_str(), O_RDONLY);
            if (attr_fd_ == -1) {
                return results;
            }
        }

        // 批量读取，减少系统调用
        for (uint64_t feature_id : feature_ids) {
            // 获取偏移量
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
                results.push_back(nullptr);
                continue;
            }

            // 读取属性数据
            auto attr_data = ReadAttributeAtOffset(feature_id, offset);
            results.push_back(std::move(attr_data));
        }

        return results;
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

        // 读取feature_id
        if (pread(attr_fd_, &stored_fid, sizeof(uint64_t), offset) != sizeof(uint64_t)) {
            return nullptr;
        }

        // 读取属性数量 (变长编码，需要先读取1字节判断长度)
        uint8_t first_byte;
        if (pread(attr_fd_, &first_byte, 1, offset + sizeof(uint64_t)) != 1) {
            return nullptr;
        }

        // 计算变长编码的属性数量需要多少字节
        size_t prop_count_bytes = 1;
        if (first_byte & 0x80) {
            // 需要更多字节，简单估算最大可能的大小
            prop_count_bytes = 4; // 最多4字节
        }

        // 读取属性数量
        std::vector<uint8_t> prop_count_data(prop_count_bytes);
        if (pread(attr_fd_, prop_count_data.data(), prop_count_bytes, offset + sizeof(uint64_t)) != static_cast<ssize_t>(prop_count_bytes)) {
            return nullptr;
        }

        // 解码属性数量
        size_t temp_offset = 0;
        uint32_t prop_count_decoded;
        temp_offset = serializer_.DecodeVarint(prop_count_data, temp_offset, prop_count_decoded);

        // 计算需要读取的数据大小（包括FID、属性数量和属性数据）
        // 使用保守估算：每个属性对最多需要8字节（两个4字节的变长编码）
        size_t data_size = sizeof(uint64_t) + prop_count_bytes + prop_count_decoded * 8;

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
        // 先保存字符串池文件，不创建索引文件（避免在转换过程中阻塞）
        std::vector<uint8_t> pool_data = serializer_.SerializeStringPool();

        std::ofstream file(string_pool_file_, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(pool_data.data()), pool_data.size());
            file.close();
            std::cout << "字符串池已保存到: " << string_pool_file_ << std::endl;
        } else {
            std::cerr << "无法保存字符串池到: " << string_pool_file_ << std::endl;
        }
    }

    void AttributeStorage::CreateStringPoolIndex() {
        // 在转换完成后创建字符串池索引文件
        std::string index_file_path = string_pool_file_ + ".index";

        // 检查索引文件是否已存在
        if (std::filesystem::exists(index_file_path)) {
            std::cout << "字符串池索引文件已存在，跳过创建: " << index_file_path << std::endl;
            return;
        }

        // 检查 .pool 文件是否存在
        if (!std::filesystem::exists(string_pool_file_)) {
            std::cerr << "字符串池文件不存在，无法创建索引: " << string_pool_file_ << std::endl;
            return;
        }

        std::cout << "开始创建字符串池索引文件..." << std::endl;

        // 直接从 .pool 文件构建索引，而不是从内存中的字符串表
        // 这样可以避免遍历大量字符串导致的性能问题
        if (BuildIndexFromPoolFile(string_pool_file_, index_file_path)) {
            std::cout << "字符串池索引文件已创建: " << index_file_path << std::endl;
        } else {
            std::cerr << "无法创建字符串池索引文件: " << index_file_path << std::endl;
        }
    }

    bool AttributeStorage::BuildIndexFromPoolFile(const std::string& pool_file_path, const std::string& index_file_path) {
        try {
            // 打开 .pool 文件
            std::ifstream pool_file(pool_file_path, std::ios::binary);
            if (!pool_file.is_open()) {
                std::cerr << "无法打开字符串池文件: " << pool_file_path << std::endl;
                return false;
            }

            // 获取文件大小
            pool_file.seekg(0, std::ios::end);
            size_t file_size = pool_file.tellg();
            pool_file.seekg(0, std::ios::beg);

            if (file_size < 1) {
                std::cerr << "字符串池文件太小: " << pool_file_path << std::endl;
                return false;
            }

            // 读取整个文件到内存
            std::vector<uint8_t> pool_data(file_size);
            pool_file.read(reinterpret_cast<char*>(pool_data.data()), file_size);
            pool_file.close();

            // 解析字符串数量
            size_t offset = 0;
            uint32_t string_count = 0;

            // 解码变长编码的字符串数量
            while (offset < pool_data.size()) {
                uint8_t byte = pool_data[offset++];
                string_count |= (byte & 0x7F) << (7 * (offset - 1));
                if ((byte & 0x80) == 0)
                    break;
            }

            std::cout << "字符串池包含 " << string_count << " 个字符串，开始构建索引..." << std::endl;

            // 构建偏移量索引
            std::vector<size_t> string_offsets;
            string_offsets.reserve(string_count);

            // 为每个字符串计算偏移量
            for (uint32_t i = 0; i < string_count; ++i) {
                if (offset >= pool_data.size()) {
                    std::cerr << "字符串池数据损坏，偏移超出范围 (i=" << i << ")" << std::endl;
                    return false;
                }

                // 记录当前字符串的偏移量
                string_offsets.push_back(offset);

                // 读取字符串长度 (变长编码)
                uint32_t length = 0;
                size_t length_offset = offset;
                while (length_offset < pool_data.size()) {
                    uint8_t byte = pool_data[length_offset++];
                    length |= (byte & 0x7F) << (7 * (length_offset - offset - 1));
                    if ((byte & 0x80) == 0)
                        break;
                }

                // 跳过字符串内容
                offset = length_offset + length;

                // 显示进度
                if (i % 100000 == 0 && i > 0) {
                    std::cout << "\r已处理 " << i << " 个字符串..." << std::flush;
                }
            }

            // 保存索引到文件
            std::ofstream index_file(index_file_path, std::ios::binary);
            if (!index_file.is_open()) {
                std::cerr << "无法创建索引文件: " << index_file_path << std::endl;
                return false;
            }

            // 写入字符串数量
            index_file.write(reinterpret_cast<const char*>(&string_count), sizeof(uint32_t));

            // 写入所有偏移量
            index_file.write(reinterpret_cast<const char*>(string_offsets.data()), string_offsets.size() * sizeof(size_t));

            index_file.close();

            std::cout << "\n索引构建完成，共 " << string_offsets.size() << " 个字符串" << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "构建索引时出错: " << e.what() << std::endl;
            return false;
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

            auto start_time = std::chrono::high_resolution_clock::now();

            // 根据配置决定是否使用mmap方式加载字符串池
            if (use_mmap_mode_ && serializer_.LoadStringPoolFromFile(string_pool_file_)) {
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                auto stats = serializer_.GetCompressionStats();
                auto cache_stats = serializer_.GetStringPool().GetCacheStats();

                std::cout << "字符串池已使用mmap加载: " << stats.unique_strings << " 个唯一字符串" << " (耗时: " << duration.count() << "ms)" << std::endl;
                std::cout << "缓存配置: 最大容量=" << cache_stats.cache_size << " 个字符串" << std::endl;

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

            // 读取属性数量 (变长编码，需要先读取1字节判断长度)
            uint8_t first_byte;
            if (!file.read(reinterpret_cast<char*>(&first_byte), 1)) {
                break;
            }

            // 计算变长编码的属性数量需要多少字节
            size_t prop_count_bytes = 1;
            if (first_byte & 0x80) {
                // 需要更多字节，简单估算最大可能的大小
                prop_count_bytes = 4; // 最多4字节
            }

            // 读取属性数量
            std::vector<uint8_t> prop_count_data(prop_count_bytes);
            prop_count_data[0] = first_byte;
            if (prop_count_bytes > 1) {
                if (!file.read(reinterpret_cast<char*>(&prop_count_data[1]), prop_count_bytes - 1)) {
                    break;
                }
            }

            // 解码属性数量
            size_t temp_offset = 0;
            uint32_t prop_count_decoded;
            temp_offset = DecodeVarint(prop_count_data, temp_offset, prop_count_decoded);

            // 逐个读取属性对，精确跳过（变长编码的key_id和value_id）
            for (uint32_t i = 0; i < prop_count_decoded; ++i) {
                // 读取key_id (变长编码)
                uint8_t byte;
                do {
                    if (!file.read(reinterpret_cast<char*>(&byte), 1)) {
                        break;
                    }
                } while (byte & 0x80);

                // 读取value_id (变长编码)
                do {
                    if (!file.read(reinterpret_cast<char*>(&byte), 1)) {
                        break;
                    }
                } while (byte & 0x80);
            }

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
            // 紧凑格式：使用变长编码优化索引存储
            std::vector<uint8_t> index_data;

            // 写入文件头 (紧凑格式)
            uint32_t version = 2; // 新版本号，表示紧凑格式
            EncodeVarint(index_data, version);
            EncodeVarint(index_data, static_cast<uint32_t>(index_chunks_.size()));
            EncodeVarint(index_data, static_cast<uint32_t>(chunk_size_));

            // 写入每个索引块
            for (const auto& chunk : index_chunks_) {
                // 写入块头 (紧凑格式)
                EncodeVarint(index_data, static_cast<uint32_t>(chunk.start_fid));
                EncodeVarint(index_data, static_cast<uint32_t>(chunk.end_fid));
                EncodeVarint(index_data, static_cast<uint32_t>(chunk.offset_map.size()));

                // 写入偏移映射 (紧凑格式)
                for (const auto& [fid, offset] : chunk.offset_map) {
                    EncodeVarint(index_data, static_cast<uint32_t>(fid));
                    EncodeVarint(index_data, static_cast<uint32_t>(offset));
                }
            }

            // 一次性写入所有数据
            file.write(reinterpret_cast<const char*>(index_data.data()), index_data.size());

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
            // 读取整个文件到内存
            file.seekg(0, std::ios::end);
            size_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<uint8_t> index_data(file_size);
            file.read(reinterpret_cast<char*>(index_data.data()), file_size);

            size_t offset = 0;

            // 读取文件头 (紧凑格式)
            uint32_t version;
            offset = DecodeVarint(index_data, offset, version);

            // 检查版本号
            if (version != 2) {
                std::cout << "不支持的属性分块索引版本: " << version << std::endl;
                return;
            }

            uint32_t chunk_count;
            offset = DecodeVarint(index_data, offset, chunk_count);

            uint32_t chunk_size;
            offset = DecodeVarint(index_data, offset, chunk_size);

            std::cout << "加载属性分块索引: 版本=" << version << ", 块数=" << chunk_count << ", 块大小=" << chunk_size << std::endl;

            // 设置块大小
            chunk_size_ = chunk_size;

            // 清空现有索引
            index_chunks_.clear();
            index_chunks_.reserve(chunk_count);

            // 读取每个索引块
            for (uint32_t i = 0; i < chunk_count; ++i) {
                IndexChunk chunk;

                // 读取块头 (紧凑格式)
                uint32_t start_fid, end_fid, entry_count;
                offset = DecodeVarint(index_data, offset, start_fid);
                offset = DecodeVarint(index_data, offset, end_fid);
                offset = DecodeVarint(index_data, offset, entry_count);

                chunk.start_fid = start_fid;
                chunk.end_fid = end_fid;
                chunk.loaded = true; // 从文件加载的块直接标记为已加载

                // 读取偏移映射 (紧凑格式)
                chunk.offset_map.reserve(entry_count);
                for (uint32_t j = 0; j < entry_count; ++j) {
                    uint32_t feature_id, file_offset;
                    offset = DecodeVarint(index_data, offset, feature_id);
                    offset = DecodeVarint(index_data, offset, file_offset);

                    chunk.offset_map[feature_id] = static_cast<int64_t>(file_offset);
                }

                index_chunks_.push_back(std::move(chunk));
            }

            use_chunked_mode_ = true;
            std::cout << "属性分块索引加载完成，共 " << index_chunks_.size() << " 个块" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "加载属性分块索引文件失败: " << e.what() << std::endl;
        }
    }

    void AttributeStorage::EncodeVarint(std::vector<uint8_t>& data, uint32_t value) const {
        // 使用变长编码，每个字节的最高位表示是否还有后续字节
        while (value >= 0x80) {
            data.push_back(static_cast<uint8_t>(value | 0x80));
            value >>= 7;
        }
        data.push_back(static_cast<uint8_t>(value));
    }

    size_t AttributeStorage::DecodeVarint(const std::vector<uint8_t>& data, size_t offset, uint32_t& value) const {
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
