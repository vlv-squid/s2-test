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
    namespace {
        struct SharedMmapInfo {
            char* mmap_ptr;
            size_t mmap_size;
            uint32_t string_count;
            std::shared_ptr<const std::vector<size_t>> string_offsets;
            int ref_count;

            SharedMmapInfo()
                : mmap_ptr(nullptr)
                , mmap_size(0)
                , string_count(0)
                , ref_count(0) {}
            ~SharedMmapInfo() {
                if (mmap_ptr != nullptr) {
                    munmap(mmap_ptr, mmap_size);
                    mmap_ptr = nullptr;
                }
            }
        };

        std::mutex g_pool_mmap_cache_mutex;
        std::unordered_map<std::string, std::weak_ptr<SharedMmapInfo>> g_pool_mmap_cache;
    } // namespace

    StringPool::StringPool()
        : string_count_(0)
        , total_size_(0) {}

    StringPool::~StringPool() {
        CleanupMmap();
    }

    uint32_t StringPool::GetStringId(const std::string& str) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (use_mmap_mode_) {
            throw std::runtime_error("字符串池处于mmap模式，不允许添加新字符串");
        }

        auto it = string_to_id_.find(str);
        if (it != string_to_id_.end()) {
            return it->second;
        }

        uint32_t new_id = static_cast<uint32_t>(string_table_.size());
        string_to_id_[str] = new_id;
        string_table_.push_back(str);
        string_count_ = static_cast<uint32_t>(string_table_.size());
        total_size_ += CalculateStringSize(str);

        return new_id;
    }

    std::string StringPool::GetString(uint32_t id) const {
        std::string fast_result = GetStringFromFastCache(id);
        if (!fast_result.empty()) {
            cache_hits_++;
            return fast_result;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto cache_it = cache_.find(id);
        if (cache_it != cache_.end()) {
            lru_list_.erase(cache_it->second.lru_it);
            lru_list_.push_front(cache_it->second);
            cache_it->second.lru_it = lru_list_.begin();

            UpdateFastCache(id, cache_it->second.value);

            cache_hits_++;
            return cache_it->second.value;
        }

        cache_misses_++;

        if (use_mmap_mode_) {
            if (id < string_count_) {
                std::string result = ParseStringFromMmap(id);
                UpdateCache(id, result);
                UpdateFastCache(id, result);
                return result;
            }
        } else {
            if (id < string_table_.size()) {
                std::string result = string_table_[id];
                UpdateCache(id, result);
                UpdateFastCache(id, result);
                return result;
            }
        }

        return "";
    }

    std::vector<uint8_t> StringPool::Serialize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<uint8_t> data;
        uint32_t count = static_cast<uint32_t>(string_table_.size());
        EncodeVarint(data, count);
        for (const auto& str : string_table_) {
            uint32_t length = static_cast<uint32_t>(str.length());
            EncodeVarint(data, length);
            data.insert(data.end(), str.begin(), str.end());
        }

        return data;
    }

    void StringPool::Deserialize(const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(mutex_);

        Clear();

        if (data.size() < 1) {
            return;
        }

        size_t offset = 0;
        uint32_t count;
        offset = DecodeVarint(data, offset, count);
        string_count_ = count;
        string_table_.reserve(count);
        string_to_id_.reserve(count);
        string_offsets_.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (offset >= data.size()) {
                break;
            }

            string_offsets_.push_back(offset);
            uint32_t length;
            offset = DecodeVarint(data, offset, length);
            if (offset + length > data.size()) {
                break;
            }

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
        return sizeof(uint32_t) + str.length();
    }

    bool StringPool::LoadFromFile(const std::string& file_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        CleanupMmap();
        static std::mutex s_pool_shared_storage_mutex;
        static std::unordered_map<std::string, std::shared_ptr<SharedMmapInfo>> s_pool_shared_storage;
        {
            std::lock_guard<std::mutex> storage_lk(s_pool_shared_storage_mutex);
            auto storage_it = s_pool_shared_storage.find(file_path);
            if (storage_it != s_pool_shared_storage.end()) {
                auto shared_mmap = storage_it->second;
                pool_mmap_ = shared_mmap->mmap_ptr;
                pool_size_ = shared_mmap->mmap_size;
                string_count_ = shared_mmap->string_count;
                shared_string_offsets_ = shared_mmap->string_offsets;
                use_mmap_mode_ = true;
                shared_mmap_info_ = std::static_pointer_cast<void>(shared_mmap);
                {
                    std::lock_guard<std::mutex> cache_lock(g_pool_mmap_cache_mutex);
                    g_pool_mmap_cache[file_path] = shared_mmap;
                }

                return true;
            }
        }

        std::shared_ptr<SharedMmapInfo> shared_mmap;
        {
            std::lock_guard<std::mutex> cache_lock(g_pool_mmap_cache_mutex);
            auto it = g_pool_mmap_cache.find(file_path);
            if (it != g_pool_mmap_cache.end()) {
                shared_mmap = it->second.lock();
                if (shared_mmap) {
                    pool_mmap_ = shared_mmap->mmap_ptr;
                    pool_size_ = shared_mmap->mmap_size;
                    string_count_ = shared_mmap->string_count;
                    shared_string_offsets_ = shared_mmap->string_offsets;
                    use_mmap_mode_ = true;
                    shared_mmap_info_ = std::static_pointer_cast<void>(shared_mmap);
                    {
                        std::lock_guard<std::mutex> storage_lk(s_pool_shared_storage_mutex);
                        s_pool_shared_storage[file_path] = shared_mmap;
                    }

                    return true;
                }
            }
        }

        std::cout << "[首次加载] 开始加载字符串池mmap: " << file_path << std::endl;
        pool_fd_ = open(file_path.c_str(), O_RDONLY);
        if (pool_fd_ < 0) {
            std::cerr << "无法打开字符串池文件: " << file_path << " (错误: " << strerror(errno) << ")" << std::endl;
            return false;
        }

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

        pool_mmap_ = static_cast<char*>(mmap(nullptr, pool_size_, PROT_READ, MAP_PRIVATE, pool_fd_, 0));
        if (pool_mmap_ == MAP_FAILED) {
            std::cerr << "无法映射字符串池文件: " << file_path << " (错误: " << strerror(errno) << ")" << std::endl;
            close(pool_fd_);
            pool_fd_ = -1;
            pool_mmap_ = nullptr;
            return false;
        }

        if (madvise(pool_mmap_, pool_size_, MADV_RANDOM) != 0) {
            std::cerr << "警告: 无法设置内存访问建议: " << strerror(errno) << std::endl;
        }

        if (pool_size_ >= 1) {
            size_t offset = 0;
            offset = DecodeVarintFromMmap(offset, string_count_);
            std::cout << "[首次加载] 字符串池mmap成功: " << file_path << " (大小: " << pool_size_ << " 字节, 字符串数: " << string_count_ << ")" << std::endl;
        } else {
            std::cerr << "字符串池文件格式错误: " << file_path << " (文件太小)" << std::endl;
            CleanupMmap();
            return false;
        }

        std::string index_file_path = file_path + ".index";
        if (!LoadOffsetsIndexFromFile(index_file_path)) {
            std::cout << "[首次加载] 预构建索引不存在，开始构建偏移量索引..." << std::endl;
            BuildStringOffsetsIndexParallel();
            SaveOffsetsIndexToFile(index_file_path);
        } else {
            std::cout << "[首次加载] 成功从预构建索引文件加载偏移量索引" << std::endl;
        }

        auto shared_offsets = std::make_shared<const std::vector<size_t>>(std::move(string_offsets_));
        shared_mmap = std::make_shared<SharedMmapInfo>();
        shared_mmap->mmap_ptr = pool_mmap_;
        shared_mmap->mmap_size = pool_size_;
        shared_mmap->string_count = string_count_;
        shared_mmap->string_offsets = shared_offsets;
        shared_string_offsets_ = shared_offsets;
        shared_mmap_info_ = std::static_pointer_cast<void>(shared_mmap);
        {
            std::lock_guard<std::mutex> storage_lk(s_pool_shared_storage_mutex);
            s_pool_shared_storage[file_path] = shared_mmap;
        }
        {
            std::lock_guard<std::mutex> cache_lock(g_pool_mmap_cache_mutex);
            g_pool_mmap_cache[file_path] = shared_mmap;
        }

        use_mmap_mode_ = true;
        return true;
    }

    bool StringPool::SaveToFile(const std::string& file_path) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (use_mmap_mode_) {
            return false;
        } else {
            auto data = Serialize();
            std::ofstream file(file_path, std::ios::binary);
            if (!file) {
                return false;
            }
            file.write(reinterpret_cast<const char*>(data.data()), data.size());
            bool success = file.good();
            file.close();

            if (success && !string_table_.empty()) {
                std::string index_file_path = file_path + ".index";
                if (!std::filesystem::exists(index_file_path)) {
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
        const std::vector<size_t>* offsets = nullptr;
        if (shared_string_offsets_) {
            offsets = shared_string_offsets_.get();
        } else if (!string_offsets_.empty()) {
            offsets = &string_offsets_;
        }

        if (!pool_mmap_ || !offsets || id >= string_count_ || id >= offsets->size()) {
            return "";
        }

        size_t offset = (*offsets)[id];
        if (offset + 1 > pool_size_) {
            std::cerr << "警告: 字符串池mmap数据损坏，偏移超出范围 (id=" << id << ", offset=" << offset << ", pool_size=" << pool_size_ << ")" << std::endl;
            return "";
        }

        uint32_t length;
        offset = DecodeVarintFromMmap(offset, length);

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

        string_offsets_.clear();
        string_offsets_.reserve(string_count_);
        size_t offset = 0;
        uint32_t count;
        offset = DecodeVarintFromMmap(offset, count);
        for (uint32_t i = 0; i < string_count_; ++i) {
            if (offset + 1 > pool_size_) {
                std::cerr << "警告: 字符串池数据损坏，偏移超出范围 (i=" << i << ", offset=" << offset << ", pool_size=" << pool_size_ << ")" << std::endl;
                break;
            }

            string_offsets_.push_back(offset);
            uint32_t length;
            offset = DecodeVarintFromMmap(offset, length);
            if (offset + length > pool_size_) {
                std::cerr << "警告: 字符串长度异常 (i=" << i << ", length=" << length << ", offset=" << offset << ", pool_size=" << pool_size_ << ")" << std::endl;
                break;
            }
            offset += length;
        }

        std::cout << "字符串偏移量索引构建完成: " << string_offsets_.size() << " 个字符串" << std::endl;
    }

    void StringPool::UpdateCache(uint32_t id, const std::string& value) const {
        if (cache_.size() >= max_cache_size_) {
            auto lru_entry = lru_list_.back();
            cache_.erase(lru_entry.id);
            lru_list_.pop_back();
        }

        CacheEntry entry{id, value, lru_list_.end()};
        lru_list_.push_front(entry);
        entry.lru_it = lru_list_.begin();
        cache_[id] = entry;
    }

    void StringPool::CleanupMmap() {
        if (shared_mmap_info_) {
            shared_mmap_info_.reset();
            pool_mmap_ = nullptr;
            pool_size_ = 0;
            string_count_ = 0;
            string_offsets_.clear();
            use_mmap_mode_ = false;
            if (pool_fd_ >= 0) {
                close(pool_fd_);
                pool_fd_ = -1;
            }
            return;
        }

        if (pool_mmap_ && pool_mmap_ != MAP_FAILED) {
            munmap(pool_mmap_, pool_size_);
            pool_mmap_ = nullptr;
        }
        if (pool_fd_ >= 0) {
            close(pool_fd_);
            pool_fd_ = -1;
        }
        pool_size_ = 0;
        string_count_ = 0;
        shared_string_offsets_.reset();
        string_offsets_.clear();
        use_mmap_mode_ = false;
    }

    void StringPool::BuildStringOffsetsIndexParallel() {
        if (!pool_mmap_ || string_count_ == 0) {
            return;
        }

        string_offsets_.clear();
        string_offsets_.resize(string_count_);
        tbb::parallel_for(tbb::blocked_range<uint32_t>(0, string_count_), [this](const tbb::blocked_range<uint32_t>& range) {
            size_t offset = 0;
            uint32_t count_dummy;
            offset = DecodeVarintFromMmap(offset, count_dummy);
            for (uint32_t i = 0; i < range.begin(); ++i) {
                if (offset + 1 > pool_size_) {
                    return;
                }
                uint32_t length;
                offset = DecodeVarintFromMmap(offset, length);
                offset += length;
            }

            for (uint32_t i = range.begin(); i < range.end(); ++i) {
                if (offset + 1 > pool_size_) {
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

        string_offsets_.clear();
        string_offsets_.reserve(string_table_.size());
        size_t current_offset = 0;
        uint32_t count = static_cast<uint32_t>(string_table_.size());
        current_offset += GetVarintSize(count);
        string_offsets_.resize(string_table_.size());
        for (size_t i = 0; i < string_table_.size(); ++i) {
            string_offsets_[i] = current_offset;
            uint32_t length = static_cast<uint32_t>(string_table_[i].length());
            current_offset += GetVarintSize(length) + length;
        }

        string_count_ = static_cast<uint32_t>(string_table_.size());
        std::cout << "从内存构建字符串偏移量索引完成: " << string_offsets_.size() << " 个字符串" << std::endl;
    }

    bool StringPool::SaveOffsetsIndexToFile(const std::string& index_file_path) const {
        const std::vector<size_t>* offsets = nullptr;
        if (shared_string_offsets_) {
            offsets = shared_string_offsets_.get();
        } else if (!string_offsets_.empty()) {
            offsets = &string_offsets_;
        }

        if (!offsets || offsets->empty()) {
            return false;
        }

        std::ofstream file(index_file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法创建索引文件: " << index_file_path << std::endl;
            return false;
        }
        file.write(reinterpret_cast<const char*>(&string_count_), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(offsets->data()), offsets->size() * sizeof(size_t));
        file.close();
        std::cout << "偏移量索引已保存到: " << index_file_path << std::endl;
        return true;
    }

    bool StringPool::LoadOffsetsIndexFromFile(const std::string& index_file_path) {
        std::ifstream file(index_file_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        uint32_t file_string_count;
        file.read(reinterpret_cast<char*>(&file_string_count), sizeof(uint32_t));
        if (file_string_count != string_count_) {
            std::cerr << "索引文件中的字符串数量不匹配: " << file_string_count << " != " << string_count_ << std::endl;
            file.close();
            return false;
        }

        std::vector<size_t> temp_offsets(string_count_);
        file.read(reinterpret_cast<char*>(temp_offsets.data()), string_count_ * sizeof(size_t));
        file.close();

        string_offsets_ = std::move(temp_offsets);
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
        for (size_t i = 0; i < fast_cache_.size(); ++i) {
            if (fast_cache_[i].first == id) {
                return fast_cache_[i].second;
            }
        }
        return "";
    }

    void StringPool::UpdateFastCache(uint32_t id, const std::string& value) const {
        size_t index = fast_cache_index_.fetch_add(1) % fast_cache_.size();
        fast_cache_[index] = std::make_pair(id, value);
    }

    void StringPool::PrefetchStrings(const std::vector<uint32_t>& ids) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t id : ids) {
            if (id < string_count_) {
                auto cache_it = cache_.find(id);
                if (cache_it == cache_.end()) {
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

        std::lock_guard<std::mutex> lock(mutex_);

        for (uint32_t id : ids) {
            std::string fast_result = GetStringFromFastCache(id);
            if (!fast_result.empty()) {
                results.push_back(fast_result);
                continue;
            }

            auto cache_it = cache_.find(id);
            if (cache_it != cache_.end()) {
                results.push_back(cache_it->second.value);
                continue;
            }

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
        const std::vector<size_t>* offsets = nullptr;
        if (shared_string_offsets_) {
            offsets = shared_string_offsets_.get();
        } else if (!string_offsets_.empty()) {
            offsets = &string_offsets_;
        }

        if (!pool_mmap_ || !offsets || id >= string_count_ || id >= offsets->size()) {
            return "";
        }

        size_t offset = (*offsets)[id];
        if (offset + 1 > pool_size_) {
            return "";
        }

        uint32_t length;
        offset = DecodeVarintFromMmap(offset, length);

        if (length > pool_size_ || offset + length > pool_size_) {
            return "";
        }

        std::string result;
        result.reserve(length);

        if (length >= 16) {
            const char* src = pool_mmap_ + offset;
            result.assign(src, length);
        } else {
            result.assign(pool_mmap_ + offset, length);
        }

        return result;
    }

    void StringPool::EncodeVarint(std::vector<uint8_t>& data, uint32_t value) const {
        while (value >= 0x80) {
            data.push_back(static_cast<uint8_t>(value | 0x80));
            value >>= 7;
        }
        data.push_back(static_cast<uint8_t>(value));
    }

    size_t StringPool::GetVarintSize(uint32_t value) const {
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
                break;
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
                break;
            }

            shift += 7;
            if (shift >= 32) {
                throw std::runtime_error("变长编码值过大");
            }
        }

        return offset;
    }

} // namespace GisStorage
