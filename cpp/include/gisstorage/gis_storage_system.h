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
#include <tbb/tbb.h>
#include <mutex>

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
            static constexpr const char* GEOMETRY_DATA = ".geom";                       // 几何数据文件
            static constexpr const char* ATTRIBUTE_DATA = ".attr";                      // 属性数据文件
            static constexpr const char* STRING_POOL = ".pool";                         // 字符串池文件
            static constexpr const char* GEOMETRY_CHUNKED_INDEX = ".geom.chunked_idx";  // 几何分块索引文件
            static constexpr const char* ATTRIBUTE_CHUNKED_INDEX = ".attr.chunked_idx"; // 属性分块索引文件
            static constexpr const char* METADATA = "_meta.json";                       // 元数据文件
            static constexpr const char* S2_INDEX = ".s2idx";                           // S2空间索引文件
        };

        // 元数据结构 - 完整描述数据集信息
        struct Metadata {
            std::string format_version = "1.0";
            std::string source_format = "Shapefile";
            std::string source_file;
            std::string creation_date;
            // 坐标系统转换信息
            std::string source_coordinate_system; // 转换前的坐标系统
            std::string target_coordinate_system; // 转换后的坐标系统
            BBox spatial_extent;
            size_t total_features = 0;
            size_t valid_features = 0;
            std::map<std::string, std::string> field_definitions;
            std::map<std::string, size_t> file_sizes;
            std::map<std::string, std::string> checksums;
            std::string compression_info;
            std::string s2_index_info;
        };

        explicit GisStorageSystem(const std::string& output_dir);

        // 读取几何数据
        std::unique_ptr<GeometryData> ReadGeometry(uint64_t feature_id);

        // 读取属性数据
        std::unique_ptr<AttributeData> ReadAttribute(uint64_t feature_id);

        // 按需读取几何数据（不依赖完整索引）
        std::unique_ptr<GeometryData> ReadGeometryOnDemand(uint64_t feature_id);

        // 按需读取属性数据（不依赖完整索引）
        std::unique_ptr<AttributeData> ReadAttributeOnDemand(uint64_t feature_id);

        // 获取所有要素ID
        std::vector<uint64_t> GetAllFeatureIds();

        // 属性查询方法
        std::vector<uint64_t> QueryByAttributePattern(const std::string& field_name, const std::string& pattern);
        std::vector<std::pair<uint64_t, std::string>> QueryAttributeValues(const std::string& field_name);

        // 高效的属性查询方法（基于字符串池）
        std::vector<uint64_t> QueryByAttributeEfficient(const std::string& field_name, const std::string& field_value);

        // 并行属性查询方法（使用TBB）
        std::vector<uint64_t> QueryByAttributeParallel(const std::string& field_name, const std::string& field_value);

        // 高效的复合查询方法（空间+属性）
        std::vector<uint64_t> QuerySpatialAttributeEfficient(const BBox& spatial_bbox, const std::string& field_name, const std::string& field_value);

        // 流式查询方法 - 优化版本，支持按需加载
        std::vector<uint64_t> QuerySpatialAttributeStreaming(const BBox& spatial_bbox, const std::string& field_name, const std::string& field_value);

        // 流式空间查询 - 只返回候选要素ID，不加载完整数据
        std::vector<uint64_t> QuerySpatialCandidates(const BBox& spatial_bbox);

        // 初始化存储文件
        void InitializeStorageFiles(const std::string& shapefile_path);

        // 设置数据集名称（用于加载现有数据）
        void SetDatasetName(const std::string& dataset_name);

        // 轻量级设置数据集名称（仅加载S2索引和元数据，不加载几何和属性索引）
        void SetDatasetNameLightweight(const std::string& dataset_name);

        // S2索引管理
        void InitializeS2Index(int resolution = 15);
        bool BuildS2IndexFromDataset(const std::string& dataset_path, int max_features_per_cell = 1000);
        std::vector<uint64_t> QueryS2Index(const BBox& query_bbox, int resolution = 15);
        void SaveS2Index();
        void LoadS2Index();
        bool IsS2IndexValid() const;
        size_t GetS2IndexSize() const;
        size_t GetS2TotalFeatureCount() const;

        // 基于bbox的空间查询方法（不依赖S2索引）
        std::vector<uint64_t> QueryByBBox(const BBox& query_bbox);
        std::vector<uint64_t> QueryByBBoxParallel(const BBox& query_bbox);

        // 元数据管理
        const Metadata& GetMetadata() const { return metadata_; }
        void UpdateMetadata(const Metadata& metadata);
        void SaveMetadata();
        void LoadMetadata();
        void UpdateFileSizes();
        void UpdateChecksums();

        // 文件路径管理
        std::string GetGeometryFilePath() const;
        std::string GetAttributeFilePath() const;
        std::string GetStringPoolFilePath() const;
        std::string GetGeometryChunkedIndexFilePath() const;
        std::string GetAttributeChunkedIndexFilePath() const;
        std::string GetMetadataFilePath() const;
        std::string GetS2IndexFilePath() const;

        // 存储统计信息
        struct StorageStats {
            size_t total_files = 0;
            size_t total_size_bytes = 0;
            size_t geometry_size_bytes = 0;
            size_t attribute_size_bytes = 0;
            size_t string_pool_size_bytes = 0;
            size_t geometry_chunked_index_size_bytes = 0;
            size_t attribute_chunked_index_size_bytes = 0;
            size_t s2_index_size_bytes = 0;
            double compression_ratio = 0.0;
            size_t string_pool_saved_bytes = 0;
        };
        StorageStats GetStorageStats() const;

        bool ValidateFileStructure();               // 验证文件结构完整性
        std::vector<std::string> GetMissingFiles(); // 获取缺失的文件列表

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
        std::string geom_chunked_index_file_;
        std::string attr_chunked_index_file_;
        std::string metadata_file_;
        std::string s2_index_file_;

        // 元数据
        Metadata metadata_;

        // 私有方法
        void InitializeFilePaths();
        void CreateMetadataFromShapefile(const std::string& shapefile_path);
        void UpdateMetadataStats();
        std::string CalculateFileChecksum(const std::string& file_path);
        std::string GetCurrentTimestamp();

        // bbox相交判断辅助函数
        bool IsBBoxIntersecting(const BBox& bbox1, const BBox& bbox2) const;
    };
} // namespace GisStorage

#endif // GIS_STORAGE_SYSTEM_H
