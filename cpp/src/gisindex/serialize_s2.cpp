//
//  Created by vlv-squid on 2025.07.23.
//

#include "gisindex/serialize_s2.h"

#include <fstream>

namespace helper {

    bool saveS2IndexToFile(const std::string& filepath, const std::unordered_map<int64_t, std::vector<int>>& s2IndexMap) {
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open())
            return false;

        size_t indexCount = s2IndexMap.size();
        file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

        for (const auto& [cellId, fids] : s2IndexMap) {
            file.write(reinterpret_cast<const char*>(&cellId), sizeof(cellId));

            size_t fidCount = fids.size();
            file.write(reinterpret_cast<const char*>(&fidCount), sizeof(fidCount));

            for (int fid : fids) {
                file.write(reinterpret_cast<const char*>(&fid), sizeof(fid));
            }
        }

        file.close();
        return true;
    }

    bool loadS2IndexFromFile(const std::string& filepath, std::unordered_map<int64_t, std::vector<int>>& s2IndexMap) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open())
            return false;

        size_t indexCount;
        file.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));

        s2IndexMap.clear();
        for (size_t i = 0; i < indexCount; ++i) {
            int64_t cellId;
            size_t fidCount;

            file.read(reinterpret_cast<char*>(&cellId), sizeof(cellId));
            file.read(reinterpret_cast<char*>(&fidCount), sizeof(fidCount));

            std::vector<int> fids(fidCount);
            for (size_t j = 0; j < fidCount; ++j) {
                int fid;
                file.read(reinterpret_cast<char*>(&fid), sizeof(fid));
                fids[j] = fid;
            }

            s2IndexMap[cellId] = std::move(fids);
        }

        file.close();
        return true;
    }

}; // namespace helper