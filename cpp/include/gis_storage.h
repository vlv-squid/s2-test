#ifndef GIS_STORAGE_H
#define GIS_STORAGE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <fstream>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <ogrsf_frmts.h>

namespace GisStorage {

    // 前向声明
    class GeometryData;
    class AttributeData;

    // 几何数据类型枚举
    enum class GeometryType : uint8_t { POINT = 0, LINE = 1, POLYGON = 2, MULTIPOINT = 3, MULTILINE = 4, MULTIPOLYGON = 5 };

    // 坐标点结构
    struct Coordinate {
        double x;
        double y;

        Coordinate(double x = 0.0, double y = 0.0)
            : x(x)
            , y(y) {}
    };

    // 边界框结构
    struct BBox {
        double min_x;
        double min_y;
        double max_x;
        double max_y;

        BBox(double min_x = 0.0, double min_y = 0.0, double max_x = 0.0, double max_y = 0.0)
            : min_x(min_x)
            , min_y(min_y)
            , max_x(max_x)
            , max_y(max_y) {}

        bool isValid() const { return min_x <= max_x && min_y <= max_y; }
    };

    // 几何数据类
    class GeometryData {
      public:
        GeometryData(uint64_t feature_id, GeometryType geometry_type, const std::vector<uint8_t>& coordinates, const BBox& bbox);

        uint64_t getFeatureId() const { return feature_id_; }
        GeometryType getGeometryType() const { return geometry_type_; }
        const std::vector<uint8_t>& getCoordinates() const { return coordinates_; }
        const BBox& getBBox() const { return bbox_; }

        // 解码压缩的坐标数据
        std::vector<Coordinate> decodeCoordinates() const;

        // 计算序列化后的大小
        size_t getSerializedSize() const;

      private:
        uint64_t feature_id_;
        GeometryType geometry_type_;
        std::vector<uint8_t> coordinates_; // 压缩的坐标数据
        BBox bbox_;
    };

    // 属性数据类
    class AttributeData {
      public:
        AttributeData(uint64_t feature_id, const std::map<std::string, std::string>& properties);

        uint64_t getFeatureId() const { return feature_id_; }
        const std::map<std::string, std::string>& getProperties() const { return properties_; }

        // 获取属性值
        std::string getProperty(const std::string& key, const std::string& default_value = "") const;

        // 计算序列化后的大小
        size_t getSerializedSize() const;

      private:
        uint64_t feature_id_;
        std::map<std::string, std::string> properties_;
    };

    // 几何数据存储类
    class GeometryStorage {
      public:
        explicit GeometryStorage(const std::string& geometry_file);

        // 写入几何数据
        int64_t writeGeometry(const GeometryData& geometry);

        // 读取几何数据
        std::unique_ptr<GeometryData> readGeometry(uint64_t feature_id);

        // 获取所有要素ID
        std::vector<uint64_t> getAllFeatureIds();

        // 检查要素是否存在
        bool hasFeature(uint64_t feature_id);

        // 清除缓存
        void clearCache();

        // 获取文件路径
        std::string getGeometryFilePath() const { return geometry_file_; }

        // 从索引文件加载索引
        void loadIndexFromFile(const std::string& index_file);

      private:
        std::string geometry_file_;
        std::unordered_map<uint64_t, int64_t> offset_index_;
        bool index_built_;

        // 构建偏移索引
        void buildOffsetIndex();

        // 获取偏移索引
        const std::unordered_map<uint64_t, int64_t>& getOffsetIndex();
    };

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

    // 序列化器类
    class GeometrySerializer {
      public:
        // 序列化几何数据
        static std::vector<uint8_t> serializeGeometry(const GeometryData& geometry);

        // 反序列化几何数据
        static std::unique_ptr<GeometryData> deserializeGeometry(const std::vector<uint8_t>& data);

        // 序列化坐标数据
        static std::vector<uint8_t> serializeCoordinates(const std::vector<Coordinate>& coordinates);

        // 反序列化坐标数据
        static std::vector<Coordinate> deserializeCoordinates(const std::vector<uint8_t>& data);

        // 差分编码坐标压缩
        static std::vector<uint8_t> encodeCoordinatesDelta(const std::vector<Coordinate>& coordinates);

        // 差分编码坐标解压
        static std::vector<Coordinate> decodeCoordinatesDelta(const std::vector<uint8_t>& data);

        // 计算边界框
        static BBox calculateBBox(const std::vector<Coordinate>& coordinates);
    };

    // 属性序列化器类
    class AttributeSerializer {
      public:
        // 序列化属性数据
        static std::vector<uint8_t> serializeAttributes(const AttributeData& attribute);

        // 反序列化属性数据
        static std::unique_ptr<AttributeData> deserializeAttributes(const std::vector<uint8_t>& data);
    };

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

#endif // GIS_STORAGE_H
