cmake_minimum_required(VERSION 3.16)
project(MiniSearchRec VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# ============================================================
# MiniSearchRec - Mini Search Recommendation Engine
# 面向初学者的搜索推荐开源项目
# 参考：X(Twitter) 开源推荐算法、工业级搜索推荐架构
# ============================================================

# macOS Homebrew 路径支持
if(APPLE)
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Homebrew prefix: ${HOMEBREW_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH
        "${HOMEBREW_PREFIX}/opt/yaml-cpp"
        "${HOMEBREW_PREFIX}/opt/protobuf"
        "${HOMEBREW_PREFIX}/opt/jsoncpp"
        "${HOMEBREW_PREFIX}/opt/cpp-httplib"
        "${HOMEBREW_PREFIX}/opt/abseil"
        "${HOMEBREW_PREFIX}/opt/abseil/lib/cmake/absl"
        "${HOMEBREW_PREFIX}/opt/spdlog"
    )
    include_directories(
        "${HOMEBREW_PREFIX}/opt/cpp-httplib/include"
        "${HOMEBREW_PREFIX}/opt/jsoncpp/include"
    )
    link_directories(
        "${HOMEBREW_PREFIX}/opt/jsoncpp/lib"
    )
endif()

# 查找依赖包
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Protobuf REQUIRED)
find_package(absl QUIET)
find_package(spdlog QUIET)
find_package(CppJieba QUIET)
find_package(Faiss QUIET)

# LightGBM（可选，brew install lightgbm）
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}/opt/lightgbm")
    find_library(LIGHTGBM_LIB lib_lightgbm
        HINTS "${HOMEBREW_PREFIX}/opt/lightgbm/lib"
              "${HOMEBREW_PREFIX}/lib"
    )
    find_path(LIGHTGBM_INCLUDE LightGBM/c_api.h
        HINTS "${HOMEBREW_PREFIX}/opt/lightgbm/include"
              "${HOMEBREW_PREFIX}/include"
    )
else()
    find_library(LIGHTGBM_LIB lib_lightgbm HINTS /usr/local/lib /usr/lib)
    find_path(LIGHTGBM_INCLUDE LightGBM/c_api.h
        HINTS /usr/local/include /usr/include)
endif()

if(LIGHTGBM_LIB AND LIGHTGBM_INCLUDE)
    set(LightGBM_FOUND TRUE)
    add_definitions(-DHAVE_LIGHTGBM)
    message(STATUS "LightGBM found: ${LIGHTGBM_LIB}")
    include_directories(${LIGHTGBM_INCLUDE})
else()
    set(LightGBM_FOUND FALSE)
    message(STATUS "LightGBM not found, using built-in rule tree fallback")
    message(STATUS "  To enable: brew install lightgbm")
endif()

message(STATUS "yaml-cpp version: ${yaml-cpp_VERSION}")
message(STATUS "Protobuf version: ${Protobuf_VERSION}")

# spdlog
if(spdlog_FOUND)
    add_definitions(-DHAVE_SPDLOG)
    message(STATUS "spdlog found, logging enabled")
else()
    message(STATUS "spdlog not found, logging disabled")
endif()

# cppjieba
if(CppJieba_FOUND)
    add_definitions(-DHAVE_CPPJIEBA)
    message(STATUS "cppjieba found, Chinese segmentation enabled")
else()
    message(STATUS "cppjieba not found, using simple tokenizer")
endif()

# faiss
if(Faiss_FOUND)
    add_definitions(-DHAVE_FAISS)
    message(STATUS "Faiss found, HNSW enabled")
else()
    message(STATUS "Faiss not found, using brute-force fallback")
endif()

# 选项：是否构建测试
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_TOOLS "Build tools" ON)

# 头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_BINARY_DIR}/proto
)

# macOS：abseil 和 protobuf 的包含路径
if(APPLE)
    include_directories(
        "${HOMEBREW_PREFIX}/opt/abseil/include"
        "${HOMEBREW_PREFIX}/opt/protobuf/include"
    )
endif()

# ============================================================
# Protocol Buffers 生成
# ============================================================
find_program(PROTOC protoc)
if(NOT PROTOC)
    message(FATAL_ERROR "protoc not found. Please install protobuf-compiler.")
endif()

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_SRCS)
set(PROTO_HDRS)

# 确保 proto 输出目录存在
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/proto)

