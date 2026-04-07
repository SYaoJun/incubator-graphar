# graph_info.cc 源码解析

## 概述

`graph_info.cc` 是 Apache GraphAr 项目中核心的图元数据管理实现文件。该文件定义了图结构的基本信息，包括顶点信息（VertexInfo）、边信息（EdgeInfo）和图信息（GraphInfo），提供了完整的元数据序列化、反序列化、验证和路径管理功能。

## 主要组件

### 1. 头文件与依赖

```cpp
#include <optional>
#include <unordered_set>
#include <utility>
#include "graphar/status.h"
#include "mini-yaml/yaml/Yaml.hpp"
#include "graphar/filesystem.h"
#include "graphar/graph_info.h"
#include "graphar/result.h"
#include "graphar/types.h"
#include "graphar/version_parser.h"
#include "graphar/yaml.h"
```

**核心依赖**：
- **mini-yaml**: YAML 格式解析
- **GraphAr 核心组件**: 文件系统、类型系统、状态处理
- **STL 容器**: 提供高效的数据结构

### 2. 辅助宏与工具函数

#### 检查宏定义
```cpp
#define CHECK_HAS_ADJ_LIST_TYPE(adj_list_type)                         \
  do {                                                                 \
    if (!HasAdjacentListType(adj_list_type)) {                         \
      return Status::KeyError(                                         \
          "Adjacency list type: ", AdjListTypeToString(adj_list_type), \
          " is not found in edge info.");                              \
    }                                                                  \
  } while (false)
```

#### 工具函数
- **ConcatEdgeTriple**: 连接边三元组（源类型-边类型-目标类型）
- **LookupKeyIndex**: 在映射表中查找键的索引
- **AddVectorElement/RemoveVectorElement**: 向量元素的添加和移除
- **BuildPath**: 构建文件路径

### 3. Property 类与 PropertyGroup 类

#### Property 比较操作符
```cpp
bool operator==(const Property& lhs, const Property& rhs) {
  return (lhs.name == rhs.name) && (lhs.type == rhs.type) &&
         (lhs.is_primary == rhs.is_primary) &&
         (lhs.is_nullable == rhs.is_nullable) &&
         (lhs.cardinality == rhs.cardinality);
}
```

#### PropertyGroup 实现
```cpp
PropertyGroup::PropertyGroup(const std::vector<Property>& properties,
                             FileType file_type, std::string prefix)
    : properties_(properties),
      file_type_(file_type),
      prefix_(std::move(prefix)) {
  // 自动生成前缀
  if (prefix_.empty() && !properties_.empty()) {
    for (const auto& p : properties_) {
      prefix_ += p.name + REGULAR_SEPARATOR;
    }
    prefix_.back() = '/';
  }
}
```

**关键方法**：
- **HasProperty**: 检查是否包含指定属性
- **IsValidated**: 验证属性组的有效性
  - 检查文件类型（CSV、PARQUET、ORC）
  - 验证属性名称唯一性
  - 检查 CSV 文件不支持 LIST 类型和多重基数

### 4. AdjacentList 类

```cpp
AdjacentList::AdjacentList(AdjListType type, FileType file_type,
                           std::string prefix)
    : type_(type), file_type_(file_type), prefix_(std::move(prefix)) {
  if (prefix_.empty()) {
    prefix_ = std::string(AdjListTypeToString(type_)) + "/";
  }
}
```

**支持的邻接表类型**：
- `unordered_by_source`
- `ordered_by_source`
- `unordered_by_dest`
- `ordered_by_dest`

## VertexInfo 类实现

### 1. Pimpl 模式实现

```cpp
class VertexInfo::Impl {
 public:
  Impl(std::string type, IdType chunk_size, std::string prefix,
       const PropertyGroupVector& property_groups,
       const std::vector<std::string>& labels,
       std::shared_ptr<const InfoVersion> version)
      : type_(std::move(type)),
        chunk_size_(chunk_size),
        property_groups_(property_groups),
        labels_(labels),
        prefix_(std::move(prefix)),
        version_(std::move(version)) {
    // 构建属性映射表
    for (size_t i = 0; i < property_groups_.size(); i++) {
      const auto& pg = property_groups_[i];
      for (const auto& p : pg->GetProperties()) {
        property_name_to_index_.emplace(p.name, i);
        property_name_to_primary_.emplace(p.name, p.is_primary);
        property_name_to_nullable_.emplace(p.name, p.is_nullable);
        property_name_to_type_.emplace(p.name, p.type);
        property_name_to_cardinality_.emplace(p.name, p.cardinality);
      }
    }
  }
```

