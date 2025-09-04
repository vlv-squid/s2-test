//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/geometry_storage.h"
#include "gisstorage/geometry_serializer.h"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace GisStorage {

    // GeometryStorage 实现
    GeometryStorage::GeometryStorage(const std::string& geometry_file)
        : geometry_file_(geometry_file)
        , index_built_(false) {
        std::filesystem::path file_path(geometry_file);
        std::filesystem::create_directories(file_path.parent_path());
    }

    int64_t GeometryStorage::writeGeometry(const GeometryData& geometry) {
        // 使用追加模式，如果文件不存在则创建新文件
        std::ofstream file(geometry_file_, std::ios::binary | std::ios::app);
        if (!file) {
            throw std::runtime_error("无法打开几何文件进行写入: " + geometry_file_);
        }

        int64_t offset = file.tellp();
        std::vector<uint8_t> geom_binary = GeometrySerializer::serializeGeometry(geometry);

        if (!geom_binary.empty()) {
            file.write(reinterpret_cast<const char*>(geom_binary.data()), geom_binary.size());
            file.flush(); // 确保数据写入磁盘
            // 清除索引缓存
            offset_index_.clear();
            index_built_ = false;
            return offset;
        } else {
            throw std::runtime_error("几何数据序列化失败 for FID " + std::to_string(geometry.getFeatureId()));
        }
    }

    std::unique_ptr<GeometryData> GeometryStorage::readGeometry(uint64_t feature_id) {
        const auto& offsets = getOffsetIndex();
        auto it = offsets.find(feature_id);
        if (it == offsets.end()) {
            throw std::runtime_error("Feature ID " + std::to_string(feature_id) + " not found");
        }

        std::ifstream file(geometry_file_, std::ios::binary);
        if (!file) {
            throw std::runtime_error("无法打开几何文件: " + geometry_file_);
        }

        file.seekg(it->second);

        // 先读取头部数据以确定需要读取的总大小（与Python版本保持一致）
        std::vector<uint8_t> header_data(48);
        if (!file.read(reinterpret_cast<char*>(header_data.data()), 48)) {
            throw std::runtime_error("几何数据不完整 for FID " + std::to_string(feature_id));
        }

        // 获取坐标数据大小
        uint32_t coord_size = 0;
        if (!file.read(reinterpret_cast<char*>(&coord_size), sizeof(uint32_t))) {
            throw std::runtime_error("几何数据不完整 for FID " + std::to_string(feature_id));
        }

        // 读取坐标数据
        std::vector<uint8_t> coord_data(coord_size);
        if (coord_size > 0) {
            if (!file.read(reinterpret_cast<char*>(coord_data.data()), coord_size)) {
                throw std::runtime_error("几何坐标数据不完整 for FID " + std::to_string(feature_id));
            }
        }

        // 组合所有数据进行反序列化
        std::vector<uint8_t> geom_data = header_data;
        geom_data.insert(geom_data.end(), reinterpret_cast<uint8_t*>(&coord_size), reinterpret_cast<uint8_t*>(&coord_size) + sizeof(uint32_t));
        geom_data.insert(geom_data.end(), coord_data.begin(), coord_data.end());

        return GeometrySerializer::deserializeGeometry(geom_data);
    }

    std::vector<uint64_t> GeometryStorage::getAllFeatureIds() {
        const auto& offsets = getOffsetIndex();
        std::vector<uint64_t> feature_ids;
        feature_ids.reserve(offsets.size());
        for (const auto& pair : offsets) {
            feature_ids.push_back(pair.first);
        }
        return feature_ids;
    }

    bool GeometryStorage::hasFeature(uint64_t feature_id) {
        const auto& offsets = getOffsetIndex();
        return offsets.find(feature_id) != offsets.end();
    }

    void GeometryStorage::clearCache() {
        offset_index_.clear();
        index_built_ = false;
    }

    void GeometryStorage::buildOffsetIndex() {
        offset_index_.clear();

        std::ifstream file(geometry_file_, std::ios::binary);
        if (!file) {
            return;
        }

        while (true) {
            int64_t current_pos = file.tellg();

            // 读取feature_id
            uint64_t fid;
            file.read(reinterpret_cast<char*>(&fid), sizeof(uint64_t));
            if (file.gcount() < sizeof(uint64_t)) {
                break; // 文件结束
            }

            offset_index_[fid] = current_pos;

            // 手动解析记录结构（与Python版本保持一致）
            try {
                // 跳过geometry_type(1B) + 7字节填充 + bbox(32B) = 40字节
                file.seekg(40, std::ios::cur);

                // 读取坐标大小(4B)
                uint32_t coord_size;
                file.read(reinterpret_cast<char*>(&coord_size), sizeof(uint32_t));
                if (file.gcount() < sizeof(uint32_t)) {
                    break;
                }

                // 跳过坐标数据
                file.seekg(coord_size, std::ios::cur);

            } catch (const std::exception& e) {
                std::cerr << "解析几何记录失败 at FID " << fid << ": " << e.what() << std::endl;
                break;
            }
        }
    }

    const std::unordered_map<uint64_t, int64_t>& GeometryStorage::getOffsetIndex() {
        if (!index_built_) {
            buildOffsetIndex();
            index_built_ = true;
        }
        return offset_index_;
    }

    void GeometryStorage::loadIndexFromFile(const std::string& index_file) {
        if (!std::filesystem::exists(index_file)) {
            std::cout << "索引文件不存在: " << index_file << std::endl;
            return;
        }

        offset_index_.clear();

        // 尝试读取JSON格式的索引文件
        std::ifstream file(index_file);
        if (!file) {
            std::cout << "无法打开索引文件: " << index_file << std::endl;
            return;
        }

        try {
            nlohmann::json index_data = nlohmann::json::parse(file);

            // 验证JSON结构
            if (!index_data.contains("version") || !index_data.contains("data") || !index_data["data"].contains("features")) {
                std::cout << "索引文件格式不正确" << std::endl;
                return;
            }

            std::cout << "索引文件格式: JSON" << std::endl;
            std::cout << "版本: " << index_data["version"] << std::endl;
            std::cout << "要素数量: " << index_data["data"]["features"].size() << std::endl;

            // 读取每个索引条目
            int count = 0;
            for (const auto& feature : index_data["data"]["features"].items()) {
                uint64_t fid = std::stoull(feature.key());
                int64_t geom_offset = feature.value()["geom_offset"];
                int64_t attr_offset = feature.value()["attr_offset"];

                offset_index_[fid] = geom_offset;

                if (count < 5) { // 只显示前5个条目
                    std::cout << "索引条目 " << count << ": FID=" << fid << ", geom_offset=" << geom_offset << ", attr_offset=" << attr_offset << std::endl;
                }
                count++;
            }

            std::cout << "加载的索引条目数量: " << offset_index_.size() << std::endl;
            index_built_ = true;

        } catch (const nlohmann::json::exception& e) {
            std::cout << "JSON索引文件解析失败: " << e.what() << std::endl;
            std::cout << "尝试读取旧格式的二进制索引文件..." << std::endl;

            // 如果JSON解析失败，尝试读取旧的二进制格式
            file.close();
            file.open(index_file, std::ios::binary);
            if (!file) {
                std::cout << "无法以二进制模式打开索引文件" << std::endl;
                return;
            }

            // 读取索引条目数量
            uint32_t count;
            file.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
            if (file.gcount() < sizeof(uint32_t)) {
                std::cout << "无法读取索引条目数量" << std::endl;
                return;
            }

            std::cout << "索引条目数量: " << count << std::endl;

            // 读取每个索引条目：FID + 几何偏移 + 属性偏移
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t fid = 0;
                int64_t geom_offset = 0;
                int64_t attr_offset = 0;

                if (!file.read(reinterpret_cast<char*>(&fid), sizeof(uint64_t))) {
                    std::cout << "读取索引条目 " << i << " 的FID失败" << std::endl;
                    break;
                }
                if (!file.read(reinterpret_cast<char*>(&geom_offset), sizeof(int64_t))) {
                    std::cout << "读取索引条目 " << i << " 的geom_offset失败" << std::endl;
                    break;
                }
                if (!file.read(reinterpret_cast<char*>(&attr_offset), sizeof(int64_t))) {
                    std::cout << "读取索引条目 " << i << " 的attr_offset失败" << std::endl;
                    break;
                }

                offset_index_[fid] = geom_offset;

                if (i < 5) { // 只显示前5个条目
                    std::cout << "索引条目 " << i << ": FID=" << fid << ", geom_offset=" << geom_offset << ", attr_offset=" << attr_offset << std::endl;
                }
            }

            std::cout << "加载的索引条目数量: " << offset_index_.size() << std::endl;
            index_built_ = true;
        }
    }

} // namespace GisStorage
