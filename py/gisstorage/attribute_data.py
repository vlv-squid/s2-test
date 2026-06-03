# attribute_data.py
# created by:
#   @author: vlv-squid
#   @date: 2025-08-21

from typing import Dict


class AttributeData:
    """属性数据类，与C++版本兼容"""

    def __init__(self, feature_id: int, properties: Dict[str, str]):
        self.feature_id = feature_id
        self.properties = properties

    def get_feature_id(self) -> int:
        """获取要素ID"""
        return self.feature_id

    def get_properties(self) -> Dict[str, str]:
        """获取属性映射"""
        return self.properties

    def get_property(self, key: str, default_value: str = "") -> str:
        """获取属性值"""
        return self.properties.get(key, default_value)

    def get_serialized_size(self) -> int:
        """计算序列化后的大小"""
        # 计算JSON字符串长度
        import json

        json_str = json.dumps(self.properties, separators=(",", ":"))

        # feature_id(8) + json_length(4) + json_data
        return 8 + 4 + len(json_str.encode("utf-8"))
