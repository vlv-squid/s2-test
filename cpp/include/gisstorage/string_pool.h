//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef STRING_POOL_H
#define STRING_POOL_H

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <list>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <atomic>
#include <array>

namespace GisStorage {

    // 字符串池类 - 用于减少重复字符串的存储，支持内存映射和按需加载
    class StringPool {
      public:
        StringPool();
        ~StringPool();

        // 获取字符串ID（如果不存在则添加）
        uint32_t GetStringId(const std::string& str);

        // 根据ID获取字符串 - 优化版本，支持内存映射和缓存
        std::string GetString(uint32_t id) const;

        // 序列化字符串池
        std::vector<uint8_t> Serialize() const;

        // 反序列化字符串池 - 优化版本，支持内存映射
        void Deserialize(const std::vector<uint8_t>& data);

        // 从文件加载字符串池（内存映射方式）
        bool LoadFromFile(const std::string& file_path);

        // 保存字符串池到文件
        bool SaveToFile(const std::string& file_path) const;

        // 获取统计信息
        size_t GetPoolSize() const { return string_count_; }
        size_t GetTotalSize() const { return total_size_; }

        // 获取缓存统计信息
        struct CacheStats {
            uint64_t hits;
            uint64_t misses;
            double hit_rate;
            size_t cache_size;
        };
        CacheStats GetCacheStats() const;

        // 清空池
        void Clear();

        // 设置缓存大小
        void SetCacheSize(size_t cache_size) { max_cache_size_ = cache_size; }

      private:
        // 缓存条目结构
        struct CacheEntry {
            uint32_t id;
            std::string value;
            mutable std::list<CacheEntry>::iterator lru_it;
        };

        // 传统模式的数据结构（用于写入）
        std::unordered_map<std::string, uint32_t> string_to_id_;
        std::vector<std::string> string_table_;

        // 内存映射模式的数据结构（用于读取）
        int pool_fd_ = -1;
        char* pool_mmap_ = nullptr;
        size_t pool_size_ = 0;
        mutable uint32_t string_count_ = 0;

        // 字符串偏移量索引，用于O(1)查找
        // 使用shared_ptr共享，避免每个实例重复存储
        mutable std::shared_ptr<const std::vector<size_t>> shared_string_offsets_;
        mutable std::vector<size_t> string_offsets_; // 回退到本地存储（如果未使用共享）
        bool use_mmap_mode_ = false;

        // 共享mmap信息（用于多实例共享同一个mmap）
        std::shared_ptr<void> shared_mmap_info_; // 类型擦除，实际类型在cpp中定义

        // 无锁缓存设计
        mutable std::unordered_map<uint32_t, CacheEntry> cache_;
        mutable std::list<CacheEntry> lru_list_;
        mutable size_t max_cache_size_ = 20000; // 大幅增加缓存大小到20000个字符串

        // 缓存统计
        mutable std::atomic<uint64_t> cache_hits_{0};
        mutable std::atomic<uint64_t> cache_misses_{0};

        // 无锁快速缓存（用于热点字符串）
        mutable std::array<std::pair<uint32_t, std::string>, 1024> fast_cache_;
        mutable std::atomic<size_t> fast_cache_index_{0};

        size_t total_size_;
        mutable std::mutex mutex_;

        // 计算字符串在池中的存储大小
        size_t CalculateStringSize(const std::string& str) const;

        // 从内存映射中解析字符串
        std::string ParseStringFromMmap(uint32_t id) const;

        // SIMD优化的字符串解析
        std::string ParseStringFromMmapSIMD(uint32_t id) const;

        // 构建字符串偏移量索引
        void BuildStringOffsetsIndex();

        // 并行构建字符串偏移量索引
        void BuildStringOffsetsIndexParallel();

        // 从内存中的字符串表构建偏移量索引
        void BuildOffsetsIndexFromMemory() const;

        // 保存偏移量索引到文件
        bool SaveOffsetsIndexToFile(const std::string& index_file_path) const;

        // 从文件加载偏移量索引
        bool LoadOffsetsIndexFromFile(const std::string& index_file_path);

        // 更新LRU缓存
        void UpdateCache(uint32_t id, const std::string& value) const;

        // 无锁快速缓存操作
        std::string GetStringFromFastCache(uint32_t id) const;
        void UpdateFastCache(uint32_t id, const std::string& value) const;

        // 批量预取优化
        void PrefetchStrings(const std::vector<uint32_t>& ids) const;
        std::vector<std::string> GetStringsBatch(const std::vector<uint32_t>& ids) const;

        // 清理内存映射
        void CleanupMmap();

        // 变长编码辅助函数
        void EncodeVarint(std::vector<uint8_t>& data, uint32_t value) const;
        size_t DecodeVarint(const std::vector<uint8_t>& data, size_t offset, uint32_t& value) const;
        size_t CalculateVarintSize(uint32_t value) const;
        size_t GetVarintSize(uint32_t value) const;

        // 从mmap中解码变长编码
        size_t DecodeVarintFromMmap(size_t offset, uint32_t& value) const;
    };

} // namespace GisStorage

#endif // STRING_POOL_H
