//
//  Created by vlv-squid on 2025.07.18.
//

#ifndef S2SPATIALINDEX_H
#define S2SPATIALINDEX_H

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include <s2/s2latlng.h>
#include <s2/s2latlng_rect.h>
#include <s2/s2region_coverer.h>
#include <s2/s2cell_index.h>

namespace S2Main {

    class S2SpatialIndex {
      public:
        S2SpatialIndex(const std::string& filePath, int level);
        ~S2SpatialIndex();

        void build(const std::vector<std::pair<int64_t, int>>& entries);
        void addBatch(const std::vector<std::pair<int64_t, int>>& entries);
        void clear();
        void save() const;
        void load();

        // 智能索引管理
        bool smartLoadOrBuild(const std::string& dataset_path, int batch_size = 50000);
        bool isIndexValid() const;
        size_t getIndexSize() const;         // 返回S2单元格数量
        size_t getTotalFeatureCount() const; // 返回总要素数量
        bool buildFromDataset(const std::string& dataset_path, int batch_size = 50000);

        // 多线程构建方法
        bool buildFromDatasetMultiThreaded(const std::string& dataset_path, int batch_size = 50000, int num_threads = 4);

        // 索引完整性验证
        bool isIndexComplete(const std::string& dataset_path) const;
        int64_t getDatasetFeatureCount(const std::string& dataset_path) const;

        std::vector<int> query(const S2LatLngRect& rect, int level) const;

        bool exists() const;

      private:
        std::string filePath_;
        int level_;
        std::unordered_map<int64_t, std::vector<int>> indexMap_;
        mutable std::mutex valid_fids_mutex_; // 用于TBB线程安全
    };

}; // namespace S2Main

#endif // S2SPATIALINDEX_H