**内部数据结构**：
- `property_name_to_index_`: 属性名到属性组索引的映射
- `property_name_to_primary_`: 属性名到主键标志的映射
- `property_name_to_nullable_`: 属性名到可空标志的映射
- `property_name_to_type_`: 属性名到数据类型的映射
- `property_name_to_cardinality_`: 属性名到基数的映射

### 2. 核心方法

#### 路径生成方法
```cpp
Result<std::string> VertexInfo::GetFilePath(
    std::shared_ptr<PropertyGroup> property_group, IdType chunk_index) const {
  if (property_group == nullptr) {
    return Status::Invalid("property group is nullptr");
  }
  return BuildPath({impl_->prefix_, property_group->GetPrefix()}) + "chunk" +
         std::to_string(chunk_index);
}
```

#### 属性查询方法
- **GetPropertyGroup**: 根据属性名获取属性组
- **GetPropertyGroupByIndex**: 根据索引获取属性组
- **GetPropertyType**: 获取属性类型
- **GetPropertyCardinality**: 获取属性基数
- **IsPrimaryKey/IsNullableKey**: 检查属性键特性

#### 动态修改方法
```cpp
Result<std::shared_ptr<VertexInfo>> VertexInfo::AddPropertyGroup(
    std::shared_ptr<PropertyGroup> property_group) const {
  if (property_group == nullptr) {
    return Status::Invalid("property group is nullptr");
  }
  for (const auto& property : property_group->GetProperties()) {
    if (HasProperty(property.name)) {
      return Status::Invalid("property in the property group already exists: ",
                             property.name);
    }
  }
  return std::make_shared<VertexInfo>(
      impl_->type_, impl_->chunk_size_,
      AddVectorElement(impl_->property_groups_, property_group), impl_->labels_,
      impl_->prefix_, impl_->version_);
}
```

### 3. 序列化与反序列化

#### YAML 加载
```cpp
Result<std::shared_ptr<VertexInfo>> VertexInfo::Load(std::shared_ptr<Yaml> yaml) {
  // 解析基本字段
  std::string type = yaml->operator[]("type").As<std::string>();
  IdType chunk_size = static_cast<IdType>(yaml->operator[]("chunk_size").As<int64_t>());
  std::string prefix;
  if (!yaml->operator[]("prefix").IsNone()) {
    prefix = yaml->operator[]("prefix").As<std::string>();
  }
  
  // 解析标签
  std::vector<std::string> labels;
  const auto& labels_node = yaml->operator[]("labels");
  if (labels_node.IsSequence()) {
    for (auto it = labels_node.Begin(); it != labels_node.End(); it++) {
      labels.push_back((*it).second.As<std::string>());
    }
  }
  
  // 解析属性组
  PropertyGroupVector property_groups;
  auto property_groups_node = yaml->operator[]("property_groups");
  if (!property_groups_node.IsNone()) {
    // 解析每个属性组...
  }
  
  return std::make_shared<VertexInfo>(type, chunk_size, property_groups, labels,
                                      prefix, version);
}
```

#### YAML 导出
```cpp
Result<std::string> VertexInfo::Dump() const noexcept {
  if (!IsValidated()) {
    return Status::Invalid("The vertex info is not validated");
  }
  ::Yaml::Node node;
  try {
    node["type"] = impl_->type_;
    node["chunk_size"] = std::to_string(impl_->chunk_size_);
    node["prefix"] = impl_->prefix_;
    
    // 导出标签
    if (impl_->labels_.size() > 0) {
      for (const auto& label : impl_->labels_) {
        node["labels"].PushBack();
        node["labels"][node["labels"].Size() - 1] = label;
      }
    }
    
    // 导出属性组
    for (const auto& pg : impl_->property_groups_) {
      // 构建属性组 YAML 节点...
    }
    
    ::Yaml::Serialize(node, dump_string);
  } catch (const std::exception& e) {
    return Status::Invalid("Failed to dump vertex info: ", e.what());
  }
  return dump_string;
}
```

