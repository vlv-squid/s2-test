//
//  Created by vlv-squid on 2025.07.23.
//

#ifndef SERIALIZE_S2_H
#define SERIALIZE_S2_H

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/segment.hpp>

namespace helper {
    bool saveS2IndexToFile(const std::string& filepath, const std::unordered_map<int64_t, std::vector<int>>& s2IndexMap);
    bool loadS2IndexFromFile(const std::string& filepath, std::unordered_map<int64_t, std::vector<int>>& s2IndexMap);
}; // namespace helper

#endif // !SERIALIZE_S2_H
