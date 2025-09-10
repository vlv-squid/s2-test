//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef GEOMETRY_STORAGE_H
#define GEOMETRY_STORAGE_H

#include "geometry_data.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace GisStorage {

    // 几何数据存储类
    class GeometryStorage {
      public:
        explicit GeometryStorage(const std::string& geometry_file);

        // 写入几何数据
        int64_t WriteGeometry(const GeometryData& geometry);

        // 读取几何数据
        std::unique_ptr<GeometryData> ReadGeometry(uint64_t feature_id);

        // 获取所有要素ID
        std::vector<uint64_t> GetAllFeatureIds();

        // 检查要素是否存在
        bool HasFeature(uint64_t feature_id);

        // 清除缓存
        void ClearCache();

        // 获取文件路径
        std::string GetGeometryFilePath() const { return geometry_file_; }

        // 从索引文件加载索引
        void LoadIndexFromFile(const std::string& index_file);

      private:
        std::string geometry_file_;
        std::unordered_map<uint64_t, int64_t> offset_index_;
        bool index_built_;

        // 构建偏移索引
        void BuildOffsetIndex();

        // 获取偏移索引
        const std::unordered_map<uint64_t, int64_t>& GetOffsetIndex();
    };

} // namespace GisStorage

#endif // GEOMETRY_STORAGE_H
