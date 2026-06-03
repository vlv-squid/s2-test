//
//  Created by vlv-squid on 2025.07.23.
//  优化版本：支持模板化序列化，兼容absl和std数据结构
//

#include "gisindex/serialize_s2.h"

#include <fstream>
#include <iostream>

// 包含absl头文件用于特化
#include <absl/container/flat_hash_map.h>
#include <absl/container/inlined_vector.h>

namespace helper {

    // 通用容器序列化辅助函数
    template<typename Container>
    void SerializeContainer(std::ofstream& file, const Container& container) {
        size_t size = container.size();
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));

        for (const auto& item : container) {
            file.write(reinterpret_cast<const char*>(&item), sizeof(item));
        }
    }

    template<typename Container>
    void DeserializeContainer(std::ifstream& file, Container& container) {
        size_t size;
        file.read(reinterpret_cast<char*>(&size), sizeof(size));

        container.clear();
        container.reserve(size);

        for (size_t i = 0; i < size; ++i) {
            typename Container::value_type item;
            file.read(reinterpret_cast<char*>(&item), sizeof(item));
            container.push_back(item);
        }
    }

    // 特化版本：std::unordered_map + std::vector
    template<>
    bool SaveS2IndexToFile(const std::string& filepath, const std::unordered_map<int64_t, std::vector<int>>& s2IndexMap) {
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件进行写入: " << filepath << std::endl;
            return false;
        }

        try {
            size_t indexCount = s2IndexMap.size();
            file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

            for (const auto& [cellId, fids] : s2IndexMap) {
                file.write(reinterpret_cast<const char*>(&cellId), sizeof(cellId));
                SerializeContainer(file, fids);
            }

            file.close();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "序列化过程中发生错误: " << e.what() << std::endl;
            file.close();
            return false;
        }
    }

    template<>
    bool LoadS2IndexFromFile(const std::string& filepath, std::unordered_map<int64_t, std::vector<int>>& s2IndexMap) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件进行读取: " << filepath << std::endl;
            return false;
        }

        try {
            size_t indexCount;
            file.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));

            s2IndexMap.clear();
            s2IndexMap.reserve(indexCount); // 预分配空间

            for (size_t i = 0; i < indexCount; ++i) {
                int64_t cellId;
                file.read(reinterpret_cast<char*>(&cellId), sizeof(cellId));

                std::vector<int> fids;
                DeserializeContainer(file, fids);

                s2IndexMap.emplace(cellId, std::move(fids));
            }

            file.close();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "反序列化过程中发生错误: " << e.what() << std::endl;
            file.close();
            return false;
        }
    }

    // 特化版本：absl::flat_hash_map + absl::InlinedVector
    template<>
    bool SaveS2IndexToFile(const std::string& filepath, const absl::flat_hash_map<int64_t, absl::InlinedVector<int, 8>>& s2IndexMap) {
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件进行写入: " << filepath << std::endl;
            return false;
        }

        try {
            size_t indexCount = s2IndexMap.size();
            file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

            for (const auto& [cellId, fids] : s2IndexMap) {
                file.write(reinterpret_cast<const char*>(&cellId), sizeof(cellId));
                SerializeContainer(file, fids);
            }

            file.close();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "序列化过程中发生错误: " << e.what() << std::endl;
            file.close();
            return false;
        }
    }

    template<>
    bool LoadS2IndexFromFile(const std::string& filepath, absl::flat_hash_map<int64_t, absl::InlinedVector<int, 8>>& s2IndexMap) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件进行读取: " << filepath << std::endl;
            return false;
        }

        try {
            size_t indexCount;
            file.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));

            s2IndexMap.clear();
            s2IndexMap.reserve(indexCount); // 预分配空间

            for (size_t i = 0; i < indexCount; ++i) {
                int64_t cellId;
                file.read(reinterpret_cast<char*>(&cellId), sizeof(cellId));

                absl::InlinedVector<int, 8> fids;
                DeserializeContainer(file, fids);

                s2IndexMap.emplace(cellId, std::move(fids));
            }

            file.close();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "反序列化过程中发生错误: " << e.what() << std::endl;
            file.close();
            return false;
        }
    }

    // 通用模板实现（用于其他兼容的容器类型）
    template<typename MapType>
    bool SaveS2IndexToFile(const std::string& filepath, const MapType& s2IndexMap) {
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件进行写入: " << filepath << std::endl;
            return false;
        }

        try {
            size_t indexCount = s2IndexMap.size();
            file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

            for (const auto& [cellId, fids] : s2IndexMap) {
                file.write(reinterpret_cast<const char*>(&cellId), sizeof(cellId));
                SerializeContainer(file, fids);
            }

            file.close();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "序列化过程中发生错误: " << e.what() << std::endl;
            file.close();
            return false;
        }
    }

    template<typename MapType>
    bool LoadS2IndexFromFile(const std::string& filepath, MapType& s2IndexMap) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件进行读取: " << filepath << std::endl;
            return false;
        }

        try {
            size_t indexCount;
            file.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));

            s2IndexMap.clear();
            // 对于支持reserve的容器类型，预分配空间
            if constexpr (std::is_same_v<MapType, std::unordered_map<int64_t, std::vector<int>>> || std::is_same_v<MapType, absl::flat_hash_map<int64_t, absl::InlinedVector<int, 8>>>) {
                s2IndexMap.reserve(indexCount);
            }

            for (size_t i = 0; i < indexCount; ++i) {
                int64_t cellId;
                file.read(reinterpret_cast<char*>(&cellId), sizeof(cellId));

                typename MapType::mapped_type fids;
                DeserializeContainer(file, fids);

                s2IndexMap.emplace(cellId, std::move(fids));
            }

            file.close();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "反序列化过程中发生错误: " << e.what() << std::endl;
            file.close();
            return false;
        }
    }

}; // namespace helper
