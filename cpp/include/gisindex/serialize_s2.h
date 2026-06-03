//
//  Created by vlv-squid on 2025.07.23.
//  优化版本：支持模板化序列化，兼容absl和std数据结构
//

#ifndef SERIALIZE_S2_OPTIMIZED_H
#define SERIALIZE_S2_OPTIMIZED_H

#include <string>
#include <unordered_map>
#include <vector>

// 前向声明，避免循环依赖
namespace S2Main {
    template<typename MapType>
    class S2SpatialIndexBase;
}

namespace helper {

    // 模板化的序列化接口
    template<typename MapType>
    bool SaveS2IndexToFile(const std::string& filepath, const MapType& s2IndexMap);

    template<typename MapType>
    bool LoadS2IndexFromFile(const std::string& filepath, MapType& s2IndexMap);

    // 特化版本：支持std::unordered_map
    template<>
    bool SaveS2IndexToFile(const std::string& filepath, const std::unordered_map<int64_t, std::vector<int>>& s2IndexMap);

    template<>
    bool LoadS2IndexFromFile(const std::string& filepath, std::unordered_map<int64_t, std::vector<int>>& s2IndexMap);

    // 特化版本：支持absl::flat_hash_map + absl::InlinedVector
    // 注意：这里需要包含absl头文件，但为了避免循环依赖，我们在实现文件中特化

    // 通用序列化辅助函数
    template<typename Container>
    void SerializeContainer(std::ofstream& file, const Container& container);

    template<typename Container>
    void DeserializeContainer(std::ifstream& file, Container& container);

}; // namespace helper

#endif // SERIALIZE_S2_OPTIMIZED_H
