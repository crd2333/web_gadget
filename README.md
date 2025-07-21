# Web Gadgets
也许不是什么有用的东西，仅仅是一些我（和我的~~动物朋友~~ AI 朋友 :smile:）做 web 项目开发过程中创建的一些小玩意儿（工具、打包配置……）。

主要是用作个人备用（以免我铸币发病本地删掉了）。

- ModelViewer
  - 一个简单的 Three.js 模型查看器，主要是用于加载 obj, glb(gltf), stl 模型文件，同时加入了
    1. （可能有 bug 的）线框、点云模式以及点云查询功能（用以确定模型顶点位置）
    2. 每个 subMesh 的材质预览和 vertices attribute 查询功能
  - 还有俩完成度不高的 Babylon.js 模型导入测试（最后感觉导入这一块还是 Three.js 好用）
- prismarine-nbt
  - 打包 [prismarine-nbt](https://github.com/PrismarineJS/prismarine-nbt) 为一个 portable es module 的配置，以及打包完成后的 prismarine-nbt.min.js 文件
  - 这个库的用处是解析 Minecraft 所用的 NBT 数据格式，这里也包含一些游戏数据的示例文件
  - 此外，也可以尝试 VSCode 中的 NBT Viewer 插件
- minecraft_structure_block_parser
  - 从 Mineways（一个第三方 Minecraft 地图导出工具）硬编码的 colorScheme 中导出如何将 Minecraft Assets 转化为 pure color 的配置
  - 从而把 setting 下降到非材质块（纯色块），但好处是不用处理 Minecraft Assets 茫茫多的材质文件，以及项目不需要支持多材质方块
  - 两个把 Minecraft 结构方块中数据解析成 gltf 的脚本（通过 Three.js 的 exporter），参考了 [mcstructure-to-gltf](https://github.com/notcacti/mcstructure-to-gltf)
- ColorQuant
  - RgbQuant
    - [rgbquant](https://github.com/leeoniya/RgbQuant.js/tree/master) 是一个颜色量化的 javascript 库，我把它转化为 esm 版本（从而能够在 js module 中 import，而不是在 html 中通过 script 标签引入）
    - 然后让我的 AI 朋友写了一个示例 demo
  - PnnQuant
    - [pnnquant](https://github.com/mcychan/PnnQuant.js) 跟 RgbQuant 类似但算法不同，年代新一点，对我的项目速度更快。PS：应该有参考 rgbquant 的 API，二者很类似
    - 同样地，转化为了 esm 版本
  - GpuQuant
    - 让 AI 帮忙优化的 PnnQuant GPU 版本，通过 Three.js 的着色器接口，把适合 GPU 计算的任务放到 fragment shader 中
  - 三个算法的 AI 分析
