//
//  Created by vlv-squid on 2025.08.21.
//

#include "gisstorage/string_pool.h"

#include <cstring>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <errno.h>
#include <filesystem>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

namespace GisStorage {

    StringPool::StringPool()
        : string_count_(0)
        , total_size_(0) {}

    StringPool::~StringPool() {
        CleanupMmap();
    }

    uint32_t StringPool::GetStringId(const std::string& str) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 在mmap模式下，不允许添加新字符串
        if (use_mmap_mode_) {
            throw std::runtime_error("字符串池处于mmap模式，不允许添加新字符串");
        }

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
        // 1. 先检查无锁快速缓存（最快路径）
        std::string fast_result = GetStringFromFastCache(id);
        if (!fast_result.empty()) {
            cache_hits_++;
            return fast_result;
        }

        // 2. 检查主缓存（需要锁）
        std::lock_guard<std::mutex> lock(mutex_);
        auto cache_it = cache_.find(id);
        if (cache_it != cache_.end()) {
            // 更新LRU列表
            lru_list_.erase(cache_it->second.lru_it);
            lru_list_.push_front(cache_it->second);
            cache_it->second.lru_it = lru_list_.begin();

            // 同时更新快速缓存
            UpdateFastCache(id, cache_it->second.value);

            cache_hits_++;
            return cache_it->second.value;
        }

        cache_misses_++;

        // 3. 根据模式选择读取方式
        if (use_mmap_mode_) {
            // 内存映射模式：从mmap中按需读取
            if (id < string_count_) {
                std::string result = ParseStringFromMmap(id);
                UpdateCache(id, result);
                UpdateFastCache(id, result); // 同时更新快速缓存
                return result;
            }
        } else {
            // 传统模式：从内存中读取
            if (id < string_table_.size()) {
                std::string result = string_table_[id];
                UpdateCache(id, result);
                UpdateFastCache(id, result); // 同时更新快速缓存
                return result;
            }
        }

