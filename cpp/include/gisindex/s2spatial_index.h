//
//  Created by vlv-squid on 2025.07.18.
//

#ifndef S2SPATIALINDEX_H
#define S2SPATIALINDEX_H

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

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
        size_t getIndexSize() const;
        bool buildFromDataset(const std::string& dataset_path, int batch_size = 50000);

        std::vector<int> query(const S2LatLngRect& rect, int level) const;

        bool exists() const;

      private:
        std::string filePath_;
        int level_;
        std::unordered_map<int64_t, std::vector<int>> indexMap_;
    };

}; // namespace S2Main

#endif // S2SPATIALINDEX_H
