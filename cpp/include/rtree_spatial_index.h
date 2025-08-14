//
//  Created by vlv-squid on 2025.07.24.
//

#ifndef RTREE_SPATIAL_INDEX_H
#define RTREE_SPATIAL_INDEX_H

#include <string>
#include <vector>
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point_xy.hpp>

namespace S2Main {
    namespace bg = boost::geometry;
    namespace bgi = boost::geometry::index;

    using Point = bg::model::point<double, 2, bg::cs::cartesian>;
    using Box = bg::model::box<Point>;
    using RtreeValue = std::pair<Box, int>; // (bounding box, fid)

    class RtreeSpatialIndex {
      public:
        explicit RtreeSpatialIndex(const std::string& filePath);
        ~RtreeSpatialIndex();

        void build(const std::vector<RtreeValue>& values);
        void save() const;
        void load();

        std::vector<RtreeValue> query(const Box& bbox) const;

        bool exists() const;

      private:
        std::string filePath_;
        bgi::rtree<RtreeValue, bgi::quadratic<16>>* rtree_;
    };
} // namespace S2Main

#endif // RTREE_SPATIAL_INDEX_H