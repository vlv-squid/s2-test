#ifndef DK_BOOST_GEOMETRY_H
#define DK_BOOST_GEOMETRY_H

#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/geometry/geometries/segment.hpp>

namespace S2Main {
    namespace bg = boost::geometry;
    namespace bgi = boost::geometry::index;
    struct DKBBox {
        using Point = bg::model::point<double, 2, bg::cs::cartesian>;
        using Box = bg::model::box<Point>;

        double minlon;
        double minlat;
        double maxlon;
        double maxlat;

        DKBBox(double minlon, double minlat, double maxlon, double maxlat)
            : minlon(minlon)
            , minlat(minlat)
            , maxlon(maxlon)
            , maxlat(maxlat) {}

        Box transform2BoostBox() const { return Box(Point(minlon, minlat), Point(maxlon, maxlat)); }
    };
}; // namespace S2Main

#endif // !DK_BOOST_GEOMETRY_H