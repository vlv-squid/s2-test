//
//  Created by vlv-squid on 2025.07.18.
//

#ifndef HYBRID_INDEX_H
#define HYBRID_INDEX_H

#include "rtree_spatial_index.h"
#include "s2spatial_index.h"

#include <string>
#include <map>
#include <vector>

namespace S2Main {
    class HybridIndex {
      public:
        using Point = boost::geometry::model::point<double, 2, boost::geometry::cs::cartesian>;
        using Box = boost::geometry::model::box<Point>;

        HybridIndex(const std::string& dataPath, const std::string& indexDir, int s2Level = 15);
        ~HybridIndex();

        void buildIndex(bool rebuild = false);
        std::vector<int> queryByBbox(const Box& bbox, bool useRtree = true, bool exactCheck = true);

      private:
        std::string dataPath_;
        std::string indexDir_;
        int s2Level_;
        std::map<int, Box> featureBounds_;

        std::unique_ptr<RtreeSpatialIndex> rtreeIndex_;
        std::unique_ptr<S2SpatialIndex> s2Index_;

        std::string rtreeFilePath_;
        std::string s2BinaryFilePath_;

        void buildFromGDAL();
        int calculateAutoLevel(const Box& bbox);
        bool intersectRect(const Box& a, const Box& b);
        void loadFeatureBoundsIfNeeded();
    };
} // namespace S2Main

#endif // HYBRID_INDEX_H