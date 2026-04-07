# chunk_writer.cc 源码解析

## 概述

`chunk_writer.cc` 是 Apache GraphAr 项目中负责写入图数据块的 C++ 实现文件。该文件提供了多种写入器类，用于将图数据高效地写入存储系统，支持顶点属性、边邻接表、属性数据块和标签数据的写入操作。

## 主要组件

### 1. 头文件与依赖

```cpp
#include <arrow/acero/api.h>
#include <arrow/api.h>
#include <arrow/compute/api.h>
#include <arrow/dataset/dataset.h>
#include "graphar/arrow/chunk_writer.h"
#include "graphar/filesystem.h"
#include "graphar/graph_info.h"
#include "graphar/writer_util.h"
```

主要依赖：
- **Apache Arrow**: 内存列式数据处理和计算引擎
- **Arrow Acero**: Arrow 的执行计划引擎
- **Arrow Dataset**: 数据集处理框架
- **GraphAr 核心组件**: 图数据结构和写入工具

### 2. 版本兼容性处理

```cpp
#if defined(ARROW_VERSION) && ARROW_VERSION >= 12000000
namespace arrow_acero_namespace = arrow::acero;
#else
namespace arrow_acero_namespace = arrow::compute;
#endif
```

确保在不同 Arrow 版本间的兼容性，处理 API 变化。

### 3. 核心辅助函数

#### ExecutePlanAndCollectAsTable
执行计算计划并收集结果为表：
```cpp
Result<std::shared_ptr<arrow::Table>> ExecutePlanAndCollectAsTable(
    const arrow::compute::ExecContext& exec_context,
    std::shared_ptr<arrow_acero_namespace::ExecPlan> plan,
    std::shared_ptr<arrow::Schema> schema, 
    AsyncGeneratorType sink_gen)
```
- 验证执行计划
- 启动生产者
- 将异步生成器转换为同步读取器
- 收集结果为 Arrow Table

## VertexPropertyWriter 类

### 构造函数
```cpp
VertexPropertyWriter(const std::shared_ptr<VertexInfo>& vertex_info, 
                    const std::string& prefix,
                    const std::shared_ptr<WriterOptions>& options,
                    const ValidateLevel& validate_level)
```

**关键特性**：
- 初始化文件系统
- 设置验证级别（不允许 default_validate）
- 使用默认写入选项（如果未提供）

### 验证机制

#### 多级验证支持
```cpp
enum class ValidateLevel {
    no_validate,      // 无验证
    weak_validate,    // 弱验证
    strong_validate,  // 强验证
    default_validate  // 默认验证（仅用于参数传递）
};
```

#### 验证方法重载
1. **顶点数量验证**:
   ```cpp
   Status validate(const IdType& count, ValidateLevel validate_level) const
   ```
   - 检查数量是否为负数

2. **文件复制验证**:
   ```cpp
   Status validate(const std::shared_ptr<PropertyGroup>& property_group, 
                   IdType chunk_index, ValidateLevel validate_level) const
   ```
   - 验证属性组存在性
   - 检查块索引有效性

3. **表写入验证**:
   ```cpp
   Status validate(const std::shared_ptr<arrow::Table>& input_table,
                   const std::shared_ptr<PropertyGroup>& property_group, 
                   IdType chunk_index, ValidateLevel validate_level) const
   ```
   - 弱验证：检查行数不超过块大小
   - 强验证：验证表结构、字段类型、数据类型匹配

### 核心写入方法

#### 1. 顶点数量写入
```cpp
Status WriteVerticesNum(const IdType& count, ValidateLevel validate_level) const
```

#### 2. 数据块写入
```cpp
Status WriteChunk(const std::shared_ptr<arrow::Table>& input_table,
                  const std::shared_ptr<PropertyGroup>& property_group, 
                  IdType chunk_index, ValidateLevel validate_level) const
```

**处理流程**：
1. 验证输入数据和参数
2. 提取索引列和属性列
3. 选择指定列创建新表
4. 写入文件系统

#### 3. 表写入（自动分块）
```cpp
Status WriteTable(const std::shared_ptr<arrow::Table>& input_table,
                  const std::shared_ptr<PropertyGroup>& property_group,
                  IdType start_chunk_index, ValidateLevel validate_level) const
```

**自动处理**：
- 添加缺失的索引列
- 按块大小自动分割表
- 逐块写入数据

#### 4. 标签处理

##### 标签块写入
```cpp
Status WriteLabelChunk(const std::shared_ptr<arrow::Table>& input_table,
                       IdType chunk_index, FileType file_type, 
                       ValidateLevel validate_level) const
```

