//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef GEOMETRY_STORAGE_H
#define GEOMETRY_STORAGE_H

#include "geometry_data.h"

#include <string>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace GisStorage {

    // 几何数据存储类 - 支持分块索引和按需加载
    class GeometryStorage {
      public:
        explicit GeometryStorage(const std::string& geometry_file);
        ~GeometryStorage();

        // 写入几何数据
        int64_t WriteGeometry(const GeometryData& geometry);

        // 读取几何数据 - 优化版本，支持按需加载
        std::unique_ptr<GeometryData> ReadGeometry(uint64_t feature_id);

        // 按需读取几何数据（不依赖完整索引）
        std::unique_ptr<GeometryData> ReadGeometryOnDemand(uint64_t feature_id);

        // 根据偏移读取几何数据
        std::unique_ptr<GeometryData> ReadGeometryAtOffset(uint64_t feature_id, int64_t offset);

        // 检查要素是否存在
        bool HasFeature(uint64_t feature_id);

        // 清除缓存
        void ClearCache();

        // 获取文件路径
        std::string GetGeometryFilePath() const { return geometry_file_; }

        // 设置分块大小
        void SetChunkSize(size_t chunk_size) { chunk_size_ = chunk_size; }

        // 设置缓存大小
        void SetCacheSize(size_t cache_size) { max_cache_size_ = cache_size; }

        // 检查是否启用分块模式
        bool IsChunkedMode() const { return use_chunked_mode_; }

        // 获取所有FID
        std::vector<uint64_t> GetAllFeatureIds() const;

        // 构建分块索引
        void BuildChunkedIndex();

        // 保存分块索引到文件
        void SaveChunkedIndex(const std::string& index_file);

        void SaveChunkedIndex() { SaveChunkedIndex(index_file_); }

        // 从文件加载分块索引
        void LoadChunkedIndex(const std::string& index_file);

      public:
        // 索引块结构
        struct IndexChunk {
            uint64_t start_fid;
            uint64_t end_fid;
            std::unordered_map<uint64_t, int64_t> offset_map;
            bool loaded = false;

            IndexChunk() = default;
            IndexChunk(const IndexChunk&) = default;
            IndexChunk& operator=(const IndexChunk&) = default;
            IndexChunk(IndexChunk&&) = default;
            IndexChunk& operator=(IndexChunk&&) = default;
        };

      private:
        std::string geometry_file_;
        std::string index_file_;

        // 分块索引
        std::vector<IndexChunk> index_chunks_;
        // 进程内共享的只读索引视图
        std::shared_ptr<const std::vector<IndexChunk>> shared_index_chunks_;
        bool use_chunked_mode_ = false;
        size_t chunk_size_ = 10000; // 默认每个块10000个要素

        // 内存映射
        int geom_fd_ = -1;
        char* geom_mmap_ = nullptr;
        size_t geom_size_ = 0;
        bool use_mmap_mode_ = false;

        // 简单缓存 - 只缓存最近访问的数据
        mutable std::unordered_map<uint64_t, std::unique_ptr<GeometryData>> cache_;
        mutable size_t max_cache_size_ = 1000; // 默认缓存1000个几何

        mutable std::mutex mutex_;

        // 获取分块偏移
        int64_t GetChunkedOffset(uint64_t feature_id) const;

        // 加载索引块
        void LoadIndexChunk(IndexChunk* chunk) const;

        // 更新LRU缓存
        void UpdateCache(uint64_t fid, std::unique_ptr<GeometryData> data) const;

        // 清理内存映射
        void CleanupMmap();
    };

} // namespace GisStorage

#endif // GEOMETRY_STORAGE_H
