//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/string_pool.h"

#include <cstring>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <errno.h>

namespace GisStorage {

    StringPool::StringPool()
        : string_count_(0)
        , total_size_(0) {}

    StringPool::~StringPool() {
        CleanupMmap();
    }

    uint32_t StringPool::GetStringId(const std::string& str) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = string_to_id_.find(str);
        if (it != string_to_id_.end()) {
            return it->second;
        }

        // 新字符串，添加到池中
        uint32_t new_id = static_cast<uint32_t>(string_table_.size());
        string_to_id_[str] = new_id;
        string_table_.push_back(str);
        total_size_ += CalculateStringSize(str);

        return new_id;
    }

    std::string StringPool::GetString(uint32_t id) const {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. 先检查缓存
        auto cache_it = cache_.find(id);
        if (cache_it != cache_.end()) {
            // 更新LRU列表
            lru_list_.erase(cache_it->second.lru_it);
            lru_list_.push_front(cache_it->second);
            cache_it->second.lru_it = lru_list_.begin();
            return cache_it->second.value;
        }

        // 2. 根据模式选择读取方式
        if (use_mmap_mode_) {
            // 内存映射模式：从mmap中按需读取
            if (id < string_count_) {
                std::string result = ParseStringFromMmap(id);
                UpdateCache(id, result);
                return result;
            }
        } else {
            // 传统模式：从内存中读取
            if (id < string_table_.size()) {
                std::string result = string_table_[id];
                UpdateCache(id, result);
                return result;
            }
        }

        return "";
    }

    std::vector<uint8_t> StringPool::Serialize() const {
        std::lock_guard<std::mutex> lock(mutex_);

        // 预计算总大小以提高性能
        size_t total_size = sizeof(uint32_t); // 字符串数量
        for (const auto& str : string_table_) {
            total_size += sizeof(uint32_t) + str.length(); // 长度 + 字符串内容
        }

        std::vector<uint8_t> data;
        data.reserve(total_size);

        // 写入字符串数量
        uint32_t count = static_cast<uint32_t>(string_table_.size());
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&count), reinterpret_cast<const uint8_t*>(&count) + sizeof(uint32_t));

        // 写入每个字符串
        for (const auto& str : string_table_) {
            uint32_t length = static_cast<uint32_t>(str.length());
            data.insert(data.end(), reinterpret_cast<const uint8_t*>(&length), reinterpret_cast<const uint8_t*>(&length) + sizeof(uint32_t));
            data.insert(data.end(), str.begin(), str.end());
        }

        return data;
    }

    void StringPool::Deserialize(const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(mutex_);

        Clear();

        if (data.size() < sizeof(uint32_t)) {
            return;
        }

        size_t offset = 0;

        // 读取字符串数量
        uint32_t count;
        std::memcpy(&count, &data[offset], sizeof(uint32_t));
        offset += sizeof(uint32_t);

        string_count_ = count;

        // 预分配内存以提高性能
        string_table_.reserve(count);
        string_to_id_.reserve(count);
        string_offsets_.reserve(count);

        // 读取每个字符串
        for (uint32_t i = 0; i < count; ++i) {
            if (offset + sizeof(uint32_t) > data.size()) {
                break;
            }

            // 记录字符串的偏移量
            string_offsets_.push_back(offset);

            uint32_t length;
            std::memcpy(&length, &data[offset], sizeof(uint32_t));
            offset += sizeof(uint32_t);

            if (offset + length > data.size()) {
                break;
            }

            // 使用更高效的字符串构造
            std::string str;
            str.reserve(length);
            str.assign(reinterpret_cast<const char*>(&data[offset]), length);

            string_to_id_[str] = i;
            string_table_.push_back(std::move(str));
            total_size_ += sizeof(uint32_t) + length;

            offset += length;
        }
    }

    void StringPool::Clear() {
        CleanupMmap();
        string_to_id_.clear();
        string_table_.clear();
        string_offsets_.clear();
        cache_.clear();
        lru_list_.clear();
        total_size_ = 0;
        string_count_ = 0;
        use_mmap_mode_ = false;
    }

    size_t StringPool::CalculateStringSize(const std::string& str) const {
        // 计算字符串在池中的存储大小：长度(4) + 字符串内容
        return sizeof(uint32_t) + str.length();
    }

    bool StringPool::LoadFromFile(const std::string& file_path) {
        std::lock_guard<std::mutex> lock(mutex_);

        CleanupMmap();

        // 打开文件
        pool_fd_ = open(file_path.c_str(), O_RDONLY);
        if (pool_fd_ < 0) {
            std::cerr << "无法打开字符串池文件: " << file_path << " (错误: " << strerror(errno) << ")" << std::endl;
            return false;
        }

        // 获取文件大小
        struct stat st;
        if (fstat(pool_fd_, &st) < 0) {
            std::cerr << "无法获取字符串池文件大小: " << file_path << " (错误: " << strerror(errno) << ")" << std::endl;
            close(pool_fd_);
            pool_fd_ = -1;
            return false;
        }
        pool_size_ = st.st_size;

        if (pool_size_ == 0) {
            std::cerr << "字符串池文件为空: " << file_path << std::endl;
            close(pool_fd_);
            pool_fd_ = -1;
            return false;
        }

        // 内存映射
        pool_mmap_ = static_cast<char*>(mmap(nullptr, pool_size_, PROT_READ, MAP_PRIVATE, pool_fd_, 0));
        if (pool_mmap_ == MAP_FAILED) {
            std::cerr << "无法映射字符串池文件: " << file_path << " (错误: " << strerror(errno) << ")" << std::endl;
            close(pool_fd_);
            pool_fd_ = -1;
            pool_mmap_ = nullptr;
            return false;
        }

        // 设置访问建议 - 使用MADV_RANDOM因为字符串访问模式是随机的
        if (madvise(pool_mmap_, pool_size_, MADV_RANDOM) != 0) {
            std::cerr << "警告: 无法设置内存访问建议: " << strerror(errno) << std::endl;
        }

        // 读取字符串数量
        if (pool_size_ >= sizeof(uint32_t)) {
            std::memcpy(&string_count_, pool_mmap_, sizeof(uint32_t));
            std::cout << "字符串池mmap成功: " << file_path << " (大小: " << pool_size_ << " 字节, 字符串数: " << string_count_ << ")" << std::endl;
        } else {
            std::cerr << "字符串池文件格式错误: " << file_path << " (文件太小)" << std::endl;
            CleanupMmap();
            return false;
        }

        // 构建字符串偏移量索引
        BuildStringOffsetsIndex();

        use_mmap_mode_ = true;
        return true;
    }

    bool StringPool::SaveToFile(const std::string& file_path) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (use_mmap_mode_) {
            return false;
        } else {
            // 传统模式下，序列化到文件
            auto data = Serialize();
            std::ofstream file(file_path, std::ios::binary);
            if (!file) {
                return false;
            }
            file.write(reinterpret_cast<const char*>(data.data()), data.size());
            return file.good();
        }
    }

    std::string StringPool::ParseStringFromMmap(uint32_t id) const {
        if (!pool_mmap_ || id >= string_count_ || id >= string_offsets_.size()) {
            return "";
        }

        // 使用预计算的偏移量索引，实现O(1)查找
        size_t offset = string_offsets_[id];

        // 检查偏移量是否在有效范围内
        if (offset + sizeof(uint32_t) > pool_size_) {
            std::cerr << "警告: 字符串池mmap数据损坏，偏移超出范围 (id=" << id << ", offset=" << offset << ", pool_size=" << pool_size_ << ")" << std::endl;
            return "";
        }

        // 读取字符串长度
        uint32_t length;
        std::memcpy(&length, pool_mmap_ + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // 检查长度是否合理
        if (length > pool_size_ || offset + length > pool_size_) {
            std::cerr << "警告: 字符串长度异常 (id=" << id << ", length=" << length << ", pool_size=" << pool_size_ << ")" << std::endl;
            return "";
        }

        return std::string(pool_mmap_ + offset, length);
    }

    void StringPool::BuildStringOffsetsIndex() {
        if (!pool_mmap_ || string_count_ == 0) {
            return;
        }

        // 清空现有的偏移量索引
        string_offsets_.clear();
        string_offsets_.reserve(string_count_);

        size_t offset = sizeof(uint32_t); // 跳过字符串数量字段

        // 遍历所有字符串，构建偏移量索引
        for (uint32_t i = 0; i < string_count_; ++i) {
            if (offset + sizeof(uint32_t) > pool_size_) {
                std::cerr << "警告: 字符串池数据损坏，偏移超出范围 (i=" << i << ", offset=" << offset << ", pool_size=" << pool_size_ << ")" << std::endl;
                break;
            }

            // 记录当前字符串的偏移量
            string_offsets_.push_back(offset);

            // 读取字符串长度
            uint32_t length;
            std::memcpy(&length, pool_mmap_ + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            // 跳过字符串内容
            if (offset + length > pool_size_) {
                std::cerr << "警告: 字符串长度异常 (i=" << i << ", length=" << length << ", offset=" << offset << ", pool_size=" << pool_size_ << ")" << std::endl;
                break;
            }
            offset += length;
        }

        std::cout << "字符串偏移量索引构建完成: " << string_offsets_.size() << " 个字符串" << std::endl;
    }

    void StringPool::UpdateCache(uint32_t id, const std::string& value) const {
        // 如果缓存已满，移除最久未使用的条目
        if (cache_.size() >= max_cache_size_) {
            auto lru_entry = lru_list_.back();
            cache_.erase(lru_entry.id);
            lru_list_.pop_back();
        }

        // 添加新条目到缓存
        CacheEntry entry{id, value, lru_list_.end()};
        lru_list_.push_front(entry);
        entry.lru_it = lru_list_.begin();
        cache_[id] = entry;
    }

    void StringPool::CleanupMmap() {
        if (pool_mmap_ && pool_mmap_ != MAP_FAILED) {
            munmap(pool_mmap_, pool_size_);
            pool_mmap_ = nullptr;
        }
        if (pool_fd_ >= 0) {
            close(pool_fd_);
            pool_fd_ = -1;
        }
        pool_size_ = 0;
    }

} // namespace GisStorage
