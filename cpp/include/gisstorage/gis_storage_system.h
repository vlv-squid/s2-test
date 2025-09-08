//
//  Created by vlv-squid on 2025.08.21.
//

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
#include <nlohmann/json.hpp>

// 前向声明
namespace S2Main {
    class S2SpatialIndex;
}

namespace GisStorage {

    // GIS存储系统主类 - 集成文件扩展名定义、S2索引和元数据管理
    class GisStorageSystem {
      public:
        // 文件扩展名定义 - 统一管理所有文件格式
        struct FileExtensions {
            static constexpr const char* GEOMETRY_DATA = ".geom";  // 几何数据文件
            static constexpr const char* ATTRIBUTE_DATA = ".attr"; // 属性数据文件
            static constexpr const char* STRING_POOL = ".pool";    // 字符串池文件
            static constexpr const char* INDEX_DATA = ".idx";      // 索引数据文件
            static constexpr const char* METADATA = "_meta.json";  // 元数据文件
            static constexpr const char* S2_INDEX = ".s2idx";      // S2空间索引文件
        };

        // 元数据结构 - 完整描述数据集信息
        struct Metadata {
            std::string format_version = "1.0";
            std::string source_format = "Shapefile";
            std::string source_file;
            std::string creation_date;
            std::string coordinate_system;
            std::string projection_info;
            // 坐标系统转换信息
            std::string source_coordinate_system;  // 转换前的坐标系统
            std::string target_coordinate_system;  // 转换后的坐标系统
            std::string coordinate_transformation; // 坐标转换信息
            BBox spatial_extent;
            size_t total_features = 0;
            size_t valid_features = 0;
            std::map<std::string, std::string> field_definitions;
            std::map<std::string, std::string> geometry_types;
            std::map<std::string, size_t> file_sizes;
            std::map<std::string, std::string> checksums;
            std::string compression_info;
            std::string index_info;
            std::string s2_index_info;
        };

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

        // 属性查询方法
        std::vector<uint64_t> queryByAttributePattern(const std::string& field_name, const std::string& pattern);
        std::vector<std::pair<uint64_t, std::string>> queryAttributeValues(const std::string& field_name);

        // 高效的属性查询方法（基于字符串池）
        std::vector<uint64_t> queryByAttributeEfficient(const std::string& field_name, const std::string& field_value);

        // 高效的复合查询方法（空间+属性）
        std::vector<uint64_t> querySpatialAttributeEfficient(const BBox& spatial_bbox, const std::string& field_name, const std::string& field_value);

        // 初始化存储文件
        void initializeStorageFiles(const std::string& shapefile_path);

        // 设置数据集名称（用于加载现有数据）
        void setDatasetName(const std::string& dataset_name);

        // 轻量级设置数据集名称（仅加载S2索引和元数据，不加载几何和属性索引）
        void setDatasetNameLightweight(const std::string& dataset_name);

        // S2索引管理
        void initializeS2Index(int resolution = 15);
        bool buildS2IndexFromDataset(const std::string& dataset_path, int max_features_per_cell = 1000);
        std::vector<uint64_t> queryS2Index(const BBox& query_bbox, int resolution = 15);
        void saveS2Index();
        void loadS2Index();
        bool isS2IndexValid() const;
        size_t getS2IndexSize() const;
        size_t getS2TotalFeatureCount() const;

        // 元数据管理
        const Metadata& getMetadata() const { return metadata_; }
        void updateMetadata(const Metadata& metadata);
        void saveMetadata();
        void loadMetadata();
        void updateFileSizes();
        void updateChecksums();

        // 文件路径管理
        std::string getGeometryFilePath() const;
        std::string getAttributeFilePath() const;
        std::string getStringPoolFilePath() const;
        std::string getIndexFilePath() const;
        std::string getMetadataFilePath() const;
        std::string getS2IndexFilePath() const;

        // 存储统计信息
        struct StorageStats {
            size_t total_files = 0;
            size_t total_size_bytes = 0;
            size_t geometry_size_bytes = 0;
            size_t attribute_size_bytes = 0;
            size_t string_pool_size_bytes = 0;
            size_t index_size_bytes = 0;
            size_t s2_index_size_bytes = 0;
            double compression_ratio = 0.0;
            size_t string_pool_saved_bytes = 0;
        };
        StorageStats getStorageStats() const;

      private:
        std::string output_dir_;
        std::string shapefile_name_;
        std::unique_ptr<GeometryStorage> geometry_storage_;
        std::unique_ptr<AttributeStorage> attribute_storage_;
        std::unique_ptr<S2Main::S2SpatialIndex> s2_spatial_index_;

        // 文件路径
        std::string geom_file_;
        std::string attr_file_;
        std::string pool_file_;
        std::string index_file_;
        std::string metadata_file_;
        std::string s2_index_file_;

        // 元数据
        Metadata metadata_;

        // 私有方法
        void initializeFilePaths();
        void createMetadataFromShapefile(const std::string& shapefile_path);
        void updateMetadataStats();
        std::string calculateFileChecksum(const std::string& file_path);
        std::string getCurrentTimestamp();
    };

} // namespace GisStorage

#endif // GIS_STORAGE_SYSTEM_H
