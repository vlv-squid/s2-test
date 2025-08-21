#include "gisstorage/gis_storage_system.h"
#include "gisstorage/shapefile_converter.h"

#include <filesystem>
#include <iostream>

namespace GisStorage {

    // GisStorageSystem 实现
    GisStorageSystem::GisStorageSystem(const std::string& output_dir)
        : output_dir_(output_dir) {
        // 创建输出目录
        std::filesystem::create_directories(output_dir);
    }

    std::unique_ptr<GeometryData> GisStorageSystem::readGeometry(uint64_t feature_id) {
        if (!geometry_storage_) {
            throw std::runtime_error("几何存储未初始化");
        }
        return geometry_storage_->readGeometry(feature_id);
    }

    std::unique_ptr<AttributeData> GisStorageSystem::readAttribute(uint64_t feature_id) {
        if (!attribute_storage_) {
            throw std::runtime_error("属性存储未初始化");
        }
        return attribute_storage_->readAttribute(feature_id);
    }

    std::vector<uint64_t> GisStorageSystem::getAllFeatureIds() {
        if (!geometry_storage_) {
            return {};
        }
        return geometry_storage_->getAllFeatureIds();
    }

    std::map<uint64_t, std::unique_ptr<GeometryData>> GisStorageSystem::readGeometries(const std::vector<uint64_t>& feature_ids) {
        std::map<uint64_t, std::unique_ptr<GeometryData>> results;

        if (!geometry_storage_) {
            return results;
        }

        for (uint64_t fid : feature_ids) {
            try {
                auto geom = geometry_storage_->readGeometry(fid);
                if (geom) {
                    results[fid] = std::move(geom);
                }
            } catch (const std::exception& e) {
                std::cerr << "读取几何数据失败 FID " << fid << ": " << e.what() << std::endl;
            }
        }

        return results;
    }

    std::map<uint64_t, std::unique_ptr<AttributeData>> GisStorageSystem::readAttributes(const std::vector<uint64_t>& feature_ids) {
        std::map<uint64_t, std::unique_ptr<AttributeData>> results;

        if (!attribute_storage_) {
            return results;
        }

        for (uint64_t fid : feature_ids) {
            try {
                auto attr = attribute_storage_->readAttribute(fid);
                if (attr) {
                    results[fid] = std::move(attr);
                }
            } catch (const std::exception& e) {
                std::cerr << "读取属性数据失败 FID " << fid << ": " << e.what() << std::endl;
            }
        }

        return results;
    }

    void GisStorageSystem::initializeStorageFiles(const std::string& shapefile_path) {
        // 提取Shapefile名称
        std::filesystem::path path(shapefile_path);
        shapefile_name_ = path.stem().string();

        // 初始化存储对象
        std::string geom_file = output_dir_ + "/" + shapefile_name_ + "_geom.dat";
        std::string attr_file = output_dir_ + "/" + shapefile_name_ + "_attr.dat";
        std::string index_file = output_dir_ + "/" + shapefile_name_ + "_index.dat";

        geometry_storage_ = std::make_unique<GeometryStorage>(geom_file);
        attribute_storage_ = std::make_unique<AttributeStorage>(attr_file);

        // 加载索引
        if (std::filesystem::exists(index_file)) {
            geometry_storage_->loadIndexFromFile(index_file);
            attribute_storage_->loadIndexFromFile(index_file);
        }
    }

} // namespace GisStorage
