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

    // GeometryStorage 实现
    GeometryStorage::GeometryStorage(const std::string& geometry_file)
        : geometry_file_(geometry_file) {
        std::filesystem::path file_path(geometry_file);
        std::filesystem::create_directories(file_path.parent_path());
    }

    GeometryStorage::~GeometryStorage() {
        CleanupMmap();
    }

    int64_t GeometryStorage::WriteGeometry(const GeometryData& geometry) {
        // 使用追加模式，如果文件不存在则创建新文件
        std::ofstream file(geometry_file_, std::ios::binary | std::ios::app);
        if (!file) {
            throw std::runtime_error("无法打开几何文件进行写入: " + geometry_file_);
        }

        int64_t offset = file.tellp();
        std::vector<uint8_t> geom_binary = GeometrySerializer::SerializeGeometry(geometry);

        if (!geom_binary.empty()) {
            file.write(reinterpret_cast<const char*>(geom_binary.data()), geom_binary.size());
            file.flush(); // 确保数据写入磁盘
            // 清除缓存
            cache_.clear();
            return offset;
        } else {
            throw std::runtime_error("几何数据序列化失败 for FID " + std::to_string(geometry.GetFeatureId()));
        }
    }

    std::unique_ptr<GeometryData> GeometryStorage::ReadGeometry(uint64_t feature_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. 先检查缓存
        auto cache_it = cache_.find(feature_id);
        if (cache_it != cache_.end()) {
            return std::make_unique<GeometryData>(*cache_it->second);
        }

        // 2. 使用分块索引获取偏移量
        int64_t offset = -1;
        if (use_chunked_mode_) {
            offset = GetChunkedOffset(feature_id);
        } else {
            // 如果没有分块索引，自动构建
            BuildChunkedIndex();
            if (use_chunked_mode_) {
                offset = GetChunkedOffset(feature_id);
            }
        }

        if (offset < 0) {
            return nullptr; // 要素不存在
        }

        // 3. 读取几何数据
        std::unique_ptr<GeometryData> result = ReadGeometryAtOffset(feature_id, offset);

        // 4. 更新缓存
        if (result) {
            UpdateCache(feature_id, std::make_unique<GeometryData>(*result));
        }

        return result;
    }

    std::unique_ptr<GeometryData> GeometryStorage::ReadGeometryOnDemand(uint64_t feature_id) {
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
        return ReadGeometryAtOffset(feature_id, offset);
    }

    std::unique_ptr<GeometryData> GeometryStorage::ReadGeometryAtOffset(uint64_t feature_id, int64_t offset) {
        std::ifstream file(geometry_file_, std::ios::binary);
        if (!file) {
            return nullptr;
        }

        file.seekg(offset);

        // 读取feature_id
        uint64_t stored_fid;
        if (!file.read(reinterpret_cast<char*>(&stored_fid), sizeof(uint64_t))) {
            return nullptr;
        }

        file.seekg(offset);

        // 先读取头部数据以确定需要读取的总大小（新格式：48字节头部 + 4字节环数量）
        std::vector<uint8_t> header_data(52);
        if (!file.read(reinterpret_cast<char*>(header_data.data()), 52)) {
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

        return GeometrySerializer::DeserializeGeometry(geom_data);
    }

    bool GeometryStorage::HasFeature(uint64_t feature_id) {
        // 流式读取模式：通过尝试读取来判断要素是否存在
        return ReadGeometryOnDemand(feature_id) != nullptr;
    }

    void GeometryStorage::ClearCache() {
        cache_.clear();
    }

    void GeometryStorage::BuildChunkedIndex() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (use_chunked_mode_) {
            return; // 已经构建过
        }

        // 尝试从文件加载分块索引
        std::string index_file = geometry_file_ + ".chunked_idx";
        if (std::filesystem::exists(index_file)) {
            std::cout << "发现几何分块索引文件，正在加载..." << std::endl;
            LoadChunkedIndex(index_file);
            if (use_chunked_mode_) {
                return; // 成功加载，直接返回
            }
        }

        // 如果加载失败或文件不存在，重新构建
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
        current_chunk.loaded = true; // 构建时直接加载

        while (file.good()) {
            int64_t current_offset = file.tellg();

            // 读取feature_id
            uint64_t feature_id;
            if (!file.read(reinterpret_cast<char*>(&feature_id), sizeof(uint64_t))) {
                break;
            }

            // 手动解析记录结构（新格式：增加了4字节环数量字段）
            try {
                // 跳过geometry_type(1B) + 7字节填充 + bbox(32B) + num_rings(4B) = 44字节
                file.seekg(44, std::ios::cur);

                // 读取坐标大小(4B)
                uint32_t coord_size;
                file.read(reinterpret_cast<char*>(&coord_size), sizeof(uint32_t));
                if (static_cast<size_t>(file.gcount()) < sizeof(uint32_t)) {
                    break;
                }

                // 跳过坐标数据
                file.seekg(coord_size, std::ios::cur);

            } catch (const std::exception& e) {
                std::cerr << "解析几何记录失败 at FID " << feature_id << ": " << e.what() << std::endl;
                break;
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
        std::cout << "构建了 " << index_chunks_.size() << " 个几何索引块" << std::endl;

        // 打印调试信息
        for (size_t i = 0; i < index_chunks_.size() && i < 3; ++i) {
            const auto& chunk = index_chunks_[i];
            std::cout << "块 " << i << ": FID范围 [" << chunk.start_fid << "-" << chunk.end_fid << "], 条目数: " << chunk.offset_map.size() << std::endl;
        }

        // 构建完成后保存到文件
        SaveChunkedIndex(index_file);
    }

    int64_t GeometryStorage::GetChunkedOffset(uint64_t feature_id) const {
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

    void GeometryStorage::LoadIndexChunk(IndexChunk* chunk) const {
        if (chunk->loaded) {
            return;
        }

        chunk->loaded = true;
    }

    void GeometryStorage::UpdateCache(uint64_t fid, std::unique_ptr<GeometryData> data) const {
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

        std::ifstream file(index_file, std::ios::binary);
        if (!file) {
            std::cout << "无法打开几何分块索引文件: " << index_file << std::endl;
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
                std::cout << "无法读取几何分块索引文件头" << std::endl;
                return;
            }

            // 检查版本号
            if (header.version != 1) {
                std::cout << "不支持的几何分块索引版本: " << header.version << std::endl;
                return;
            }

            std::cout << "加载几何分块索引: 版本=" << header.version << ", 块数=" << header.chunk_count << ", 块大小=" << header.chunk_size << std::endl;

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
                    std::cout << "读取几何索引块 " << i << " 头失败" << std::endl;
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
                        std::cout << "读取几何索引条目 " << j << " 失败" << std::endl;
                        break;
                    }

                    chunk.offset_map[entry.feature_id] = entry.offset;
                }

                index_chunks_.push_back(std::move(chunk));
            }

            use_chunked_mode_ = true;
            std::cout << "几何分块索引加载完成，共 " << index_chunks_.size() << " 个块" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "加载几何分块索引文件失败: " << e.what() << std::endl;
        }
    }

} // namespace GisStorage
