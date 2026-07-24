# GraphAr C++ API 层级说明

## 一、核心概念 Q&A

### Q1: property 是什么？

**property（属性）** 指的是图顶点或边的属性字段，类似关系数据库中的列。

以 LDBC 示例数据为例，`person` 顶点有以下 property：

| property | 类型 | 说明 |
|----------|------|------|
| `id` | int64 | 主键 |
| `firstName` | string | 名 |
| `lastName` | string | 姓 |
| `gender` | string | 性别 |

### Q2: label 和 property 的关系是什么？

**label** 是顶点或边的类型名（`person`、`organization`、`knows`），**property** 是某个 label 下的属性字段——**label 是"谁"，property 是"谁的什么"**。

类比关系数据库：

| 概念 | 类比 | 示例 |
|------|------|------|
| label | 表 | `person`、`organization` |
| property | 列 | `id`、`firstName`、`birthday` |

```
person (label)
├── id (property)
├── firstName (property)
├── lastName (property)
└── birthday (property)

knows (edge label)
├── creationDate (property)
└── ...
```

### Q3: 同一属性是否存储在一起？chunk_size 的含义？

**是的**，同一属性按列存储，但按顶点 ID 范围水平拆分成多个 chunk（每个 chunk 是一个 parquet 文件）。

默认每个 chunk 覆盖固定数量的顶点（如 `chunk_size: 100` 表示每个 chunk 覆盖 100 个顶点）。不同 property 的 chunk 边界是对齐的（chunk0 都覆盖 id 0~99），但存储在不同的 parquet 文件中。

**此外**，property 还按 `property_groups` 分组，同组的多个 property 存在同一个 parquet 文件中。

以 `person.vertex.yml` 为例：
```yaml
chunk_size: 100
property_groups:
  - properties:
      - name: id          # Group 1: id 单独存
  - properties:
      - name: firstName   # Group 2: firstName + lastName + gender 一起存
      - name: lastName
      - name: gender
```

物理目录结构：
```
vertex/person/
├── chunk0/
│   ├── part0.parquet    → id 列（100行，顶点0~99）
│   └── part1.parquet    → firstName + lastName + gender 三列（100行，顶点0~99）
├── chunk1/
│   ├── part0.parquet    → id 列（100行，顶点100~199）
│   └── part1.parquet    → firstName + lastName + gender 三列（100行，顶点100~199）
└── ...
```

### Q4: 边配置中 adj_lists 的含义？

以 `person_knows_person.edge.yml` 为例：

```yaml
adj_lists:
  - ordered: false            # 按 src 组织的无序邻接表
    aligned_by: src
  - ordered: true             # 按 src 组织的有序邻接表
    aligned_by: src
  - ordered: true             # 按 dst 组织的有序邻接表（反向视角）
    aligned_by: dst
property_groups:
  - properties:
      - name: creationDate    # 边属性
```

定义了三种邻接表视图，每种存储为 `(src_id, dst_id)` 两列的 parquet：

| 邻接表 | ordered | aligned_by | 用途 |
|--------|---------|------------|------|
| 第 1 个 | false | src | BFS 等不关心顺序的遍历 |
| 第 2 个 | true | src | 需要二分查找邻居的场景 |
| 第 3 个 | true | dst | PageRank Pull 模式——"谁指向我" |

### Q5: Low-Level API 使用的 Parquet 读写接口

`AdjListPropertyChunkInfoReader` 本身不直接调用 Parquet API——它只是一个路径解析器。真正的 Parquet 读写发生在更底层，分两条路径：

**路径 A：简单读取（无 filter）— `parquet::arrow::FileReaderBuilder`**
```cpp
parquet::arrow::FileReaderBuilder builder;
builder.OpenFile(path);
auto reader = builder.Build().value();
auto table  = reader->ReadTable();              // 全表读取
auto table  = reader->ReadTable({0, 2});        // 按列索引读取
```

**路径 B：需要 filter/列投影 — `arrow::dataset` Scanner**
```cpp
auto format  = std::make_shared<ds::ParquetFileFormat>();
auto dataset = FileSystemDatasetFactory::Make(arrow_fs_, {path}, format, ...)->Finish();
auto scanner = dataset->NewScan()
                  ->Filter(filter_expr)
                  ->Project({"column_a"})
                  ->Finish();
auto table   = scanner->ToTable();
```

完整调用链：
```
AdjListPropertyChunkInfoReader
  ├── 构造时：读 vertex/edge count（二进制文件，非 Parquet）
  ├── seek_src(100)：读 offset 文件 → FileReaderBuilder → ReadTable()
  └── GetChunk()：纯路径拼接，无 I/O

AdjListPropertyArrowChunkReader::GetChunk()
  └── FileSystem::ReadFileToTable()
        ├── 路径 A → FileReaderBuilder → ReadTable()
        └── 路径 B → Dataset → Scanner → Filter/Project → ToTable()
```

---

## 二、Low-Level vs High-Level API 差异

GraphAr 提供了 **三层 API**，从底层到高层逐步抽象：

### 1. Low-Level API（ChunkInfoReader 层）—— 路径导航

**头文件:** `graphar/api/meta_reader.h`

**核心类:**
- `VertexPropertyChunkInfoReader`
- `AdjListChunkInfoReader`
- `AdjListPropertyChunkInfoReader`