## EdgeInfo 类实现

### 1. EdgeInfo::Impl 结构

```cpp
class EdgeInfo::Impl {
 public:
  Impl(std::string src_type, std::string edge_type, std::string dst_type,
       IdType chunk_size, IdType src_chunk_size, IdType dst_chunk_size,
       bool directed, std::string prefix,
       const AdjacentListVector& adjacent_lists,
       const PropertyGroupVector& property_groups,
       std::shared_ptr<const InfoVersion> version)
      : src_type_(std::move(src_type)),
        edge_type_(std::move(edge_type)),
        dst_type_(std::move(dst_type)),
        chunk_size_(chunk_size),
        src_chunk_size_(src_chunk_size),
        dst_chunk_size_(dst_chunk_size),
        directed_(directed),
        prefix_(std::move(prefix)),
        adjacent_lists_(adjacent_lists),
        property_groups_(property_groups),
        version_(std::move(version)) {
    
    // 构建邻接表类型映射
    for (size_t i = 0; i < adjacent_lists_.size(); i++) {
      auto adj_list_type = adjacent_lists_[i]->GetType();
      adjacent_list_type_to_index_[adj_list_type] = i;
    }
    
    // 构建属性映射
    for (size_t i = 0; i < property_groups_.size(); i++) {
      const auto& pg = property_groups_[i];
      for (const auto& p : pg->GetProperties()) {
        property_name_to_index_.emplace(p.name, i);
        property_name_to_primary_.emplace(p.name, p.is_primary);
        property_name_to_nullable_.emplace(p.name, p.is_nullable);
        property_name_to_type_.emplace(p.name, p.type);
      }
    }
  }
```

### 2. 关键验证逻辑

```cpp
bool is_validated() const noexcept {
  // 基本字段检查
  if (src_type_.empty() || edge_type_.empty() || dst_type_.empty() ||
      chunk_size_ <= 0 || src_chunk_size_ <= 0 || dst_chunk_size_ <= 0 ||
      prefix_.empty() || adjacent_lists_.empty()) {
    return false;
  }

  // 邻接表验证
  for (const auto& al : adjacent_lists_) {
    if (!al || !al->IsValidated()) {
      return false;
    }
  }

  // 属性组验证 - 边属性只支持单基数
  std::unordered_set<std::string> check_property_unique_set;
  for (const auto& pg : property_groups_) {
    if (!pg || !pg->IsValidated()) {
      return false;
    }
    for (const auto& p : pg->GetProperties()) {
      if (p.cardinality != Cardinality::SINGLE) {
        std::cout << "Edge property only supports single cardinality, but got: "
                  << CardinalityToString(p.cardinality) << std::endl;
        return false;
      }
      // 检查属性名唯一性...
    }
  }
  return true;
}
```

### 3. 路径管理方法

#### 邻接表路径
```cpp
Result<std::string> EdgeInfo::GetAdjListFilePath(
    IdType vertex_chunk_index, IdType edge_chunk_index,
    AdjListType adj_list_type) const {
  CHECK_HAS_ADJ_LIST_TYPE(adj_list_type);
  size_t i = impl_->adjacent_list_type_to_index_.at(adj_list_type);
  return BuildPath({impl_->prefix_, impl_->adjacent_lists_[i]->GetPrefix()}) +
         "adj_list/part" + std::to_string(vertex_chunk_index) + "/chunk" +
         std::to_string(edge_chunk_index);
}
```

#### 偏移量路径
```cpp
Result<std::string> EdgeInfo::GetAdjListOffsetFilePath(
    IdType vertex_chunk_index, AdjListType adj_list_type) const {
  CHECK_HAS_ADJ_LIST_TYPE(adj_list_type);
  size_t i = impl_->adjacent_list_type_to_index_.at(adj_list_type);
  return BuildPath({impl_->prefix_, impl_->adjacent_lists_[i]->GetPrefix()}) +
         "offset/chunk" + std::to_string(vertex_chunk_index);
}
```

