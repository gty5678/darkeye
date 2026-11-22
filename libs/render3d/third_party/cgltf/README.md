# cgltf v1.15

Unmodified upstream `cgltf.h` and MIT `LICENSE` from:
https://github.com/jkuhlmann/cgltf/tree/v1.15

Header SHA-256: `e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec`.

Selected after evaluating the plan's preferred fastgltf: cgltf's single-file,
dependency-free parser keeps the Qt-only standalone demo build reproducible
without introducing simdjson or requiring vcpkg/network access at configure time.
Parsing remains an independent library, as required by ADR-006. All file I/O,
image decoding, CPU asset conversion and diagnostics belong to GltfLoader.
The MIT notice is retained here and in the header; the parser is statically linked.