##### 标签表转换
```cpp
Result<std::shared_ptr<arrow::Table>> GetLabelTable(
    const std::shared_ptr<arrow::Table>& input_table,
    const std::vector<std::string>& labels) const
```

**转换逻辑**：
1. 查找标签列（`:LABEL`）
2. 解析标签字符串（分号分隔）
3. 创建布尔矩阵表示标签关系
4. 转换为 Arrow Table

### 工厂方法

```cpp
static Result<std::shared_ptr<VertexPropertyWriter>> Make(
    const std::shared_ptr<VertexInfo>& vertex_info, 
    const std::string& prefix,
    const std::shared_ptr<WriterOptions>& options,
    const ValidateLevel& validate_level)

static Result<std::shared_ptr<VertexPropertyWriter>> Make(
    const std::shared_ptr<GraphInfo>& graph_info, 
    const std::string& type,
    const std::shared_ptr<WriterOptions>& options,
    const ValidateLevel& validate_level)
```

## EdgeChunkWriter 类

### 构造函数
```cpp
EdgeChunkWriter(const std::shared_ptr<EdgeInfo>& edge_info,
                const std::string& prefix,
                AdjListType adj_list_type,
                const std::shared_ptr<WriterOptions>& options,
                const ValidateLevel& validate_level)
```

**初始化特性**：
- 根据邻接表类型设置顶点块大小
- 支持四种邻接表类型：
  - `unordered_by_source`
  - `ordered_by_source`
  - `unordered_by_dest`
  - `ordered_by_dest`

### 验证方法

#### 多重验证重载
1. **基础验证**：验证邻接表类型和索引
2. **属性组验证**：验证属性组存在性
3. **偏移量表验证**：验证偏移量数据结构
4. **邻接表验证**：验证源/目标索引列
5. **属性表验证**：验证属性数据类型

### 核心写入方法

#### 1. 边数量写入
```cpp
Status WriteEdgesNum(IdType vertex_chunk_index, const IdType& count,
                    ValidateLevel validate_level) const
Status WriteVerticesNum(const IdType& count, ValidateLevel validate_level) const
```

#### 2. 偏移量写入
```cpp
Status WriteOffsetChunk(const std::shared_ptr<arrow::Table>& input_table,
                        IdType vertex_chunk_index, 
                        ValidateLevel validate_level) const
```

**验证要求**：
- 仅支持有序邻接表类型
- 行数限制：源顶点块大小 + 1 或目标顶点块大小 + 1
- 偏移量列必须为 INT64 类型

#### 3. 邻接表写入
```cpp
Status WriteAdjListChunk(const std::shared_ptr<arrow::Table>& input_table,
                         IdType vertex_chunk_index, IdType chunk_index,
                         ValidateLevel validate_level) const
```

**数据要求**：
- 必须包含源索引列（`kSrcIndexCol`）
- 必须包含目标索引列（`kDstIndexCol`）
- 索引列必须为 INT64 类型

#### 4. 属性写入
```cpp
Status WritePropertyChunk(const std::shared_ptr<arrow::Table>& input_table,
                          const std::shared_ptr<PropertyGroup>& property_group,
                          IdType vertex_chunk_index, IdType chunk_index,
                          ValidateLevel validate_level) const
```

## 关键特性

### 1. 多级验证机制
- **无验证**：跳过所有检查，最高性能
- **弱验证**：基本的数据完整性检查
- **强验证**：全面的数据类型和结构验证

### 2. 灵活的写入选项
- 支持多种文件格式（Parquet、ORC、CSV 等）
- 可配置的写入参数
- 批量写入优化

### 3. 自动数据处理
- 自动添加索引列
- 自动表分块
- 标签数据自动转换

### 4. 错误处理
- 详细的验证错误信息
- 边界条件检查
- 类型不匹配检测

### 5. 性能优化
- 列选择减少 I/O
- 批量写入操作
- 内存高效的表操作

## 使用场景

### 1. 顶点数据写入
- 顶点属性数据
- 顶点标签数据
- 顶点数量信息

### 2. 边数据写入
- 边邻接表数据
- 边属性数据
- 边数量和偏移量信息

### 3. 批量数据处理
- 大规模表的自动分块写入
- 数据类型自动转换
- 索引列自动生成

## 总结

`chunk_writer.cc` 实现了一个功能完整、安全可靠的图数据写入框架。通过多级验证机制确保数据质量，通过 Arrow 列式存储引擎提供高性能的写入能力，同时支持灵活的配置和多种数据格式，是 GraphAr 项目中数据持久化的核心组件。