#### 属性路径
```cpp
Result<std::string> EdgeInfo::GetPropertyFilePath(
    const std::shared_ptr<PropertyGroup>& property_group,
    AdjListType adj_list_type, IdType vertex_chunk_index,
    IdType edge_chunk_index) const {
  if (property_group == nullptr) {
    return Status::Invalid("property group is nullptr");
  }
  CHECK_HAS_ADJ_LIST_TYPE(adj_list_type);
  size_t i = impl_->adjacent_list_type_to_index_.at(adj_list_type);
  return BuildPath({impl_->prefix_, impl_->adjacent_lists_[i]->GetPrefix(),
                    property_group->GetPrefix()}) +
         "part" + std::to_string(vertex_chunk_index) + "/chunk" +
         std::to_string(edge_chunk_index);
}
```

## GraphInfo 类实现

### 1. GraphInfo::Impl 结构

```cpp
class GraphInfo::Impl {
 public:
  Impl(const std::string& graph_name, VertexInfoVector vertex_infos,
       EdgeInfoVector edge_infos, const std::vector<std::string>& labels,
       const std::string& prefix, std::shared_ptr<const InfoVersion> version,
       const std::unordered_map<std::string, std::string>& extra_info)
      : name_(graph_name),
        vertex_infos_(std::move(vertex_infos)),
        edge_infos_(std::move(edge_infos)),
        labels_(labels),
        prefix_(prefix),
        version_(std::move(version)),
        extra_info_(extra_info) {
    
    // 构建顶点类型映射
    for (size_t i = 0; i < vertex_infos_.size(); i++) {
      if (vertex_infos_[i] != nullptr) {
        vtype_to_index_[vertex_infos_[i]->GetType()] = i;
      }
    }
    
    // 构建边类型映射
    for (size_t i = 0; i < edge_infos_.size(); i++) {
      if (edge_infos_[i] != nullptr) {
        std::string edge_key = ConcatEdgeTriple(edge_infos_[i]->GetSrcType(),
                                                edge_infos_[i]->GetEdgeType(),
                                                edge_infos_[i]->GetDstType());
        etype_to_index_[edge_key] = i;
      }
    }
  }
```

### 2. 图信息查询

#### 顶点信息查询
```cpp
std::shared_ptr<VertexInfo> GraphInfo::GetVertexInfo(
    const std::string& type) const {
  auto i = GetVertexInfoIndex(type);
  return i.has_value() ? impl_->vertex_infos_[i.value()] : nullptr;
}

std::optional<size_t> GraphInfo::GetVertexInfoIndex(
    const std::string& type) const {
  return LookupKeyIndex(impl_->vtype_to_index_, type);
}
```

#### 边信息查询
```cpp
std::shared_ptr<EdgeInfo> GraphInfo::GetEdgeInfo(
    const std::string& src_type, const std::string& edge_type,
    const std::string& dst_type) const {
  auto i = GetEdgeInfoIndex(src_type, edge_type, dst_type);
  return i.has_value() ? impl_->edge_infos_[i.value()] : nullptr;
}

std::optional<size_t> GraphInfo::GetEdgeInfoIndex(
    const std::string& src_type, const std::string& edge_type,
    const std::string& dst_type) const {
  std::string edge_key = ConcatEdgeTriple(src_type, edge_type, dst_type);
  return LookupKeyIndex(impl_->etype_to_index_, edge_key);
}
```

### 3. 图的加载与构建

#### 从文件系统加载
```cpp
Result<std::shared_ptr<GraphInfo>> GraphInfo::Load(const std::string& path) {
  std::string no_url_path;
  GAR_ASSIGN_OR_RAISE(auto fs, FileSystemFromUriOrPath(path, &no_url_path));
  GAR_ASSIGN_OR_RAISE(auto yaml_content,
                      fs->ReadFileToValue<std::string>(no_url_path));
  GAR_ASSIGN_OR_RAISE(auto graph_meta, Yaml::Load(yaml_content));
  std::string default_name = "graph";
  std::string default_prefix = PathToDirectory(path);
  no_url_path = PathToDirectory(no_url_path);
  return ConstructGraphInfo(graph_meta, default_name, default_prefix, fs,
                            no_url_path);
}
```

