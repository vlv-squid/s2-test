#include "attribute_data.h"
#include <sstream>

namespace GisStorage {

    // AttributeData 实现
    AttributeData::AttributeData(uint64_t feature_id, const std::map<std::string, std::string>& properties)
        : feature_id_(feature_id)
        , properties_(properties) {}

    std::string AttributeData::getProperty(const std::string& key, const std::string& default_value) const {
        auto it = properties_.find(key);
        return (it != properties_.end()) ? it->second : default_value;
    }

    size_t AttributeData::getSerializedSize() const {
        // 计算JSON字符串长度
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& prop : properties_) {
            if (!first)
                oss << ",";
            oss << "\"" << prop.first << "\":\"" << prop.second << "\"";
            first = false;
        }
        oss << "}";
        std::string json_str = oss.str();

        // feature_id(8) + json_length(4) + json_data
        return 8 + 4 + json_str.length();
    }

} // namespace GisStorage
