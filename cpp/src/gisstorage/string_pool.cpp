//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/string_pool.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace GisStorage {

    StringPool::StringPool()
        : total_size_(0) {}

    uint32_t StringPool::getStringId(const std::string& str) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = string_to_id_.find(str);
        if (it != string_to_id_.end()) {
            return it->second;
        }

        // 新字符串，添加到池中
        uint32_t new_id = static_cast<uint32_t>(string_table_.size());
        string_to_id_[str] = new_id;
        string_table_.push_back(str);
        total_size_ += calculateStringSize(str);

        return new_id;
    }

    std::string StringPool::getString(uint32_t id) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (id < string_table_.size()) {
            return string_table_[id];
        }
        return "";
    }

    std::vector<uint8_t> StringPool::serialize() const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<uint8_t> data;

        // 写入字符串数量
        uint32_t count = static_cast<uint32_t>(string_table_.size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&count), reinterpret_cast<uint8_t*>(&count) + sizeof(uint32_t));

        // 写入每个字符串
        for (const auto& str : string_table_) {
            uint32_t length = static_cast<uint32_t>(str.length());
            data.insert(data.end(), reinterpret_cast<uint8_t*>(&length), reinterpret_cast<uint8_t*>(&length) + sizeof(uint32_t));
            data.insert(data.end(), str.begin(), str.end());
        }

        return data;
    }

    void StringPool::deserialize(const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(mutex_);

        clear();

        if (data.size() < sizeof(uint32_t)) {
            return;
        }

        size_t offset = 0;

        // 读取字符串数量
        uint32_t count;
        std::memcpy(&count, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // 读取每个字符串
        for (uint32_t i = 0; i < count; ++i) {
            if (offset + sizeof(uint32_t) > data.size()) {
                break;
            }

            uint32_t length;
            std::memcpy(&length, &data[offset], sizeof(uint32_t));
            offset += sizeof(uint32_t);

            if (offset + length > data.size()) {
                break;
            }

            std::string str(data.begin() + offset, data.begin() + offset + length);
            string_to_id_[str] = i;
            string_table_.push_back(str);
            total_size_ += calculateStringSize(str);

            offset += length;
        }
    }

    void StringPool::clear() {
        string_to_id_.clear();
        string_table_.clear();
        total_size_ = 0;
    }

    size_t StringPool::calculateStringSize(const std::string& str) const {
        // 计算字符串在池中的存储大小：长度(4) + 字符串内容
        return sizeof(uint32_t) + str.length();
    }

} // namespace GisStorage
