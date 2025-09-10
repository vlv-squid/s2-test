//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef STRING_POOL_H
#define STRING_POOL_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>

namespace GisStorage {

    // 字符串池类 - 用于减少重复字符串的存储
    class StringPool {
      public:
        StringPool();

        // 获取字符串ID（如果不存在则添加）
        uint32_t GetStringId(const std::string& str);

        // 根据ID获取字符串
        std::string GetString(uint32_t id) const;

        // 序列化字符串池
        std::vector<uint8_t> Serialize() const;

        // 反序列化字符串池
        void Deserialize(const std::vector<uint8_t>& data);

        // 获取统计信息
        size_t GetPoolSize() const { return string_table_.size(); }
        size_t GetTotalSize() const { return total_size_; }

        // 清空池
        void Clear();

      private:
        std::unordered_map<std::string, uint32_t> string_to_id_;
        std::vector<std::string> string_table_;
        size_t total_size_;
        mutable std::mutex mutex_;

        // 计算字符串在池中的存储大小
        size_t CalculateStringSize(const std::string& str) const;
    };

} // namespace GisStorage

#endif // STRING_POOL_H
