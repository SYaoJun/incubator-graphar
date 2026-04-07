# chunk_reader.cc 源码解析

## 概述

`chunk_reader.cc` 是 Apache GraphAr 项目中负责读取图数据块的 C++ 实现文件。该文件提供了多种读取器类，用于高效地从存储中读取顶点属性、边邻接表和属性数据块。

## 主要组件

### 1. 头文件与依赖

```cpp
#include <arrow/api.h>
#include <arrow/compute/api.h>
#include "graphar/arrow/chunk_reader.h"
#include "graphar/filesystem.h"
#include "graphar/graph_info.h"
#include "graphar/reader_util.h"
```

主要依赖：
- **Apache Arrow**: 用于内存中的列式数据处理
- **GraphAr 核心组件**: 提供图数据结构定义和工具函数

### 2. 辅助函数

#### PropertyGroupToSchema
将属性组转换为 Arrow Schema：
```cpp
Result<std::shared_ptr<arrow::Schema>> PropertyGroupToSchema(
    const std::shared_ptr<PropertyGroup> pg,
    bool contain_index_column = false)
```
- 为每个属性创建对应的 Arrow Field
- 支持基数（Cardinality）处理，非 SINGLE 基数转换为 list 类型
- 可选择性包含顶点索引列

#### LabelToSchema
将标签集合转换为 Arrow Schema：
```cpp
Result<std::shared_ptr<arrow::Schema>> LabelToSchema(
    std::vector<std::string> labels, bool contain_index_column = false)
```
- 每个标签对应一个布尔类型的字段
- 支持可选的顶点索引列

#### 类型转换函数
- **GeneralCast**: 通用的 Arrow 数组类型转换
- **CastStringToLargeString**: 字符串到大字符串的专门转换
- **CastTableWithSchema**: 将表按照指定模式进行类型转换

### 3. VertexPropertyArrowChunkReader 类

#### 构造函数
支持多种初始化方式：
```cpp
// 基础构造函数
VertexPropertyArrowChunkReader(
    const std::shared_ptr<VertexInfo>& vertex_info,
    const std::shared_ptr<PropertyGroup>& property_group,
    const std::string& prefix, util::FilterOptions options)

// 支持指定属性名
VertexPropertyArrowChunkReader(
    const std::shared_ptr<VertexInfo>& vertex_info,
    const std::shared_ptr<PropertyGroup>& property_group,
    const std::vector<std::string>& property_names, 
    const std::string& prefix, util::FilterOptions options)

// 标签专用构造函数
VertexPropertyArrowChunkReader(
    const std::shared_ptr<VertexInfo>& vertex_info,
    const std::vector<std::string>& labels, 
    const std::string& prefix, util::FilterOptions options)
```

#### 核心方法

##### seek 操作
```cpp
Status seek(IdType id)
```
- 定位到指定的顶点 ID
- 计算对应的块索引
- 重置缓存以加载新块

##### GetChunk 方法
支持多个版本的块读取实现：
- **GetChunkV1**: 支持过滤器下推的读取方式
- **GetChunkV2**: 优化的读取方式，不支持过滤器下推
- **GetChunk**: 自动选择版本（有过滤器时使用 V1，否则使用 V2）

##### 静态工厂方法
```cpp
static Result<std::shared_ptr<VertexPropertyArrowChunkReader>> Make(...)
static Result<std::shared_ptr<VertexPropertyArrowChunkReader>> MakeForLabels(...)
```

### 4. AdjListArrowChunkReader 类

#### 构造函数与复制控制
```cpp
AdjListArrowChunkReader(const std::shared_ptr<EdgeInfo>& edge_info, 
                        AdjListType adj_list_type, const std::string& prefix)
AdjListArrowChunkReader(const AdjListArrowChunkReader& other)
AdjListArrowChunkReader& operator=(const AdjListArrowChunkReader& other)
```

#### 定位方法
- **seek_src**: 按源顶点定位
- **seek_dst**: 按目标顶点定位  
- **seek**: 按偏移量定位
- **seek_chunk_index**: 直接定位到指定块索引

#### 数据读取
```cpp
Result<std::shared_ptr<arrow::Table>> GetChunk()
Result<IdType> GetRowNumOfChunk()
Status next_chunk()
```

### 5. AdjListOffsetArrowChunkReader 类

专门用于读取有序邻接表的偏移量信息：
```cpp
Result<std::shared_ptr<arrow::Array>> GetChunk()
Status seek(IdType id)
Status next_chunk()
```

## 关键特性

### 1. 分块读取
- 支持按块读取大规模图数据
- 自动计算块索引和偏移量
- 提供缓存机制避免重复加载

### 2. 过滤支持
- 支持列选择过滤器
- 支持行过滤条件
- V1 版本支持过滤器下推优化

### 3. 类型转换
- 自动进行 Arrow 数据类型转换
- 支持字符串到大字符串的特殊转换
- 保持数据类型一致性

### 4. 错误处理
- 完善的边界检查
- 详细的错误信息
- 使用 Result 模式处理异常

## 使用场景

1. **顶点属性读取**: 读取顶点的各种属性数据
2. **边邻接表读取**: 读取图的邻接表信息
3. **标签数据读取**: 读取顶点的标签信息
4. **偏移量读取**: 读取有序邻接表的偏移量

## 性能优化

1. **缓存机制**: 避免重复加载相同数据块
2. **懒加载**: 只在需要时加载数据
3. **版本选择**: 根据是否有过滤器自动选择最优读取方式
4. **列裁剪**: 只读取需要的列数据

## 总结

`chunk_reader.cc` 实现了一个功能完整、性能优化的图数据块读取框架。通过 Arrow 列式存储引擎和分块读取策略，能够高效处理大规模图数据，同时提供了灵活的过滤和类型转换功能。
