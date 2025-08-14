//
//  Created by vlv-squid on 2025.07.18.
//

#include "serialize_rtree.h"

#include <vector>
#include <fstream>
#include <ostream>

namespace helper {

    bool saveRtreeToFile(const char* filePath, bgi::rtree<RtreeValue, bgi::quadratic<16>>* rtreeIdx) {
        std::ofstream ofs(filePath, std::ios::out | std::ios::binary);
        if (!ofs) {
            std::cerr << "无法打开文件进行写入: " << filePath << std::endl;
            return false;
        }

        // 收集所有值
        std::vector<RtreeValue> values;
        rtreeIdx->query(bgi::satisfies([](const RtreeValue&) { return true; }), std::back_inserter(values));

        const size_t size = values.size();
        const size_t element_size = 4 * sizeof(double) + sizeof(int);
        const size_t buffer_size = sizeof(size_t) + size * element_size;
        std::vector<char> buffer(buffer_size);

        // 写入大小
        std::memcpy(buffer.data(), &size, sizeof(size_t));

        // 并行写入每个 Value（可选，视性能需求）
        for (size_t i = 0; i < size; ++i) {
            const auto& [box, fid] = values[i];
            const auto& min = box.min_corner();
            const auto& max = box.max_corner();
            double coords[4] = {bg::get<0>(min), bg::get<1>(min), bg::get<0>(max), bg::get<1>(max)};
            char* ptr = buffer.data() + sizeof(size_t) + i * element_size;

            std::memcpy(ptr, coords, 4 * sizeof(double));
            std::memcpy(ptr + 4 * sizeof(double), &fid, sizeof(fid));
        }

        // 一次性写入文件
        ofs.write(buffer.data(), buffer_size);
        ofs.close();
        return true;
    }

    bool loadRtreeFromFile(const char* filePath, bgi::rtree<RtreeValue, bgi::quadratic<16>>& rtreeIdx) {
        std::ifstream ifs(filePath, std::ios::in | std::ios::binary);
        if (!ifs) {
            std::cerr << "无法打开文件进行读取: " << filePath << std::endl;
            return false;
        }

        // 一次性读取整个文件
        ifs.seekg(0, std::ios::end);
        const size_t file_size = ifs.tellg();
        ifs.seekg(0, std::ios::beg);
        std::vector<char> buffer(file_size);
        ifs.read(buffer.data(), file_size);
        ifs.close();

        // 解析大小
        size_t size = 0;
        std::memcpy(&size, buffer.data(), sizeof(size_t));
        const char* data_ptr = buffer.data() + sizeof(size_t);

        const size_t element_size = 4 * sizeof(double) + sizeof(int);
        std::vector<RtreeValue> values(size);

        // 并行解析数据（可选）
        for (size_t i = 0; i < size; ++i) {
            const char* element_start = data_ptr + i * element_size;
            double coords[4];
            int fid;

            std::memcpy(coords, element_start, 4 * sizeof(double));
            std::memcpy(&fid, element_start + 4 * sizeof(double), sizeof(fid));

            Point min_point(coords[0], coords[1]);
            Point max_point(coords[2], coords[3]);
            values[i] = RtreeValue(Box(min_point, max_point), fid);
        }

        // 构建 RTree
        rtreeIdx = bgi::rtree<RtreeValue, bgi::quadratic<16>>(values.begin(), values.end());
        return true;
    }
}; // namespace helper
