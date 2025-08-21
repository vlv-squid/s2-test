#ifndef SHAPEFILE_CONVERTER_H
#define SHAPEFILE_CONVERTER_H

#include "types.h"
#include "geometry_storage.h"
#include "attribute_storage.h"
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include <ogrsf_frmts.h>

namespace GisStorage {

    // Shapefile转换器类
    class ShapefileConverter {
      public:
        ShapefileConverter(const std::string& shapefile_path, const std::string& output_dir);

        // 转换Shapefile到自定义格式
        std::vector<uint64_t> convert();

        // 获取转换后的文件路径
        std::string getGeometryFilePath() const;
        std::string getAttributeFilePath() const;
        std::string getIndexFilePath() const;

      private:
        std::string shapefile_path_;
        std::string output_dir_;
        std::string shapefile_name_;
        std::unique_ptr<GeometryStorage> geometry_storage_;
        std::unique_ptr<AttributeStorage> attribute_storage_;
        std::string index_file_;

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
    };

} // namespace GisStorage

#endif // SHAPEFILE_CONVERTER_H
