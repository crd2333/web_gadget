# .mcstructure 文件格式解析

本文档旨在为语言模型提供关于 `.mcstructure` 文件格式的清晰解释，重点关注 `palette`、`block_indices` 和 `size` 字段如何协同工作以定义三维结构。

## 核心概念

`.mcstructure` 格式采用了一种高效的空间压缩策略来存储 Minecraft 建筑。其核心思想类似于乐高积木的说明书，包含两个主要部分：

1.  **`palette` (调色板)**: 一个“积木清单”，定义了结构中使用的所有独特的方块类型及其状态。
2.  **`block_indices` (方块索引)**: 一张“拼装图纸”，通过引用 `palette` 中的积木，来指定三维空间中每个位置应放置哪种方块。

## 1. `palette` (调色板)

`palette` 是一个对象数组，其中每个对象代表一种唯一的方块。

-   **功能**: 作为结构中所有方块类型的中央“字典”。
-   **结构**:
    -   每个元素都是一个对象，包含 `name` (方块ID，如 `minecraft:grass_block`) 和 `states` (方块状态，如 `vine_direction_bits`)。
    -   即使是同一种方块，如果其 `states` 不同，也会在 `palette` 中被视为独立的条目。
-   **关键**: 数组中每个元素的**索引**（从0开始）是该方块在此文件中的唯一标识符。

**示例 (`bedrock.json`)**:
```json
"block_palette": [
  { "name": "minecraft:grass_block", ... }, // 索引 0
  { "name": "minecraft:structure_block", ... }, // 索引 1
  { "name": "minecraft:melon_block", ... }, // 索引 2
  // ...
]
```

## 2. `block_indices` (方块索引)

`block_indices` 是一个一维整数数组，它构成了建筑的“骨架”。

-   **功能**: 定义空间中每个点的方块类型。
-   **结构**:
    -   这是一个扁平化的列表，其中每个整数都是对 `palette` 数组的索引引用。
    -   例如，数组中的数字 `2` 表示在该位置应放置 `palette` 中索引为 `2` 的方块（即 `minecraft:melon_block`）。
    -   `.mcstructure` 格式支持多个“层”（即 `block_indices` 是一个列表的列表），但通常只使用第一层（索引 `[0]`）来表示固体方块。

**示例 (`bedrock.json`)**:
`[0, 0, 0, 0, 0, 1, 2, 3, 3, 4, ...]`

## 3. `size` (尺寸) 与坐标映射

`size` 字段是一个包含三个整数的数组 `[width, height, depth]`，它定义了结构的三维边界。这是将一维 `block_indices` 数组“解压”回三维空间的关键。

-   **数组长度**: `block_indices` 数组的总长度等于 `width * height * depth`。
-   **坐标计算**: 通过以下公式，可以将 `block_indices` 中的一维索引 `i` 转换为三维坐标 `(x, y, z)`：

    ```
    x = floor(i / (depth * height)) % width
    y = floor(i / depth) % height
    z = i % depth
    ```

-   **数据顺序**: 这个公式表明，数据在 `block_indices` 数组中是按 Z -> Y -> X 的顺序存储的（Z轴索引变化最快）。

## 总结

`.mcstructure` 格式通过将方块定义 (`palette`) 与空间布局 (`block_indices`) 分离，实现了高效的数据存储。解析该格式的流程如下：

1.  读取 `size` 数组 `[width, height, depth]`。
2.  读取 `palette` 数组，建立一个从索引到方块定义的映射。
3.  遍历 `block_indices` 数组，对于每个索引 `i` 和其值 `palette_index`：
    a.  使用 `size` 将一维索引 `i` 转换为三维坐标 `(x, y, z)`。
    b.  使用 `palette_index` 从 `palette` 中查找对应的方块数据。
    c.  在计算出的 `(x, y, z)` 坐标处放置该方块。

这种设计既节省了空间，又使得数据处理变得直接和高效。