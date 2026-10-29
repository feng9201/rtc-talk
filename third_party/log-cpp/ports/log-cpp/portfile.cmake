# Overlay / private-registry port for log-cpp.
#
#   vcpkg install log-cpp[shared] --overlay-ports=<this-repo>/ports
#
# Overlay 下源码就是本仓库。拷到 git registry 时，把 SOURCE_PATH 换成 vcpkg_from_git。
# QString 支持：消费端 #include "w_log_cpp/qt_formatter.h"，用本机 Qt，不经 vcpkg 拉 qt5-base。

get_filename_component(SOURCE_PATH "${CURRENT_PORT_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${SOURCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "log-cpp overlay port expects the repo root at ${SOURCE_PATH}")
endif()

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        sqlite LOGCPP_SUPPORT_SQLITE
)

# [shared] 强制动态库；未选则跟 triplet 的 VCPKG_LIBRARY_LINKAGE。
if("shared" IN_LIST FEATURES)
    set(LOGCPP_BUILD_SHARED ON)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
elseif(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    set(LOGCPP_BUILD_SHARED ON)
else()
    set(LOGCPP_BUILD_SHARED OFF)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DLOGCPP_BUILD_SHARED=${LOGCPP_BUILD_SHARED}
        -DLOGCPP_BUILD_TESTS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME log-cpp CONFIG_PATH lib/cmake/log-cpp)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

if(EXISTS "${SOURCE_PATH}/LICENSE")
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
else()
    file(INSTALL "${SOURCE_PATH}/README.md"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
        RENAME copyright)
endif()