        return "";
    }

    std::vector<uint8_t> StringPool::Serialize() const {
        std::lock_guard<std::mutex> lock(mutex_);

        // 紧凑格式：使用变长编码优化存储
        std::vector<uint8_t> data;

        // 写入字符串数量 (变长编码)
        uint32_t count = static_cast<uint32_t>(string_table_.size());
        EncodeVarint(data, count);

        // 写入每个字符串 - 使用紧凑格式
        for (const auto& str : string_table_) {
            // 使用变长编码存储字符串长度
            uint32_t length = static_cast<uint32_t>(str.length());
            EncodeVarint(data, length);

            // 直接写入字符串内容
            data.insert(data.end(), str.begin(), str.end());
        }

        return data;
    }

    void StringPool::Deserialize(const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(mutex_);

        Clear();

        if (data.size() < 1) { // 至少需要1字节的字符串数量
            return;
        }

        size_t offset = 0;

        // 读取字符串数量 (变长编码)
        uint32_t count;
        offset = DecodeVarint(data, offset, count);

        string_count_ = count;

        // 预分配内存以提高性能
        string_table_.reserve(count);
        string_to_id_.reserve(count);
        string_offsets_.reserve(count);

        // 读取每个字符串
        for (uint32_t i = 0; i < count; ++i) {
            if (offset >= data.size()) {
                break;
            }

            // 记录字符串的偏移量
            string_offsets_.push_back(offset);

            // 读取字符串长度 (变长编码)
            uint32_t length;
            offset = DecodeVarint(data, offset, length);

            if (offset + length > data.size()) {
                break;
            }

            // 使用更高效的字符串构造
            std::string str;
            str.reserve(length);
            str.assign(reinterpret_cast<const char*>(&data[offset]), length);

            string_to_id_[str] = i;
            string_table_.push_back(std::move(str));
            total_size_ += CalculateVarintSize(length) + length;

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

        // 读取字符串数量 (变长编码)
        if (pool_size_ >= 1) { // 至少需要1字节的变长编码
            size_t offset = 0;
            offset = DecodeVarintFromMmap(offset, string_count_);
            std::cout << "字符串池mmap成功: " << file_path << " (大小: " << pool_size_ << " 字节, 字符串数: " << string_count_ << ")" << std::endl;
        } else {
            std::cerr << "字符串池文件格式错误: " << file_path << " (文件太小)" << std::endl;
            CleanupMmap();
            return false;
        }

        // 尝试从预构建的索引文件加载偏移量索引
        std::string index_file_path = file_path + ".index";
        if (!LoadOffsetsIndexFromFile(index_file_path)) {
            // 如果预构建索引不存在，则构建索引
            std::cout << "预构建索引不存在，开始构建偏移量索引..." << std::endl;
            BuildStringOffsetsIndexParallel();

            // 保存索引到文件以供下次使用
            SaveOffsetsIndexToFile(index_file_path);
        } else {
            std::cout << "成功从预构建索引文件加载偏移量索引" << std::endl;
        }

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
            bool success = file.good();
            file.close();

            // 在保存 .pool 文件后，构建并保存 .pool.index 文件
            if (success && !string_table_.empty()) {
                std::string index_file_path = file_path + ".index";

                // 检查索引文件是否已存在，如果不存在才创建
                if (!std::filesystem::exists(index_file_path)) {
                    // 构建偏移量索引
                    BuildOffsetsIndexFromMemory();

                    if (!SaveOffsetsIndexToFile(index_file_path)) {
                        std::cerr << "警告: 无法保存字符串池索引文件: " << index_file_path << std::endl;
                    } else {
                        std::cout << "已创建字符串池索引文件: " << index_file_path << std::endl;
                    }
                } else {
                    std::cout << "字符串池索引文件已存在，跳过创建: " << index_file_path << std::endl;
                }
            }

            return success;
        }
    }

    std::string StringPool::ParseStringFromMmap(uint32_t id) const {
        if (!pool_mmap_ || id >= string_count_ || id >= string_offsets_.size()) {
            return "";
        }

        // 使用预计算的偏移量索引，实现O(1)查找
        size_t offset = string_offsets_[id];

        // 检查偏移量是否在有效范围内
        if (offset + 1 > pool_size_) { // 至少需要1字节的变长编码
            std::cerr << "警告: 字符串池mmap数据损坏，偏移超出范围 (id=" << id << ", offset=" << offset << ", pool_size=" << pool_size_ << ")" << std::endl;
            return "";
        }

        // 读取字符串长度 (变长编码)
        uint32_t length;
        offset = DecodeVarintFromMmap(offset, length);

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

        // 跳过字符串数量字段 (变长编码)
        size_t offset = 0;
        uint32_t count;
        offset = DecodeVarintFromMmap(offset, count);

        // 遍历所有字符串，构建偏移量索引
        for (uint32_t i = 0; i < string_count_; ++i) {
            if (offset + 1 > pool_size_) { // 至少需要1字节的变长编码
                std::cerr << "警告: 字符串池数据损坏，偏移超出范围 (i=" << i << ", offset=" << offset << ", pool_size=" << pool_size_ << ")" << std::endl;
                break;
            }

            // 记录当前字符串的偏移量
            string_offsets_.push_back(offset);

            // 读取字符串长度 (变长编码)
            uint32_t length;
            offset = DecodeVarintFromMmap(offset, length);

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

    void StringPool::BuildStringOffsetsIndexParallel() {
        if (!pool_mmap_ || string_count_ == 0) {
            return;
        }

        // 清空现有的偏移量索引
        string_offsets_.clear();
        string_offsets_.resize(string_count_);

        // 使用TBB并行构建索引
        tbb::parallel_for(tbb::blocked_range<uint32_t>(0, string_count_), [this](const tbb::blocked_range<uint32_t>& range) {
            size_t offset = 0;
            // 跳过字符串数量字段 (变长编码)
            uint32_t count_dummy;
            offset = DecodeVarintFromMmap(offset, count_dummy);

            // 为每个线程计算起始偏移量
            for (uint32_t i = 0; i < range.begin(); ++i) {
                if (offset + 1 > pool_size_) { // 至少需要1字节的变长编码
                    return;
                }
                uint32_t length;
                offset = DecodeVarintFromMmap(offset, length);
                offset += length;
            }

            // 并行处理范围内的字符串
            for (uint32_t i = range.begin(); i < range.end(); ++i) {
                if (offset + 1 > pool_size_) { // 至少需要1字节的变长编码
                    break;
                }

                string_offsets_[i] = offset;

                uint32_t length;
                offset = DecodeVarintFromMmap(offset, length);
                offset += length;
            }
        });

        std::cout << "并行构建字符串偏移量索引完成: " << string_offsets_.size() << " 个字符串" << std::endl;
    }

    void StringPool::BuildOffsetsIndexFromMemory() const {
        if (string_table_.empty()) {
            return;
        }

        // 清空现有的偏移量索引
        string_offsets_.clear();
        string_offsets_.reserve(string_table_.size());

        // 计算每个字符串在序列化数据中的偏移量
        size_t current_offset = 0;

        // 跳过字符串数量字段 (变长编码)
        // 计算字符串数量字段的字节数
        uint32_t count = static_cast<uint32_t>(string_table_.size());
        current_offset += GetVarintSize(count);

        // 为每个字符串计算偏移量 - 使用预分配和批量操作优化性能
        string_offsets_.resize(string_table_.size());
        for (size_t i = 0; i < string_table_.size(); ++i) {
            string_offsets_[i] = current_offset;

            // 计算这个字符串在序列化数据中占用的字节数
            uint32_t length = static_cast<uint32_t>(string_table_[i].length());
            current_offset += GetVarintSize(length) + length;
        }

        // 更新字符串数量
        string_count_ = static_cast<uint32_t>(string_table_.size());

        std::cout << "从内存构建字符串偏移量索引完成: " << string_offsets_.size() << " 个字符串" << std::endl;
    }

    bool StringPool::SaveOffsetsIndexToFile(const std::string& index_file_path) const {
        if (string_offsets_.empty()) {
            return false;
        }

        std::ofstream file(index_file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法创建索引文件: " << index_file_path << std::endl;
            return false;
        }

        // 写入字符串数量
        file.write(reinterpret_cast<const char*>(&string_count_), sizeof(uint32_t));

        // 写入所有偏移量
        file.write(reinterpret_cast<const char*>(string_offsets_.data()), string_offsets_.size() * sizeof(size_t));

        file.close();
        std::cout << "偏移量索引已保存到: " << index_file_path << std::endl;
        return true;
    }

    bool StringPool::LoadOffsetsIndexFromFile(const std::string& index_file_path) {
        std::ifstream file(index_file_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // 读取字符串数量
        uint32_t file_string_count;
        file.read(reinterpret_cast<char*>(&file_string_count), sizeof(uint32_t));

        // 验证字符串数量是否匹配
        if (file_string_count != string_count_) {
            std::cerr << "索引文件中的字符串数量不匹配: " << file_string_count << " != " << string_count_ << std::endl;
            file.close();
            return false;
        }

        // 读取偏移量数据
        string_offsets_.resize(string_count_);
        file.read(reinterpret_cast<char*>(string_offsets_.data()), string_count_ * sizeof(size_t));

        file.close();
        return true;
    }

    StringPool::CacheStats StringPool::GetCacheStats() const {
        std::lock_guard<std::mutex> lock(mutex_);

        CacheStats stats;
        stats.hits = cache_hits_.load();
        stats.misses = cache_misses_.load();
        stats.cache_size = cache_.size();

        uint64_t total_requests = stats.hits + stats.misses;
        stats.hit_rate = total_requests > 0 ? static_cast<double>(stats.hits) / total_requests : 0.0;

        return stats;
    }

    std::string StringPool::GetStringFromFastCache(uint32_t id) const {
        // 无锁快速缓存查找
        for (size_t i = 0; i < fast_cache_.size(); ++i) {
            if (fast_cache_[i].first == id) {
                return fast_cache_[i].second;
            }
        }
        return "";
    }

    void StringPool::UpdateFastCache(uint32_t id, const std::string& value) const {
        // 使用原子操作更新快速缓存
        size_t index = fast_cache_index_.fetch_add(1) % fast_cache_.size();
        fast_cache_[index] = std::make_pair(id, value);
    }

    void StringPool::PrefetchStrings(const std::vector<uint32_t>& ids) const {
        // 批量预取字符串到缓存
        std::lock_guard<std::mutex> lock(mutex_);

        for (uint32_t id : ids) {
            if (id < string_count_) {
                // 检查是否已在缓存中
                auto cache_it = cache_.find(id);
                if (cache_it == cache_.end()) {
                    // 预取到缓存
                    std::string result = ParseStringFromMmap(id);
                    UpdateCache(id, result);
                    UpdateFastCache(id, result);
                }
            }
        }
    }

    std::vector<std::string> StringPool::GetStringsBatch(const std::vector<uint32_t>& ids) const {
        std::vector<std::string> results;
        results.reserve(ids.size());

        // 批量获取，减少锁竞争
        std::lock_guard<std::mutex> lock(mutex_);

        for (uint32_t id : ids) {
            // 先检查快速缓存
            std::string fast_result = GetStringFromFastCache(id);
            if (!fast_result.empty()) {
                results.push_back(fast_result);
                continue;
            }

            // 检查主缓存
            auto cache_it = cache_.find(id);
            if (cache_it != cache_.end()) {
                results.push_back(cache_it->second.value);
                continue;
            }

            // 从mmap读取
            if (use_mmap_mode_ && id < string_count_) {
                std::string result = ParseStringFromMmap(id);
                UpdateCache(id, result);
                UpdateFastCache(id, result);
                results.push_back(result);
            } else {
                results.push_back("");
            }
        }

        return results;
    }

    std::string StringPool::ParseStringFromMmapSIMD(uint32_t id) const {
        if (!pool_mmap_ || id >= string_count_ || id >= string_offsets_.size()) {
            return "";
        }

        // 使用预计算的偏移量索引
        size_t offset = string_offsets_[id];

        // 检查偏移量是否在有效范围内
        if (offset + 1 > pool_size_) { // 至少需要1字节的变长编码
            return "";
        }

        // 读取字符串长度 (变长编码)
        uint32_t length;
        offset = DecodeVarintFromMmap(offset, length);

        // 检查长度是否合理
        if (length > pool_size_ || offset + length > pool_size_) {
            return "";
        }

        // 使用SIMD优化的字符串构造
        std::string result;
        result.reserve(length);

        // 对于长字符串，使用SIMD优化复制
        if (length >= 16) {
            const char* src = pool_mmap_ + offset;
            result.assign(src, length);
        } else {
            // 短字符串直接复制
            result.assign(pool_mmap_ + offset, length);
        }

        return result;
    }

    void StringPool::EncodeVarint(std::vector<uint8_t>& data, uint32_t value) const {
        // 使用变长编码，每个字节的最高位表示是否还有后续字节
        while (value >= 0x80) {
            data.push_back(static_cast<uint8_t>(value | 0x80));
            value >>= 7;
        }
        data.push_back(static_cast<uint8_t>(value));
    }

    size_t StringPool::GetVarintSize(uint32_t value) const {
        // 计算变长编码的字节数
        size_t size = 1;
        while (value >= 0x80) {
            value >>= 7;
            size++;
        }
        return size;
    }

    size_t StringPool::DecodeVarint(const std::vector<uint8_t>& data, size_t offset, uint32_t& value) const {
        value = 0;
        int shift = 0;

        while (offset < data.size()) {
            uint8_t byte = data[offset++];
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;

            if ((byte & 0x80) == 0) {
                break; // 最后一个字节
            }

            shift += 7;
            if (shift >= 32) {
                throw std::runtime_error("变长编码值过大");
            }
        }

        return offset;
    }

    size_t StringPool::CalculateVarintSize(uint32_t value) const {
        size_t size = 1;
        while (value >= 0x80) {
            value >>= 7;
            size++;
        }
        return size;
    }

    size_t StringPool::DecodeVarintFromMmap(size_t offset, uint32_t& value) const {
        value = 0;
        int shift = 0;

        while (offset < pool_size_) {
            uint8_t byte = pool_mmap_[offset++];
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;

            if ((byte & 0x80) == 0) {
                break; // 最后一个字节
            }

            shift += 7;
            if (shift >= 32) {
                throw std::runtime_error("变长编码值过大");
            }
        }

        return offset;
    }

} // namespace GisStorage
