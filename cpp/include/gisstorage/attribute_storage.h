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

    // 属性数据存储类 - 集成字符串池
    class AttributeStorage {
      public:
        explicit AttributeStorage(const std::string& attribute_file, const std::string& string_pool_file);

        // 写入属性数据
        int64_t WriteAttribute(const AttributeData& attribute);

        // 读取属性数据
        std::unique_ptr<AttributeData> ReadAttribute(uint64_t feature_id);

        // 获取所有要素ID
        std::vector<uint64_t> GetAllFeatureIds();

        // 检查要素是否存在
        bool HasFeature(uint64_t feature_id);

        // 清除缓存
        void ClearCache();

        // 获取文件路径
        std::string GetAttributeFilePath() const { return attribute_file_; }
        std::string GetStringPoolFilePath() const { return string_pool_file_; }

        // 从索引文件加载索引
        void LoadIndexFromFile(const std::string& index_file);

        // 保存索引到二进制文件
        void SaveIndexToFile(const std::string& index_file);

        // 保存字符串池到文件
        void SaveStringPool();

        // 加载字符串池从文件
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

      private:
        std::string attribute_file_;
        std::string string_pool_file_;
        std::unordered_map<uint64_t, int64_t> offset_index_;
        bool index_built_;

        AttributeSerializer serializer_;

        // 构建偏移索引
        void BuildOffsetIndex();

        // 获取偏移索引
        const std::unordered_map<uint64_t, int64_t>& GetOffsetIndex();
    };

} // namespace GisStorage

#endif // ATTRIBUTE_STORAGE_H
