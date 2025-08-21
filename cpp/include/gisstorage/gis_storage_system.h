#ifndef GIS_STORAGE_SYSTEM_H
#define GIS_STORAGE_SYSTEM_H

#include "geometry_data.h"
#include "attribute_data.h"
#include "geometry_storage.h"
#include "attribute_storage.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace GisStorage {

    // GIS存储系统主类
    class GisStorageSystem {
      public:
        explicit GisStorageSystem(const std::string& output_dir);

        // 读取几何数据
        std::unique_ptr<GeometryData> readGeometry(uint64_t feature_id);

        // 读取属性数据
        std::unique_ptr<AttributeData> readAttribute(uint64_t feature_id);

        // 获取所有要素ID
        std::vector<uint64_t> getAllFeatureIds();

        // 批量读取几何数据
        std::map<uint64_t, std::unique_ptr<GeometryData>> readGeometries(const std::vector<uint64_t>& feature_ids);

        // 批量读取属性数据
        std::map<uint64_t, std::unique_ptr<AttributeData>> readAttributes(const std::vector<uint64_t>& feature_ids);

        // 初始化存储文件
        void initializeStorageFiles(const std::string& shapefile_path);

      private:
        std::string output_dir_;
        std::string shapefile_name_;
        std::unique_ptr<GeometryStorage> geometry_storage_;
        std::unique_ptr<AttributeStorage> attribute_storage_;
    };

} // namespace GisStorage

#endif // GIS_STORAGE_SYSTEM_H
