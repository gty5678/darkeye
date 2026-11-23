vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO NanoMichael/MicroTeX
  REF 0e3707f6dafebb121d98b53c64364d16fefe481d
  SHA512 7f3c0d4218aaee91b7fb1b402e545228c024a9d07479aafbda1f50daf1b37761e57887c89a356eb5ff2073b0a77b528758208540ccf55ef4922a370e5335a400
  PATCHES qt6-portability.patch
)
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt"
          "${CMAKE_CURRENT_LIST_DIR}/MicroTeXConfig.cmake.in"
     DESTINATION "${SOURCE_PATH}")
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME microtex CONFIG_PATH share/microtex)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include"
                    "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
