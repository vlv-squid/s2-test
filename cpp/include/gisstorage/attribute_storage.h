//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef ATTRIBUTE_STORAGE_H
#define ATTRIBUTE_STORAGE_H

#include "attribute_data.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace GisStorage {

    // 属性数据存储类
    class AttributeStorage {
      public:
        explicit AttributeStorage(const std::string& attribute_file);

        // 写入属性数据
        int64_t writeAttribute(const AttributeData& attribute);

        // 读取属性数据
        std::unique_ptr<AttributeData> readAttribute(uint64_t feature_id);

        // 获取所有要素ID
        std::vector<uint64_t> getAllFeatureIds();

        // 检查要素是否存在
        bool hasFeature(uint64_t feature_id);

        // 清除缓存
        void clearCache();

        // 获取文件路径
        std::string getAttributeFilePath() const { return attribute_file_; }

        // 从索引文件加载索引
        void loadIndexFromFile(const std::string& index_file);

      private:
        std::string attribute_file_;
        std::unordered_map<uint64_t, int64_t> offset_index_;
        bool index_built_;

        // 构建偏移索引
        void buildOffsetIndex();

        // 获取偏移索引
        const std::unordered_map<uint64_t, int64_t>& getOffsetIndex();
    };

} // namespace GisStorage

#endif // ATTRIBUTE_STORAGE_H
