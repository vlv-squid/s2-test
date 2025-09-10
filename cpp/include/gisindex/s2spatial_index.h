//
//  Created by vlv-squid on 2025.07.18.
//  优化版本：使用absl数据结构提升性能
//

#ifndef S2SPATIALINDEX_OPTIMIZED_H
#define S2SPATIALINDEX_OPTIMIZED_H

#include <string>
#include <mutex>

// 使用absl的高性能数据结构
#include <absl/container/flat_hash_map.h>
#include <absl/container/inlined_vector.h>
#include <absl/types/span.h>

#include <s2/s2latlng.h>
#include <s2/s2latlng_rect.h>
#include <s2/s2region_coverer.h>
#include <s2/s2cell_index.h>

namespace S2Main {

    class S2SpatialIndex {
      public:
        S2SpatialIndex(const std::string& filePath, int level);
        ~S2SpatialIndex();

        void Build(const std::vector<std::pair<int64_t, int>>& entries);
        void AddBatch(const std::vector<std::pair<int64_t, int>>& entries);
        void Clear();
        void Save() const;
        void Load();

        // 智能索引管理
        bool SmartLoadOrBuild(const std::string& dataset_path, int batch_size = 50000);
        bool IsIndexValid() const;
        size_t GetIndexSize() const;         // 返回S2单元格数量
        size_t GetTotalFeatureCount() const; // 返回总要素数量
        bool BuildFromDataset(const std::string& dataset_path, int batch_size = 50000);

        // 多线程构建方法
        bool BuildFromDatasetMultiThreaded(const std::string& dataset_path, int batch_size = 50000, int num_threads = 4);

        // 索引完整性验证
        bool IsIndexComplete(const std::string& dataset_path) const;
        int64_t GetDatasetFeatureCount(const std::string& dataset_path) const;

        // 优化的查询方法
        std::vector<int> Query(const S2LatLngRect& rect, int level) const;

        // 高性能批量查询
        void QueryBatch(const std::vector<S2LatLngRect>& rects, int level, std::vector<std::vector<int>>& results) const;

        bool Exists() const;

      private:
        std::string filePath_;
        int level_;

        // 使用absl::flat_hash_map替代std::unordered_map
        // 提供更好的内存局部性和查找性能
        absl::flat_hash_map<int64_t, absl::InlinedVector<int, 8>> indexMap_;

        // 使用absl::InlinedVector优化小向量的存储
        // 对于大多数S2单元格，要素数量不会超过8个，避免堆分配

        mutable std::mutex valid_fids_mutex_; // 用于TBB线程安全

        // 性能优化：预分配查询结果向量
        mutable std::vector<int> query_result_cache_;
        mutable size_t last_query_size_ = 0;
    };

}; // namespace S2Main

#endif // S2SPATIALINDEX_OPTIMIZED_H
