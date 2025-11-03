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
    namespace {
        std::mutex g_attr_index_cache_mutex;
        std::unordered_map<std::string, std::weak_ptr<const std::vector<AttributeStorage::IndexChunk>>> g_attr_index_cache;
    } // namespace

    AttributeStorage::AttributeStorage(const std::string& attribute_file, const std::string& string_pool_file)
        : attribute_file_(attribute_file)
        , string_pool_file_(string_pool_file) {
        std::filesystem::path file_path(attribute_file);
        std::filesystem::create_directories(file_path.parent_path());
        index_file_ = attribute_file_ + ".chunked_idx";
    }

    AttributeStorage::~AttributeStorage() {
        CleanupMmap();
    }

    int64_t AttributeStorage::WriteAttribute(const AttributeData& attribute) {
        std::ofstream file(attribute_file_, std::ios::binary | std::ios::app);
        if (!file) {
            throw std::runtime_error("无法打开属性文件进行写入: " + attribute_file_);
        }

        int64_t offset = file.tellp();
        std::vector<uint8_t> attr_binary = serializer_.SerializeAttributes(attribute);

        if (!attr_binary.empty()) {
            file.write(reinterpret_cast<const char*>(attr_binary.data()), attr_binary.size());
            file.flush();
            cache_.clear();
            return offset;
        } else {
            throw std::runtime_error("属性数据序列化失败 for FID " + std::to_string(attribute.GetFeatureId()));
        }
    }

    std::unique_ptr<AttributeData> AttributeStorage::ReadAttribute(uint64_t feature_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto cache_it = cache_.find(feature_id);
        if (cache_it != cache_.end()) {
            return std::make_unique<AttributeData>(*cache_it->second);
        }

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
            return nullptr;
        }

        std::unique_ptr<AttributeData> result = ReadAttributeAtOffset(feature_id, offset);

        if (result) {
            UpdateCache(feature_id, std::make_unique<AttributeData>(*result));
        }

        return result;
    }

    std::unique_ptr<AttributeData> AttributeStorage::ReadAttributeOnDemand(uint64_t feature_id) {
        if (serializer_.GetPoolSize() == 0) {
            LoadStringPool();
        }

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
            return nullptr;
        }

        return ReadAttributeAtOffset(feature_id, offset);
    }

    std::vector<std::unique_ptr<AttributeData>> AttributeStorage::ReadAttributesBatch(const std::vector<uint64_t>& feature_ids) {
        std::vector<std::unique_ptr<AttributeData>> results;
        results.reserve(feature_ids.size());

        if (serializer_.GetPoolSize() == 0) {
            LoadStringPool();
        }

        if (attr_fd_ == -1) {
            attr_fd_ = open(attribute_file_.c_str(), O_RDONLY);
            if (attr_fd_ == -1) {
                return results;
            }
        }

        for (uint64_t feature_id : feature_ids) {
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

            auto attr_data = ReadAttributeAtOffset(feature_id, offset);
            results.push_back(std::move(attr_data));
        }

        return results;
    }

    std::unique_ptr<AttributeData> AttributeStorage::ReadAttributeAtOffset(uint64_t feature_id, int64_t offset) {
        if (serializer_.GetPoolSize() == 0) {
            LoadStringPool();
        }

        if (attr_fd_ == -1) {
            attr_fd_ = open(attribute_file_.c_str(), O_RDONLY);
            if (attr_fd_ == -1) {
                return nullptr;
            }
        }

        uint64_t stored_fid;
        if (pread(attr_fd_, &stored_fid, sizeof(uint64_t), offset) != sizeof(uint64_t)) {
            return nullptr;
        }

        uint8_t first_byte;
        if (pread(attr_fd_, &first_byte, 1, offset + sizeof(uint64_t)) != 1) {
            return nullptr;
        }

        size_t prop_count_bytes = 1;
        if (first_byte & 0x80) {
            prop_count_bytes = 4;
        }

        std::vector<uint8_t> prop_count_data(prop_count_bytes);
        if (pread(attr_fd_, prop_count_data.data(), prop_count_bytes, offset + sizeof(uint64_t)) != static_cast<ssize_t>(prop_count_bytes)) {
            return nullptr;
        }

        size_t temp_offset = 0;
        uint32_t prop_count_decoded;
        temp_offset = serializer_.DecodeVarint(prop_count_data, temp_offset, prop_count_decoded);
        size_t data_size = sizeof(uint64_t) + prop_count_bytes + prop_count_decoded * 8;

        std::vector<uint8_t> data(data_size);
        if (pread(attr_fd_, data.data(), data_size, offset) != static_cast<ssize_t>(data_size)) {
            throw std::runtime_error("属性数据不完整 for FID " + std::to_string(feature_id));
        }

        return serializer_.DeserializeAttributes(data);
    }

    bool AttributeStorage::HasFeature(uint64_t feature_id) {
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
            std::cout << "字符串池已保存到: " << string_pool_file_ << std::endl;
        } else {
            std::cerr << "无法保存字符串池到: " << string_pool_file_ << std::endl;
        }
    }

    void AttributeStorage::CreateStringPoolIndex() {
        std::string index_file_path = string_pool_file_ + ".index";

        if (std::filesystem::exists(index_file_path)) {
            std::cout << "字符串池索引文件已存在，跳过创建: " << index_file_path << std::endl;
            return;
        }

        if (!std::filesystem::exists(string_pool_file_)) {
            std::cerr << "字符串池文件不存在，无法创建索引: " << string_pool_file_ << std::endl;
            return;
        }

        std::cout << "开始创建字符串池索引文件..." << std::endl;
        if (BuildIndexFromPoolFile(string_pool_file_, index_file_path)) {
            std::cout << "字符串池索引文件已创建: " << index_file_path << std::endl;
        } else {
            std::cerr << "无法创建字符串池索引文件: " << index_file_path << std::endl;
        }
    }

    bool AttributeStorage::BuildIndexFromPoolFile(const std::string& pool_file_path, const std::string& index_file_path) {
        try {
            std::ifstream pool_file(pool_file_path, std::ios::binary);
            if (!pool_file.is_open()) {
                std::cerr << "无法打开字符串池文件: " << pool_file_path << std::endl;
                return false;
            }

            pool_file.seekg(0, std::ios::end);
            size_t file_size = pool_file.tellg();
            pool_file.seekg(0, std::ios::beg);

            if (file_size < 1) {
                std::cerr << "字符串池文件太小: " << pool_file_path << std::endl;
                return false;
            }

            std::vector<uint8_t> pool_data(file_size);
            pool_file.read(reinterpret_cast<char*>(pool_data.data()), file_size);
            pool_file.close();

            size_t offset = 0;
            uint32_t string_count = 0;
            while (offset < pool_data.size()) {
                uint8_t byte = pool_data[offset++];
                string_count |= (byte & 0x7F) << (7 * (offset - 1));
                if ((byte & 0x80) == 0)
                    break;
            }

            std::cout << "字符串池包含 " << string_count << " 个字符串，开始构建索引..." << std::endl;
            std::vector<size_t> string_offsets;
            string_offsets.reserve(string_count);
            for (uint32_t i = 0; i < string_count; ++i) {
                if (offset >= pool_data.size()) {
                    std::cerr << "字符串池数据损坏，偏移超出范围 (i=" << i << ")" << std::endl;
                    return false;
                }

                string_offsets.push_back(offset);
                uint32_t length = 0;
                size_t length_offset = offset;
                while (length_offset < pool_data.size()) {
                    uint8_t byte = pool_data[length_offset++];
                    length |= (byte & 0x7F) << (7 * (length_offset - offset - 1));
                    if ((byte & 0x80) == 0)
                        break;
                }

                offset = length_offset + length;
                if (i % 100000 == 0 && i > 0) {
                    std::cout << "\r已处理 " << i << " 个字符串..." << std::flush;
                }
            }

            std::ofstream index_file(index_file_path, std::ios::binary);
            if (!index_file.is_open()) {
                std::cerr << "无法创建索引文件: " << index_file_path << std::endl;
                return false;
            }

            index_file.write(reinterpret_cast<const char*>(&string_count), sizeof(uint32_t));
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
        static std::mutex s_pool_once_mutex;
        static std::unordered_map<std::string, std::unique_ptr<std::once_flag>> s_path_once;

        {
            std::lock_guard<std::mutex> g(s_pool_once_mutex);
            if (s_path_once.find(string_pool_file_) == s_path_once.end()) {
                s_path_once[string_pool_file_] = std::make_unique<std::once_flag>();
            }
        }

        std::once_flag& once = *s_path_once[string_pool_file_];
        std::call_once(once, [this]() {
            if (!std::filesystem::exists(string_pool_file_)) {
                std::cout << "字符串池文件不存在: " << string_pool_file_ << std::endl;
                return;
            }

            if (!use_mmap_mode_) {
                return;
            }

            const std::string index_file_path = string_pool_file_ + ".index";
            if (!std::filesystem::exists(index_file_path)) {
                const std::string lock_file_path = index_file_path + ".lock";
                int lock_fd = ::open(lock_file_path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0644);
                if (lock_fd >= 0) {
                    if (!serializer_.LoadStringPoolFromFile(string_pool_file_)) {
                        std::cout << "mmap加载失败（用于索引构建），稍后将回退传统方式" << std::endl;
                    }
                    ::close(lock_fd);
                    ::unlink(lock_file_path.c_str());
                }
            }
        });

        if (serializer_.GetPoolSize() == 0) {
            if (use_mmap_mode_ && serializer_.LoadStringPoolFromFile(string_pool_file_)) {
                return;
            }

            std::cout << "mmap加载失败，回退到传统方式加载字符串池..." << std::endl;
            std::ifstream file(string_pool_file_, std::ios::binary);
            if (!file.is_open()) {
                std::cout << "无法打开字符串池文件: " << string_pool_file_ << std::endl;
                return;
            }
            file.seekg(0, std::ios::end);
            size_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> pool_data(file_size);
            file.read(reinterpret_cast<char*>(pool_data.data()), file_size);
            file.close();
            serializer_.DeserializeStringPool(pool_data);
            auto stats = serializer_.GetCompressionStats();
            std::cout << "字符串池已使用传统方式加载: " << stats.unique_strings << " 个唯一字符串" << std::endl;
        }
    }

    AttributeSerializer::CompressionStats AttributeStorage::GetCompressionStats() const {
        return serializer_.GetCompressionStats();
    }

    AttributeStorage::StorageStats AttributeStorage::GetStorageStats() const {
        StorageStats stats;
        auto compression_stats = serializer_.GetCompressionStats();

        stats.total_features = 0;
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
            return;
        }

        if (std::filesystem::exists(index_file_)) {
            std::cout << "发现属性分块索引文件，正在加载..." << std::endl;
            LoadChunkedIndex(index_file_);
            if (use_chunked_mode_) {
                return;
            }
        }

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
        current_chunk.loaded = true;

        while (file.good()) {
            int64_t current_offset = file.tellg();

            uint64_t feature_id;
            if (!file.read(reinterpret_cast<char*>(&feature_id), sizeof(uint64_t))) {
                break;
            }

            uint8_t first_byte;
            if (!file.read(reinterpret_cast<char*>(&first_byte), 1)) {
                break;
            }

            size_t prop_count_bytes = 1;
            if (first_byte & 0x80) {
                prop_count_bytes = 4;
            }

            std::vector<uint8_t> prop_count_data(prop_count_bytes);
            prop_count_data[0] = first_byte;
            if (prop_count_bytes > 1) {
                if (!file.read(reinterpret_cast<char*>(&prop_count_data[1]), prop_count_bytes - 1)) {
                    break;
                }
            }

            size_t temp_offset = 0;
            uint32_t prop_count_decoded;
            temp_offset = DecodeVarint(prop_count_data, temp_offset, prop_count_decoded);

            for (uint32_t i = 0; i < prop_count_decoded; ++i) {
                uint8_t byte;
                do {
                    if (!file.read(reinterpret_cast<char*>(&byte), 1)) {
                        break;
                    }
                } while (byte & 0x80);

                do {
                    if (!file.read(reinterpret_cast<char*>(&byte), 1)) {
                        break;
                    }
                } while (byte & 0x80);
            }

            if (file.good()) {
                if (current_chunk.offset_map.size() >= chunk_size_) {
                    current_chunk.end_fid = feature_id - 1;
                    index_chunks_.push_back(current_chunk);

                    current_chunk.offset_map.clear();
                    current_chunk.start_fid = feature_id;
                    current_chunk.end_fid = feature_id;
                }

                current_chunk.offset_map[feature_id] = current_offset;
                current_chunk.end_fid = feature_id;
            }
        }

        if (!current_chunk.offset_map.empty()) {
            index_chunks_.push_back(current_chunk);
        }
        use_chunked_mode_ = true;

        std::cout << "构建了 " << index_chunks_.size() << " 个索引块" << std::endl;
        for (size_t i = 0; i < index_chunks_.size() && i < 3; ++i) {
            const auto& chunk = index_chunks_[i];
            std::cout << "属性块 " << i << ": FID范围 [" << chunk.start_fid << "-" << chunk.end_fid << "], 条目数: " << chunk.offset_map.size() << std::endl;
        }
    }

    int64_t AttributeStorage::GetChunkedOffset(uint64_t feature_id) const {
        if (shared_index_chunks_) {
            for (const auto& chunk : *shared_index_chunks_) {
                if (feature_id >= chunk.start_fid && feature_id <= chunk.end_fid) {
                    auto it = chunk.offset_map.find(feature_id);
                    if (it != chunk.offset_map.end()) {
                        return it->second;
                    }
                    break;
                }
            }
            return -1;
        }

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
        if (cache_.size() >= max_cache_size_) {
            auto it = cache_.begin();
            for (size_t i = 0; i < max_cache_size_ / 2 && it != cache_.end(); ++i) {
                it = cache_.erase(it);
            }
        }

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
            std::vector<uint8_t> index_data;
            uint32_t version = 2;
            EncodeVarint(index_data, version);
            EncodeVarint(index_data, static_cast<uint32_t>(index_chunks_.size()));
            EncodeVarint(index_data, static_cast<uint32_t>(chunk_size_));
            for (const auto& chunk : index_chunks_) {
                EncodeVarint(index_data, static_cast<uint32_t>(chunk.start_fid));
                EncodeVarint(index_data, static_cast<uint32_t>(chunk.end_fid));
                EncodeVarint(index_data, static_cast<uint32_t>(chunk.offset_map.size()));
                for (const auto& [fid, offset] : chunk.offset_map) {
                    EncodeVarint(index_data, static_cast<uint32_t>(fid));
                    EncodeVarint(index_data, static_cast<uint32_t>(offset));
                }
            }

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

        static std::mutex s_attr_shared_storage_mutex;
        static std::unordered_map<std::string, std::shared_ptr<const std::vector<IndexChunk>>> s_attr_shared_storage;
        {
            std::lock_guard<std::mutex> storage_lk(s_attr_shared_storage_mutex);
            auto storage_it = s_attr_shared_storage.find(index_file);
            if (storage_it != s_attr_shared_storage.end()) {
                shared_index_chunks_ = storage_it->second;
                use_chunked_mode_ = true;
                {
                    std::lock_guard<std::mutex> lk(g_attr_index_cache_mutex);
                    g_attr_index_cache[index_file] = storage_it->second;
                }
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_attr_index_cache_mutex);
            auto it = g_attr_index_cache.find(index_file);
            if (it != g_attr_index_cache.end()) {
                auto shared = it->second.lock();
                if (shared) {
                    shared_index_chunks_ = shared;
                    use_chunked_mode_ = true;
                    {
                        std::lock_guard<std::mutex> storage_lk(s_attr_shared_storage_mutex);
                        s_attr_shared_storage[index_file] = shared;
                    }
                    return;
                }
            }
        }

        std::cout << "发现属性分块索引文件，正在加载..." << std::endl;

        std::ifstream file(index_file, std::ios::binary);
        if (!file) {
            std::cout << "无法打开属性分块索引文件: " << index_file << std::endl;
            return;
        }

        try {
            file.seekg(0, std::ios::end);
            size_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> index_data(file_size);
            file.read(reinterpret_cast<char*>(index_data.data()), file_size);

            size_t offset = 0;
            uint32_t version;
            offset = DecodeVarint(index_data, offset, version);
            if (version != 2) {
                std::cout << "不支持的属性分块索引版本: " << version << std::endl;
                return;
            }

            uint32_t chunk_count;
            offset = DecodeVarint(index_data, offset, chunk_count);
            uint32_t chunk_size;
            offset = DecodeVarint(index_data, offset, chunk_size);

            std::cout << "加载属性分块索引: 版本=" << version << ", 块数=" << chunk_count << ", 块大小=" << chunk_size << std::endl;
            chunk_size_ = chunk_size;
            std::vector<IndexChunk> temp_chunks;
            temp_chunks.reserve(chunk_count);
            for (uint32_t i = 0; i < chunk_count; ++i) {
                IndexChunk chunk;
                uint32_t start_fid, end_fid, entry_count;
                offset = DecodeVarint(index_data, offset, start_fid);
                offset = DecodeVarint(index_data, offset, end_fid);
                offset = DecodeVarint(index_data, offset, entry_count);
                chunk.start_fid = start_fid;
                chunk.end_fid = end_fid;
                chunk.loaded = true;
                chunk.offset_map.reserve(entry_count);
                for (uint32_t j = 0; j < entry_count; ++j) {
                    uint32_t feature_id, file_offset;
                    offset = DecodeVarint(index_data, offset, feature_id);
                    offset = DecodeVarint(index_data, offset, file_offset);

                    chunk.offset_map[feature_id] = static_cast<int64_t>(file_offset);
                }

                temp_chunks.push_back(std::move(chunk));
            }

            auto shared = std::make_shared<const std::vector<IndexChunk>>(std::move(temp_chunks));
            {
                std::lock_guard<std::mutex> storage_lk(s_attr_shared_storage_mutex);
                s_attr_shared_storage[index_file] = shared;
            }
            {
                std::lock_guard<std::mutex> lk(g_attr_index_cache_mutex);
                g_attr_index_cache[index_file] = shared;
            }
            shared_index_chunks_ = shared;
            use_chunked_mode_ = true;
            std::cout << "属性分块索引加载完成(共享): 共 " << shared_index_chunks_->size() << " 个块" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "加载属性分块索引文件失败: " << e.what() << std::endl;
        }
    }

    void AttributeStorage::EncodeVarint(std::vector<uint8_t>& data, uint32_t value) const {
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
                break;
            }

            shift += 7;
            if (shift >= 32) {
                throw std::runtime_error("变长编码值过大");
            }
        }

        return offset;
    }

} // namespace GisStorage