file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")
foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    set(PROTO_SRC ${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_NAME}.pb.cc)
    set(PROTO_HDR ${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_NAME}.pb.h)
    list(APPEND PROTO_SRCS ${PROTO_SRC})
    list(APPEND PROTO_HDRS ${PROTO_HDR})

    add_custom_command(
        OUTPUT ${PROTO_SRC} ${PROTO_HDR}
        COMMAND ${PROTOC}
            --proto_path=${PROTO_DIR}
            --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/proto
            ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating protobuf for ${PROTO_NAME}"
    )
endforeach()
include_directories(${CMAKE_CURRENT_BINARY_DIR}/proto)

# ============================================================
# 源码收集
# ============================================================
set(CORE_SRCS
    src/core/session.cpp
    src/core/pipeline.cpp
    src/core/config_manager.cpp
    src/core/factory.cpp
    src/core/app_context.cpp
)

# build_index 只需要核心基础（不含 factory，factory 依赖全部 processor）
set(CORE_BASE_SRCS
    src/core/session.cpp
    src/core/config_manager.cpp
    src/core/app_context.cpp
)

set(RECALL_SRCS
    src/recall/inverted_recall.cpp
    src/recall/vector_recall.cpp
    src/recall/user_history_recall.cpp
    src/recall/hot_content_recall.cpp
    src/recall/recall_fusion.cpp
)

set(INDEX_SRCS
    src/index/inverted_index.cpp
    src/index/vector_index.cpp
    src/index/doc_store.cpp
    src/index/index_builder.cpp
)

set(RANK_SRCS
    src/rank/bm25_scorer.cpp
    src/rank/quality_scorer.cpp
    src/rank/freshness_scorer.cpp
    src/rank/lgbm_ranker.cpp
    src/rank/mmr_reranker.cpp
)

set(FEATURE_SRCS
    src/feature/feature_store.cpp
    src/feature/query_features.cpp
    src/feature/user_features.cpp
    src/feature/doc_features.cpp
)

set(FILTER_SRCS
    src/filter/dedup_filter.cpp
    src/filter/quality_filter.cpp
    src/filter/spam_filter.cpp
    src/filter/blacklist_filter.cpp
)

set(USER_SRCS
    src/user/user_profile.cpp
    src/user/user_event_handler.cpp
    src/user/user_interest_updater.cpp
)

set(QUERY_SRCS
    src/query/query_parser.cpp
    src/query/query_normalizer.cpp
    src/query/query_expander.cpp
)

set(CACHE_SRCS
    src/cache/cache_manager.cpp
    src/cache/redis_client.cpp
)

set(AB_SRCS
    src/ab/ab_test.cpp
)

set(SERVICE_SRCS
    src/service/http_server.cpp
    src/service/search_handler.cpp
    src/service/doc_handler.cpp
    src/service/event_handler.cpp
)

set(UTILS_SRCS
    src/utils/string_utils.cpp
    src/utils/vector_utils.cpp
    src/utils/hash_utils.cpp
    src/utils/logger.cpp
)

# 主程序源码
set(MAIN_SRC src/main.cpp)

# 全部源码
set(ALL_SRCS
    ${MAIN_SRC}
    ${CORE_SRCS}
    ${RECALL_SRCS}
    ${INDEX_SRCS}
    ${RANK_SRCS}
    ${FEATURE_SRCS}
    ${FILTER_SRCS}
    ${USER_SRCS}
    ${QUERY_SRCS}
    ${CACHE_SRCS}
    ${AB_SRCS}
    ${SERVICE_SRCS}
    ${UTILS_SRCS}
    ${PROTO_SRCS}
)

# ============================================================
# 可执行文件
# ============================================================
add_executable(minisearchrec ${ALL_SRCS})

target_link_libraries(minisearchrec
    PRIVATE
    Threads::Threads
    protobuf::libprotobuf
    yaml-cpp::yaml-cpp
    jsoncpp
    sqlite3
    $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog>
    $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog_header_only>
    $<$<BOOL:${CppJieba_FOUND}>:cppjieba>
    $<$<BOOL:${Faiss_FOUND}>:faiss>
    $<$<BOOL:${LightGBM_FOUND}>:${LIGHTGBM_LIB}>
    # GCC < 9 需要显式链接 filesystem
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)

