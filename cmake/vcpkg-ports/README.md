# Dependency compatibility patches

`zlib` is copied from the project's vcpkg baseline
`64adda19c86e89526b5e27703a193c14477cce07`. It retains the upstream port
and source checksum, with one additional patch (`0005-modern-macos.patch`).

New Apple SDK headers define `TARGET_OS_MAC`, which causes zlib 1.2.13 to
activate its classic Mac OS workaround and redefine `fdopen` before including
`stdio.h`. Excluding `__APPLE__` from that branch preserves the normal Unix
implementation on modern macOS. Other platforms keep their existing behavior.

The overlay is registered in `vcpkg.json` so manifest builds and CI use it.
Remove it when updating the baseline to a zlib release that fixes this branch.

`libpng` is also copied from that baseline. Its `modern-macos.patch` excludes
modern Apple platforms from the classic Mac OS `fp.h` include branch, using
the standard `math.h` header instead. This fixes preprocessing with SDK 27.
