//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef ATTRIBUTE_STORAGE_H
#define ATTRIBUTE_STORAGE_H

#include "attribute_data.h"
#include "attribute_serializer.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace GisStorage {

    // 属性数据存储类 - 集成字符串池，支持分块索引和按需加载
    class AttributeStorage {
      public:
        explicit AttributeStorage(const std::string& attribute_file, const std::string& string_pool_file);
        ~AttributeStorage();

        // 写入属性数据
        int64_t WriteAttribute(const AttributeData& attribute);

        // 读取属性数据 - 优化版本，支持按需加载
        std::unique_ptr<AttributeData> ReadAttribute(uint64_t feature_id);

        // 按需读取属性数据（不依赖完整索引）
        std::unique_ptr<AttributeData> ReadAttributeOnDemand(uint64_t feature_id);

        // 根据偏移读取属性数据
        std::unique_ptr<AttributeData> ReadAttributeAtOffset(uint64_t feature_id, int64_t offset);

        // 检查要素是否存在
        bool HasFeature(uint64_t feature_id);

        // 清除缓存
        void ClearCache();

        // 获取文件路径
        std::string GetAttributeFilePath() const { return attribute_file_; }
        std::string GetStringPoolFilePath() const { return string_pool_file_; }

        // 从索引文件加载索引 - 优化版本，支持分块加载
        void LoadIndexFromFile(const std::string& index_file);

        // 保存索引到二进制文件
        void SaveIndexToFile(const std::string& index_file);

        // 保存字符串池到文件
        void SaveStringPool();

        // 加载字符串池从文件 - 优化版本，支持内存映射
        void LoadStringPool();

        // 获取压缩统计信息
        AttributeSerializer::CompressionStats GetCompressionStats() const;

        // 获取存储统计信息
        struct StorageStats {
            size_t total_features;
            size_t total_original_size;
            size_t total_compressed_size;
            double compression_ratio;
            size_t string_pool_size;
            size_t string_pool_saved_bytes;
        };
        StorageStats GetStorageStats() const;

        // 设置分块大小
        void SetChunkSize(size_t chunk_size) { chunk_size_ = chunk_size; }

        // 设置缓存大小
        void SetCacheSize(size_t cache_size) { max_cache_size_ = cache_size; }

        // 设置是否启用mmap模式
        void SetUseMmapMode(bool use_mmap) { use_mmap_mode_ = use_mmap; }

        // 构建分块索引
        void BuildChunkedIndex();

        // 保存分块索引到文件
        void SaveChunkedIndex(const std::string& index_file);

        // 从文件加载分块索引
        void LoadChunkedIndex(const std::string& index_file);

      private:
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

        std::string attribute_file_;
        std::string string_pool_file_;

        // 分块索引
        std::vector<IndexChunk> index_chunks_;
        bool use_chunked_mode_ = false;
        size_t chunk_size_ = 10000; // 默认每个块10000个要素

        // 内存映射
        int attr_fd_ = -1;
        char* attr_mmap_ = nullptr;
        size_t attr_size_ = 0;
        bool use_mmap_mode_ = true; // 默认启用mmap模式

        // 简单缓存 - 只缓存最近访问的数据
        mutable std::unordered_map<uint64_t, std::unique_ptr<AttributeData>> cache_;
        mutable size_t max_cache_size_ = 5000; // 增加缓存大小到5000个属性

        mutable std::mutex mutex_;

        AttributeSerializer serializer_;

        // 获取分块偏移
        int64_t GetChunkedOffset(uint64_t feature_id) const;

        // 加载索引块
        void LoadIndexChunk(IndexChunk* chunk) const;

        // 更新LRU缓存
        void UpdateCache(uint64_t fid, std::unique_ptr<AttributeData> data) const;

        // 清理内存映射
        void CleanupMmap();
    };

} // namespace GisStorage

#endif // ATTRIBUTE_STORAGE_H