# macOS：直接链接 abseil 所有库（protobuf 34.x 依赖）
if(APPLE)
    target_link_directories(minisearchrec PRIVATE "${HOMEBREW_PREFIX}/opt/abseil/lib")
    file(GLOB ABSL_LIBS "${HOMEBREW_PREFIX}/opt/abseil/lib/libabsl_*.dylib")
    target_link_libraries(minisearchrec PRIVATE ${ABSL_LIBS})
endif()

# ============================================================
# 工具
# ============================================================
if(BUILD_TOOLS)
    add_executable(build_index tools/build_index.cpp ${INDEX_SRCS} ${CORE_BASE_SRCS} ${AB_SRCS} ${UTILS_SRCS} ${PROTO_SRCS})
    target_link_libraries(build_index
        PRIVATE
        Threads::Threads
        protobuf::libprotobuf
        yaml-cpp::yaml-cpp
        jsoncpp
        sqlite3
        $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog>
        $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog_header_only>
    )
    if(APPLE)
        target_link_directories(build_index PRIVATE "${HOMEBREW_PREFIX}/opt/abseil/lib")
        file(GLOB ABSL_LIBS_TOOL "${HOMEBREW_PREFIX}/opt/abseil/lib/libabsl_*.dylib")
        target_link_libraries(build_index PRIVATE ${ABSL_LIBS_TOOL})
    endif()

    # 离线样本导出工具（训练闭环）
    add_executable(dump_train_data tools/dump_train_data.cpp)
    target_link_libraries(dump_train_data PRIVATE sqlite3)
    if(APPLE)
        target_link_directories(dump_train_data PRIVATE "${HOMEBREW_PREFIX}/opt/jsoncpp/lib")
    endif()
    message(STATUS "dump_train_data tool configured")
endif()

# ============================================================
# 测试
# ============================================================
if(BUILD_TESTS)
    enable_testing()

    # ── 独立集成测试（不依赖 GTest）──
    # 覆盖 v1.1/v1.2 全部修复点
    set(TEST_ALL_SRCS
        tests/test_all.cpp
        ${CORE_SRCS}
        ${RECALL_SRCS}
        ${INDEX_SRCS}
        ${RANK_SRCS}
        ${FEATURE_SRCS}
        ${FILTER_SRCS}
        ${USER_SRCS}
        ${QUERY_SRCS}
        ${CACHE_SRCS}
        ${AB_SRCS}
        ${SERVICE_SRCS}
        ${UTILS_SRCS}
        ${PROTO_SRCS}
    )
    add_executable(test_all ${TEST_ALL_SRCS})
    target_link_libraries(test_all
        PRIVATE
        Threads::Threads
        protobuf::libprotobuf
        yaml-cpp::yaml-cpp
        jsoncpp
        sqlite3
        $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog>
        $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog_header_only>
        $<$<BOOL:${LightGBM_FOUND}>:${LIGHTGBM_LIB}>
    )
    if(APPLE)
        target_link_directories(test_all PRIVATE "${HOMEBREW_PREFIX}/opt/abseil/lib")
        file(GLOB ABSL_LIBS_TEST "${HOMEBREW_PREFIX}/opt/abseil/lib/libabsl_*.dylib")
        target_link_libraries(test_all PRIVATE ${ABSL_LIBS_TEST})
    endif()
    add_test(NAME IntegrationTests COMMAND test_all)
    message(STATUS "Integration test (test_all) configured")

    # ── GTest 单元测试（可选）──
    find_package(GTest QUIET)
    if(GTest_FOUND)
        set(TEST_SRCS
            tests/test_inverted_index.cpp
        )
        add_executable(test_minisearchrec ${TEST_SRCS} ${ALL_SRCS})
        target_link_libraries(test_minisearchrec
            PRIVATE GTest::gtest GTest::gtest_main
            Threads::Threads protobuf::libprotobuf yaml-cpp::yaml-cpp jsoncpp)
        add_test(NAME UnitTests COMMAND test_minisearchrec)
    else()
        message(STATUS "GTest not found, skipping unit tests (test_all still available)")
    endif()
endif()

# ============================================================
# 安装规则
# ============================================================
install(TARGETS minisearchrec DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/minisearchrec)
install(DIRECTORY data/ DESTINATION share/minisearchrec/data)
install(DIRECTORY models/ DESTINATION share/minisearchrec/models)

message(STATUS "MiniSearchRec configured successfully!")
message(STATUS "  CXX Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Build Tools: ${BUILD_TOOLS}")
