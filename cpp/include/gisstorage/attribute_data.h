//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef ATTRIBUTE_DATA_H
#define ATTRIBUTE_DATA_H

#include <string>
#include <map>
#include <cstdint>
#include <optional>

namespace GisStorage {

    // 属性数据类
    class AttributeData {
      public:
        AttributeData(uint64_t feature_id, const std::map<std::string, std::string>& properties);

        AttributeData(uint64_t feature_id, const std::map<std::string, std::optional<std::string>>& properties);

        uint64_t GetFeatureId() const { return feature_id_; }
        const std::map<std::string, std::optional<std::string>>& GetProperties() const { return properties_; }

        std::optional<std::string> GetProperty(const std::string& key) const;

        std::string GetProperty(const std::string& key, const std::string& default_value) const;

        // 检查属性是否存在且不为NULL
        bool HasProperty(const std::string& key) const;

        // 计算序列化后的大小
        size_t GetSerializedSize() const;

      private:
        uint64_t feature_id_;
        std::map<std::string, std::optional<std::string>> properties_;
    };

} // namespace GisStorage

#endif // ATTRIBUTE_DATA_H
