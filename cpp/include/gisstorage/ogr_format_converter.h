//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef OGR_FORMAT_CONVERTER_H
#define OGR_FORMAT_CONVERTER_H

#include "types.h"
#include "geometry_storage.h"
#include "attribute_storage.h"
#include "attribute_serializer.h"

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include <ogrsf_frmts.h>

namespace GisStorage {

    // OGR格式转换器类 - 集成字符串池
    class OGRFormatConverter {
      public:
        OGRFormatConverter(const std::string& ogr_file_path, const std::string& output_dir);

        // 转换OGR格式到优化格式
        std::vector<uint64_t> convert();

        // 获取转换后的文件路径
        std::string getGeometryFilePath() const;
        std::string getAttributeFilePath() const;
        std::string getIndexFilePath() const;
        std::string getStringPoolFilePath() const;
        std::string getMetadataFilePath() const;

        // 获取压缩统计信息
        AttributeSerializer::CompressionStats getCompressionStats() const;

        // 获取存储统计信息
        struct ConversionStats {
            size_t total_features;
            size_t valid_features;
            size_t geometry_size;
            size_t attribute_original_size;
            size_t attribute_compressed_size;
            double compression_ratio;
            size_t string_pool_size;
            size_t string_pool_saved_bytes;
            double conversion_time_seconds;
        };
        ConversionStats getConversionStats() const;

      private:
        std::string ogr_file_path_;
        std::string output_dir_;
        std::string ogr_file_name_;
        std::unique_ptr<GeometryStorage> geometry_storage_;
        std::unique_ptr<AttributeStorage> attribute_storage_;
        std::string index_file_;

        // 统计信息
        ConversionStats stats_;

        // 数据集空间范围
        BBox dataset_spatial_extent_;

        // 计算边界框
        BBox calculateBBox(const std::vector<Coordinate>& coordinates);

        // 差分编码压缩坐标
        std::vector<uint8_t> encodeCoordinatesDelta(const std::vector<Coordinate>& coordinates);

        // 优化的差分编码
        std::vector<uint8_t> encodeCoordinatesDeltaOptimized(const std::vector<Coordinate>& coordinates);

        // 提取几何坐标
        std::vector<Coordinate> extractGeometryCoordinates(OGRGeometry* geometry);

        // 提取点坐标
        std::vector<Coordinate> extractPointCoordinates(OGRGeometry* geometry);

        // 提取线坐标
        std::vector<Coordinate> extractLineCoordinates(OGRGeometry* geometry);

        // 提取面坐标
        std::vector<Coordinate> extractPolygonCoordinates(OGRGeometry* geometry);

        // 递归提取坐标
        void extractCoordinatesRecursive(OGRGeometry* geometry, std::vector<Coordinate>& coordinates);

        // 初始化存储文件
        void initializeStorageFiles();

        // 保存索引数据
        void saveIndexData(const nlohmann::json& index_data);

        // 保存字符串池
        void saveStringPool();

        // 保存元数据（包括字段定义、空间范围和坐标系统信息）
        void saveMetadata(const nlohmann::json& field_info, const std::string& source_crs, const std::string& target_crs);

        // 更新统计信息
        void updateStats(size_t geom_size, size_t attr_original_size, size_t attr_compressed_size);
    };

} // namespace GisStorage

#endif // OGR_FORMAT_CONVERTER_H
