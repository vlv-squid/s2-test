//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef ATTRIBUTE_SERIALIZER_H
#define ATTRIBUTE_SERIALIZER_H

#include "attribute_data.h"
#include "string_pool.h"

#include <vector>
#include <memory>
#include <map>

namespace GisStorage {

    // 属性序列化器类 - 集成字符串池
    class AttributeSerializer {
      public:
        AttributeSerializer();

        // 序列化属性数据（使用字符串池）
        std::vector<uint8_t> serializeAttributes(const AttributeData& attribute);

        // 反序列化属性数据（从字符串池恢复）
        std::unique_ptr<AttributeData> deserializeAttributes(const std::vector<uint8_t>& data);

        // 序列化字符串池
        std::vector<uint8_t> serializeStringPool() const;

        // 反序列化字符串池
        void deserializeStringPool(const std::vector<uint8_t>& data);

        // 获取字符串池统计信息
        size_t getPoolSize() const { return string_pool_.getPoolSize(); }
        size_t getPoolTotalSize() const { return string_pool_.getTotalSize(); }

        // 清空字符串池
        void clearStringPool() { string_pool_.clear(); }

        // 获取压缩率统计
        struct CompressionStats {
            size_t original_size;
            size_t compressed_size;
            double compression_ratio;
            size_t unique_strings;
            size_t total_strings;
        };
        CompressionStats getCompressionStats() const;

      private:
        StringPool string_pool_;
        mutable CompressionStats stats_;

        // 辅助函数
        void updateStats(size_t original_size, size_t compressed_size);
    };

} // namespace GisStorage

#endif // ATTRIBUTE_SERIALIZER_H