**提供能力：** 只返回 chunk 的**文件路径**，不读任何实际数据。

**典型用法：**
```cpp
auto reader = VertexPropertyChunkInfoReader::Make(graph_info, "person", "id").value();
auto chunk_path = reader->GetChunk().value();    // 返回文件路径字符串
reader->seek(520);                                 // 定位到 id=520 的顶点
reader->next_chunk();                              // 下一个 chunk
```

**控制度 vs 易用性：** 最大控制，最小便利。用户拿到路径后需要自己打开 Parquet 文件读取数据。没有 Arrow 集成。

**适用场景：** 需要精确控制每个 chunk 的加载时机、自定义 IO 策略。

---

### 2. Mid-Level API（ArrowChunkReader 层）—— 数据读取

**头文件:** `graphar/api/arrow_reader.h`

**核心类:**
- `VertexPropertyArrowChunkReader`
- `AdjListArrowChunkReader`
- `AdjListPropertyArrowChunkReader`

**提供能力：** 读取实际数据，返回 **Arrow Table**。支持 filter 和列投影。

**典型用法：**
```cpp
auto property_group = graph_info->GetVertexInfo("person")->GetPropertyGroup("firstName");
auto reader = VertexPropertyArrowChunkReader::Make(graph_info, "person", property_group).value();
reader->Filter(expression);                       // 谓词下推
reader->Select({"firstName", "lastName"});        // 列投影
auto table = reader->GetChunk().value();          // 返回 arrow::Table
reader->seek(100);
reader->next_chunk();
```

**控制度 vs 易用性：** 折中。自动完成 Parquet 读取，但用户仍需要管理 chunk 遍历。用户直接操作 Arrow 数据结构。支持 predicate pushdown 和 column projection。

**适用场景：** 需要列式数据做分析计算、需要控制 chunk 粒度但不关心文件 IO 细节。

---

### 3. High-Level API（Collection 层）—— 图迭代器

**头文件:** `graphar/api/high_level_reader.h`

**核心类:**
- `VerticesCollection` — 顶点集合
- `EdgesCollection` — 边集合

**提供能力：** 像操作 STL 容器一样操作图数据，提供 **C++ 迭代器语义**。

**典型用法：**
```cpp
auto vertices = VerticesCollection::Make(graph_info, "person").value();

// 范围迭代
for (auto it = vertices->begin(); it != vertices->end(); ++it) {
    std::cout << it.id() << ", firstName="
              << it.property<std::string>("firstName").value() << std::endl;
}

// 随机访问
auto it = vertices->begin() + 100;

// 按 id 查找
auto it_find = vertices->find(100);

// 获取总数
size_t count = vertices->size();

// 边的迭代
auto edges = EdgesCollection::Make(graph_info, "person", "knows", "person",
                                    AdjListType::ordered_by_source).value();
for (auto it = edges->begin(); it != edges->end(); ++it) {
    std::cout << "src=" << it.source() << ", dst=" << it.destination() << std::endl;
    auto edge = *it;
    std::cout << edge.property<std::string>("creationDate").value() << std::endl;
}

// 按源顶点查找边
auto it_find = edges->find_src(100, edges->begin());
while (it_find.next_src()) { /* ... */ }
```

**控制度 vs 易用性：** 最大便利，最小控制。chunk 的加载、遍历全部自动完成。用户不需要感知 parquet、chunk、Arrow Table 等底层细节。

**适用场景：** 快速开发原型、遍历全图、不需要精细 IO 控制的场景。

---

## 三、三层 API 对比总结

| 维度 | Low-Level | Mid-Level | High-Level |
|------|-----------|-----------|------------|
| 头文件 | `api/meta_reader.h` | `api/arrow_reader.h` | `api/high_level_reader.h` |
| 返回类型 | 文件路径字符串 | `arrow::Table` | C++ 迭代器 / 顶点/边对象 |
| chunk 管理 | 手动 seek/next | 手动 seek/next | **全自动** |
| Parquet 感知 | 用户自己读 | **框架自动读** | **完全透明** |
| filter 支持 | 无 | 支持 | 间接支持 |
| 列投影 | 无 | 支持 | 间接支持 |
| 学习成本 | 高 | 中 | 低 |
| 灵活性 | 最高 | 高 | 低 |
| 典型场景 | 自定义 IO 策略 | 数据分析/计算 | 原型的快速开发 |

### 层级关系

```
High-Level (VerticesCollection / EdgesCollection)
    │  封装了迭代器、自动 chunk 加载
    ├── 内部调用 ──▶ Mid-Level (ArrowChunkReader)
                        │  封装了 Parquet 读取、filter/投影，返回 Arrow Table
                        ├── 内部调用 ──▶ Low-Level (ChunkInfoReader)
                                            │  只做 chunk 索引计算和路径拼接
                                            └── 底层依赖 FileSystem
                                                  ├── FileReaderBuilder → ReadTable()
                                                  └── Dataset → Scanner → ToTable()
```

**选择建议：**
- 想快速跑通、不在意 IO 细节 → 用 **High-Level**
- 需要列式计算、filter pushdown、column projection → 用 **Mid-Level**
- 需要完全掌控文件 IO、自定义存储后端 → 用 **Low-Level**
