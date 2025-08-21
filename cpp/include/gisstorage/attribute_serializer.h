//
//  Created by vlv-squid on 2025.08.21.
//

#ifndef ATTRIBUTE_SERIALIZER_H
#define ATTRIBUTE_SERIALIZER_H

#include "attribute_data.h"

#include <vector>
#include <memory>

namespace GisStorage {

    // 属性序列化器类
    class AttributeSerializer {
      public:
        // 序列化属性数据
        static std::vector<uint8_t> serializeAttributes(const AttributeData& attribute);

        // 反序列化属性数据
        static std::unique_ptr<AttributeData> deserializeAttributes(const std::vector<uint8_t>& data);
    };

} // namespace GisStorage

#endif // ATTRIBUTE_SERIALIZER_H
