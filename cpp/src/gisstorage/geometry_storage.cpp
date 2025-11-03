//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/geometry_storage.h"
#include "gisstorage/geometry_serializer.h"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace GisStorage {
    namespace {
        std::mutex g_geom_index_cache_mutex;
        std::unordered_map<std::string, std::weak_ptr<const std::vector<GeometryStorage::IndexChunk>>> g_geom_index_cache;
    } // namespace

    GeometryStorage::GeometryStorage(const std::string& geometry_file)
        : geometry_file_(geometry_file) {
        index_file_ = geometry_file_ + ".chunked_idx";
        std::filesystem::path file_path(geometry_file);
        std::filesystem::create_directories(file_path.parent_path());
    }

    GeometryStorage::~GeometryStorage() {
        CleanupMmap();
    }

    int64_t GeometryStorage::WriteGeometry(const GeometryData& geometry) {
        std::ofstream file(geometry_file_, std::ios::binary | std::ios::app);
        if (!file) {
            throw std::runtime_error("无法打开几何文件进行写入: " + geometry_file_);
        }

        int64_t offset = file.tellp();
        std::vector<uint8_t> geom_binary = GeometrySerializer::SerializeGeometry(geometry);

        if (!geom_binary.empty()) {
            file.write(reinterpret_cast<const char*>(geom_binary.data()), geom_binary.size());
            file.flush();
            cache_.clear();
            return offset;
        } else {
            throw std::runtime_error("几何数据序列化失败 for FID " + std::to_string(geometry.GetFeatureId()));
        }
    }

    std::unique_ptr<GeometryData> GeometryStorage::ReadGeometry(uint64_t feature_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto cache_it = cache_.find(feature_id);
        if (cache_it != cache_.end()) {
            return std::make_unique<GeometryData>(*cache_it->second);
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

        std::unique_ptr<GeometryData> result = ReadGeometryAtOffset(feature_id, offset);
        if (result) {
            UpdateCache(feature_id, std::make_unique<GeometryData>(*result));
        }

        return result;
    }

    std::unique_ptr<GeometryData> GeometryStorage::ReadGeometryOnDemand(uint64_t feature_id) {
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

        return ReadGeometryAtOffset(feature_id, offset);
    }

    std::unique_ptr<GeometryData> GeometryStorage::ReadGeometryAtOffset(uint64_t feature_id, int64_t offset) {
        std::ifstream file(geometry_file_, std::ios::binary);
        if (!file) {
            return nullptr;
        }

        file.seekg(offset);
        uint64_t stored_fid;
        if (!file.read(reinterpret_cast<char*>(&stored_fid), sizeof(uint64_t))) {
            return nullptr;
        }

        file.seekg(offset);
        std::vector<uint8_t> header_data(52);
        if (!file.read(reinterpret_cast<char*>(header_data.data()), 52)) {
            throw std::runtime_error("几何数据不完整 for FID " + std::to_string(feature_id));
        }

        uint32_t coord_size = 0;
        if (!file.read(reinterpret_cast<char*>(&coord_size), sizeof(uint32_t))) {
            throw std::runtime_error("几何数据不完整 for FID " + std::to_string(feature_id));
        }
        std::vector<uint8_t> coord_data(coord_size);
        if (coord_size > 0) {
            if (!file.read(reinterpret_cast<char*>(coord_data.data()), coord_size)) {
                throw std::runtime_error("几何坐标数据不完整 for FID " + std::to_string(feature_id));
            }
        }

        std::vector<uint8_t> geom_data = header_data;
        geom_data.insert(geom_data.end(), reinterpret_cast<uint8_t*>(&coord_size), reinterpret_cast<uint8_t*>(&coord_size) + sizeof(uint32_t));
        geom_data.insert(geom_data.end(), coord_data.begin(), coord_data.end());

        return GeometrySerializer::DeserializeGeometry(geom_data);
    }

    bool GeometryStorage::HasFeature(uint64_t feature_id) {
        return ReadGeometryOnDemand(feature_id) != nullptr;
    }

    void GeometryStorage::ClearCache() {
        cache_.clear();
    }

    void GeometryStorage::BuildChunkedIndex() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (use_chunked_mode_) {
            return;
        }

        if (std::filesystem::exists(index_file_)) {
            std::cout << "发现几何分块索引文件，正在加载..." << std::endl;
            LoadChunkedIndex(index_file_);
            if (use_chunked_mode_) {
                return;
            }
        }

        std::cout << "重新构建几何分块索引..." << std::endl;
        std::ifstream file(geometry_file_, std::ios::binary);
        if (!file) {
            std::cout << "无法打开几何文件构建分块索引: " << geometry_file_ << std::endl;
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

            try {
                file.seekg(44, std::ios::cur);
                uint32_t coord_size;
                file.read(reinterpret_cast<char*>(&coord_size), sizeof(uint32_t));
                if (static_cast<size_t>(file.gcount()) < sizeof(uint32_t)) {
                    break;
                }

                file.seekg(coord_size, std::ios::cur);

            } catch (const std::exception& e) {
                std::cerr << "解析几何记录失败 at FID " << feature_id << ": " << e.what() << std::endl;
                break;
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
        std::cout << "构建了 " << index_chunks_.size() << " 个几何索引块" << std::endl;
        for (size_t i = 0; i < index_chunks_.size() && i < 3; ++i) {
            const auto& chunk = index_chunks_[i];
            std::cout << "块 " << i << ": FID范围 [" << chunk.start_fid << "-" << chunk.end_fid << "], 条目数: " << chunk.offset_map.size() << std::endl;
        }
    }

    int64_t GeometryStorage::GetChunkedOffset(uint64_t feature_id) const {
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

    void GeometryStorage::LoadIndexChunk(IndexChunk* chunk) const {
        if (chunk->loaded) {
            return;
        }

        chunk->loaded = true;
    }

    void GeometryStorage::UpdateCache(uint64_t fid, std::unique_ptr<GeometryData> data) const {
        if (cache_.size() >= max_cache_size_) {
            auto it = cache_.begin();
            for (size_t i = 0; i < max_cache_size_ / 2 && it != cache_.end(); ++i) {
                it = cache_.erase(it);
            }
        }

        cache_[fid] = std::move(data);
    }

    void GeometryStorage::CleanupMmap() {
        if (geom_mmap_ && geom_mmap_ != MAP_FAILED) {
            munmap(geom_mmap_, geom_size_);
            geom_mmap_ = nullptr;
        }
        if (geom_fd_ >= 0) {
            close(geom_fd_);
            geom_fd_ = -1;
        }
        geom_size_ = 0;
    }

    void GeometryStorage::SaveChunkedIndex(const std::string& index_file) {
        if (!use_chunked_mode_ || index_chunks_.empty()) {
            std::cout << "没有分块索引需要保存" << std::endl;
            return;
        }

        std::ofstream file(index_file, std::ios::binary);
        if (!file) {
            std::cout << "无法创建分块索引文件: " << index_file << std::endl;
            return;
        }

        try {
            struct ChunkedIndexHeader {
                uint32_t version = 1;
                uint32_t chunk_count;
                uint64_t chunk_size;
                uint64_t reserved = 0;
            } header;

            header.chunk_count = static_cast<uint32_t>(index_chunks_.size());
            header.chunk_size = chunk_size_;
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));

            for (const auto& chunk : index_chunks_) {
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
            std::cout << "几何分块索引已保存到: " << index_file << " (块数: " << index_chunks_.size() << ")" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "保存几何分块索引文件失败: " << e.what() << std::endl;
        }
    }

    void GeometryStorage::LoadChunkedIndex(const std::string& index_file) {
        if (!std::filesystem::exists(index_file)) {
            std::cout << "几何分块索引文件不存在: " << index_file << std::endl;
            return;
        }

        static std::mutex s_geom_shared_storage_mutex;
        static std::unordered_map<std::string, std::shared_ptr<const std::vector<IndexChunk>>> s_geom_shared_storage;
        {
            std::lock_guard<std::mutex> storage_lk(s_geom_shared_storage_mutex);
            auto storage_it = s_geom_shared_storage.find(index_file);
            if (storage_it != s_geom_shared_storage.end()) {
                shared_index_chunks_ = storage_it->second;
                use_chunked_mode_ = true;
                {
                    std::lock_guard<std::mutex> lk(g_geom_index_cache_mutex);
                    g_geom_index_cache[index_file] = storage_it->second;
                }

                return;
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_geom_index_cache_mutex);
            auto it = g_geom_index_cache.find(index_file);
            if (it != g_geom_index_cache.end()) {
                auto shared = it->second.lock();
                if (shared) {
                    shared_index_chunks_ = shared;
                    use_chunked_mode_ = true;
                    {
                        std::lock_guard<std::mutex> storage_lk(s_geom_shared_storage_mutex);
                        s_geom_shared_storage[index_file] = shared;
                    }
                    return;
                }
            }
        }

        std::cout << "发现几何分块索引文件，正在加载..." << std::endl;
        std::ifstream file(index_file, std::ios::binary);
        if (!file) {
            std::cout << "无法打开几何分块索引文件: " << index_file << std::endl;
            return;
        }

        try {
            struct ChunkedIndexHeader {
                uint32_t version;
                uint32_t chunk_count;
                uint64_t chunk_size;
                uint64_t reserved;
            } header;

            if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
                std::cout << "无法读取几何分块索引文件头" << std::endl;
                return;
            }

            if (header.version != 1) {
                std::cout << "不支持的几何分块索引版本: " << header.version << std::endl;
                return;
            }

            std::cout << "加载几何分块索引: 版本=" << header.version << ", 块数=" << header.chunk_count << ", 块大小=" << header.chunk_size << std::endl;
            chunk_size_ = header.chunk_size;
            std::vector<IndexChunk> temp_chunks;
            temp_chunks.reserve(header.chunk_count);

            for (uint32_t i = 0; i < header.chunk_count; ++i) {
                IndexChunk chunk;
                struct ChunkHeader {
                    uint64_t start_fid;
                    uint64_t end_fid;
                    uint32_t entry_count;
                    uint32_t reserved;
                } chunk_header;

                if (!file.read(reinterpret_cast<char*>(&chunk_header), sizeof(chunk_header))) {
                    std::cout << "读取几何索引块 " << i << " 头失败" << std::endl;
                    break;
                }

                chunk.start_fid = chunk_header.start_fid;
                chunk.end_fid = chunk_header.end_fid;
                chunk.loaded = true;
                chunk.offset_map.reserve(chunk_header.entry_count);
                for (uint32_t j = 0; j < chunk_header.entry_count; ++j) {
                    struct IndexEntry {
                        uint64_t feature_id;
                        int64_t offset;
                    } entry;

                    if (!file.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
                        std::cout << "读取几何索引条目 " << j << " 失败" << std::endl;
                        break;
                    }

                    chunk.offset_map[entry.feature_id] = entry.offset;
                }

                temp_chunks.push_back(std::move(chunk));
            }

            auto shared = std::make_shared<const std::vector<IndexChunk>>(std::move(temp_chunks));
            {
                std::lock_guard<std::mutex> storage_lk(s_geom_shared_storage_mutex);
                s_geom_shared_storage[index_file] = shared;
            }
            {
                std::lock_guard<std::mutex> lk(g_geom_index_cache_mutex);
                g_geom_index_cache[index_file] = shared;
            }
            shared_index_chunks_ = shared;
            use_chunked_mode_ = true;
            std::cout << "几何分块索引加载完成(共享): 共 " << shared_index_chunks_->size() << " 个块" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "加载几何分块索引文件失败: " << e.what() << std::endl;
        }
    }

} // namespace GisStorage
