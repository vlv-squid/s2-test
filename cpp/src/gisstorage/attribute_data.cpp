//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/attribute_data.h"

#include <sstream>

namespace GisStorage {

    AttributeData::AttributeData(uint64_t feature_id, const std::map<std::string, std::string>& properties)
        : feature_id_(feature_id) {
        for (const auto& prop : properties) {
            properties_[prop.first] = prop.second;
        }
    }

    AttributeData::AttributeData(uint64_t feature_id, const std::map<std::string, std::optional<std::string>>& properties)
        : feature_id_(feature_id)
        , properties_(properties) {}

    std::optional<std::string> AttributeData::GetProperty(const std::string& key) const {
        auto it = properties_.find(key);
        return (it != properties_.end()) ? it->second : std::nullopt;
    }

    std::string AttributeData::GetProperty(const std::string& key, const std::string& default_value) const {
        auto value = GetProperty(key);
        return value.has_value() ? value.value() : default_value;
    }

    bool AttributeData::HasProperty(const std::string& key) const {
        auto value = GetProperty(key);
        return value.has_value();
    }

    size_t AttributeData::GetSerializedSize() const {
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& prop : properties_) {
            if (!first)
                oss << ",";
            oss << "\"" << prop.first << "\":";
            if (prop.second.has_value()) {
                oss << "\"" << prop.second.value() << "\"";
            } else {
                oss << "null";
            }
            first = false;
        }
        oss << "}";
        std::string json_str = oss.str();

        // feature_id(8) + json_length(4) + json_data
        return 8 + 4 + json_str.length();
    }

} // namespace GisStorage