#### 图构建辅助函数
```cpp
static Result<std::shared_ptr<GraphInfo>> ConstructGraphInfo(
    std::shared_ptr<Yaml> graph_meta, const std::string& default_name,
    const std::string& default_prefix, const std::shared_ptr<FileSystem> fs,
    const std::string& no_url_path) {
  
  // 解析图元数据
  std::string name = default_name;
  std::string prefix = default_prefix;
  if (!graph_meta->operator[]("name").IsNone()) {
    name = graph_meta->operator[]("name").As<std::string>();
  }
  
  // 加载顶点信息
  VertexInfoVector vertex_infos;
  const auto& vertices = graph_meta->operator[]("vertices");
  if (vertices.IsSequence()) {
    for (auto it = vertices.Begin(); it != vertices.End(); it++) {
      std::string vertex_meta_file = no_url_path + (*it).second.As<std::string>();
      GAR_ASSIGN_OR_RAISE(auto input, fs->ReadFileToValue<std::string>(vertex_meta_file));
      GAR_ASSIGN_OR_RAISE(auto vertex_meta, Yaml::Load(input));
      GAR_ASSIGN_OR_RAISE(auto vertex_info, VertexInfo::Load(vertex_meta));
      vertex_infos.push_back(vertex_info);
    }
  }
  
  // 加载边信息
  EdgeInfoVector edge_infos;
  const auto& edges = graph_meta->operator[]("edges");
  if (edges.IsSequence()) {
    for (auto it = edges.Begin(); it != edges.End(); it++) {
      std::string edge_meta_file = no_url_path + (*it).second.As<std::string>();
      GAR_ASSIGN_OR_RAISE(auto input, fs->ReadFileToValue<std::string>(edge_meta_file));
      GAR_ASSIGN_OR_RAISE(auto edge_meta, Yaml::Load(input));
      GAR_ASSIGN_OR_RAISE(auto edge_info, EdgeInfo::Load(edge_meta));
      edge_infos.push_back(edge_info);
    }
  }
  
  return std::make_shared<GraphInfo>(name, vertex_infos, edge_infos, labels,
                                     prefix, version, extra_info);
}
```

## 关键特性

### 1. Pimpl 设计模式
- 隐藏实现细节，提供稳定的 ABI
- 减少编译依赖
- 便于内存管理和优化

### 2. 完整的验证机制
- **属性组验证**: 文件类型、属性唯一性、类型兼容性
- **邻接表验证**: 类型有效性、路径正确性
- **边属性限制**: 只支持单基数
- **图完整性验证**: 顶点、边信息的有效性

### 3. 灵活的路径管理
- 自动路径构建
- 支持多种文件系统（本地、S3）
- 分块文件路径生成
- 相对路径和绝对路径处理

### 4. 高效的索引机制
- 哈希表映射提供 O(1) 查找
- 属性名到属性组的快速定位
- 边三元组到边信息的索引
- 支持动态添加和删除

### 5. 完整的序列化支持
- YAML 格式的元数据持久化
- 版本信息管理
- 额外信息存储
- 错误处理和异常安全

### 6. 类型安全设计
- 强类型的数据结构
- Result 模式错误处理
- 编译时类型检查
- 运行时验证机制

## 使用场景

1. **图元数据管理**: 定义和管理图的结构信息
2. **文件路径生成**: 自动生成数据文件的存储路径
3. **配置文件处理**: YAML 格式的图配置加载和保存
4. **数据验证**: 确保图结构的完整性和一致性
5. **动态图修改**: 运行时添加/删除顶点类型和边类型

## 总结

`graph_info.cc` 实现了一个功能完整、设计优雅的图元数据管理系统。通过 Pimpl 模式、高效的索引机制、完整的验证体系和灵活的序列化支持，为 GraphAr 项目提供了坚实的元数据基础。该实现不仅保证了类型安全和性能，还提供了良好的可扩展性和维护性，是整个图数据框架的核心组件。
