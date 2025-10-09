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
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

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
        uint32_t string_count_ = 0;

        // 字符串偏移量索引，用于O(1)查找
        std::vector<size_t> string_offsets_;
        bool use_mmap_mode_ = false;

        // LRU缓存
        mutable std::unordered_map<uint32_t, CacheEntry> cache_;
        mutable std::list<CacheEntry> lru_list_;
        mutable size_t max_cache_size_ = 5000; // 增加缓存大小到5000个字符串

        size_t total_size_;
        mutable std::mutex mutex_;

        // 计算字符串在池中的存储大小
        size_t CalculateStringSize(const std::string& str) const;

        // 从内存映射中解析字符串
        std::string ParseStringFromMmap(uint32_t id) const;

        // 构建字符串偏移量索引
        void BuildStringOffsetsIndex();

        // 更新LRU缓存
        void UpdateCache(uint32_t id, const std::string& value) const;

        // 清理内存映射
        void CleanupMmap();
    };

} // namespace GisStorage

#endif // STRING_POOL_H
