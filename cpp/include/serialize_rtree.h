//
//  Created by vlv-squid on 2025.07.18.
//

#ifndef SERIALIZE_RTREE_H
#define SERIALIZE_RTREE_H

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/segment.hpp>

namespace helper {
    namespace bg = boost::geometry;
    namespace bgi = boost::geometry::index;
    using Point = bg::model::point<double, 2, bg::cs::cartesian>;
    using Box = bg::model::box<Point>;
    using RtreeValue = std::pair<Box, int>;

    bool saveRtreeToFile(const char* filePath, bgi::rtree<RtreeValue, bgi::quadratic<16>>* rtreeIdx);
    bool loadRtreeFromFile(const char* filePath, bgi::rtree<RtreeValue, bgi::quadratic<16>>& rtreeIdx);
}; // namespace helper

#endif // !SERIALIZE_RTREE_H
