# MdEdit 完整版

独立应用包含实时编辑、Markdown 源码、公式、PDF、SVG、WebP 和 TIFF 支持。构建目录和 vcpkg 清单独立于 Darkeye。

在 Darkeye 根目录运行以下命令构建完整版。使用支持当前编译器和平台的 vcpkg 工具链，并按需指定 triplet。

```sh
cmake -S apps/mdedit -B build/mdedit-full -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/mdedit-full --target MdEdit
```

也可以给 `CMAKE_PREFIX_PATH` 提供现成的 Qt 6.12、MicroTeX 和 tinyxml2 安装路径。Windows PowerShell 用户可在一行中执行配置命令，并用 `$env:VCPKG_ROOT` 引用 vcpkg 目录。

共享编辑器源码位于 `libs/mdedit`，源自 [MdEdit](https://github.com/gty5678/MdEdit) 的 `8f77aad` 和集成时已有的本地轻量版改动。Darkeye 知识库链接轻量构建，其新增的 Qt 依赖只涉及 Widgets 和 Network，两者都是 Darkeye 已有依赖。Darkeye 自身仍使用 Qt Svg。完整版部署由此目录的 CMake 配置负责。
