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

# ONNX Runtime（可选：内置 embedding 模型推理）
# 支持三种发现方式：
#   1. find_package（Homebrew / 系统包管理器安装的 cmake config）
#   2. -DONNXRUNTIME_ROOT=<path> 手动指定
#   3. macOS 自动 brew --prefix / Linux 自动搜索标准路径
find_package(onnxruntime QUIET)
if(onnxruntime_FOUND)
    message(STATUS "ONNX Runtime found via find_package")
    add_definitions(-DHAVE_ONNXRUNTIME)
    set(ONNXRUNTIME_LIBRARIES onnxruntime::onnxruntime)
else()
    # 自动探测路径
    if(NOT DEFINED ONNXRUNTIME_ROOT)
        if(APPLE)
            execute_process(
                COMMAND brew --prefix onnxruntime
                OUTPUT_VARIABLE ONNXRUNTIME_ROOT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
        else()
            # Linux: 常见安装路径
            foreach(_ort_prefix /usr/local /usr /opt/onnxruntime)
                if(EXISTS "${_ort_prefix}/include/onnxruntime/onnxruntime_cxx_api.h"
                   OR EXISTS "${_ort_prefix}/include/onnxruntime_cxx_api.h")
                    set(ONNXRUNTIME_ROOT "${_ort_prefix}")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if(ONNXRUNTIME_ROOT AND EXISTS "${ONNXRUNTIME_ROOT}")
        # 查找头文件
        if(EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime/onnxruntime_cxx_api.h")
            set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT}/include/onnxruntime")
        elseif(EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
            set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT}/include")
        endif()
        # 查找库（自动匹配 .dylib / .so / .so.x）
        find_library(ONNXRUNTIME_LIB onnxruntime
            PATHS "${ONNXRUNTIME_ROOT}/lib"
                  "${ONNXRUNTIME_ROOT}/lib64"
            NO_DEFAULT_PATH
        )
        if(ONNXRUNTIME_INCLUDE_DIRS AND ONNXRUNTIME_LIB)
            message(STATUS "ONNX Runtime found at ${ONNXRUNTIME_ROOT}")
            message(STATUS "  include: ${ONNXRUNTIME_INCLUDE_DIRS}")
            message(STATUS "  lib: ${ONNXRUNTIME_LIB}")
            add_definitions(-DHAVE_ONNXRUNTIME)
            include_directories(${ONNXRUNTIME_INCLUDE_DIRS})
            set(ONNXRUNTIME_LIBRARIES ${ONNXRUNTIME_LIB})
        else()
            message(STATUS "ONNX Runtime: headers/lib not found at ${ONNXRUNTIME_ROOT}")
        endif()
    else()
        message(STATUS "ONNX Runtime not found, embedding will use tokenizer-hash fallback")
        message(STATUS "  To enable: brew install onnxruntime (macOS)")
        message(STATUS "             apt install libonnxruntime-dev (Ubuntu)")
        message(STATUS "             or cmake -DONNXRUNTIME_ROOT=/path/to/onnxruntime ..")
    endif()
endif()
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

# 选项：是否构建测试（默认关闭，减少编译耗时）
option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_TOOLS "Build tools" ON)

# 头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/deps
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
    src/framework/config/config_manager.cpp
    src/framework/app_context.cpp
    src/biz/search/search_session.cpp
)

set(SCHEDULER_SRCS
    src/scheduler/scheduler.cpp
    src/scheduler/task/auto_train_task.cpp
    src/scheduler/task/index_rebuild_task.cpp
    src/scheduler/task/trie_rebuild_task.cpp
)


set(RECALL_SRCS
    src/lib/recall/inverted_recall.cpp
    src/lib/recall/vector_recall.cpp
    src/lib/recall/user_history_recall.cpp
    src/lib/recall/hot_content_recall.cpp
    src/lib/recall/recall_fusion.cpp
)

set(INDEX_SRCS
    src/lib/index/inverted_index.cpp
    src/lib/index/vector_index.cpp
    src/lib/index/doc_store.cpp
    src/lib/index/index_builder.cpp
)

set(RANK_SRCS
    src/lib/rank/scorer/bm25_scorer.cpp
    src/lib/rank/scorer/quality_scorer.cpp
    src/lib/rank/scorer/freshness_scorer.cpp
    src/lib/rank/scorer/lgbm_ranker.cpp
    src/lib/rank/reranker/mmr_reranker.cpp
    src/lib/rank/base/rank.cpp
    src/lib/rank/base/rank_manager.cpp
)

set(FEATURE_SRCS
    src/lib/feature/feature_store.cpp
    src/lib/feature/query_features.cpp
    src/lib/feature/user_features.cpp
    src/lib/feature/doc_features.cpp
)

set(EMBEDDING_SRCS
    src/lib/embedding/onnx_embedding_provider.cpp
    src/lib/embedding/embedding_provider_factory.cpp
)

set(FILTER_SRCS
    src/lib/filter/dedup_filter.cpp
    src/lib/filter/quality_filter.cpp
    src/lib/filter/spam_filter.cpp
    src/lib/filter/blacklist_filter.cpp
)

set(USER_SRCS
    src/lib/user/user_profile.cpp
    src/lib/user/user_event_handler.cpp
    src/lib/user/user_interest_updater.cpp
)

set(QUERY_SRCS
    src/lib/query/query_parser.cpp
    src/lib/query/query_normalizer.cpp
    src/lib/query/query_expander.cpp
)

set(CACHE_SRCS
    src/cache/cache_manager.cpp
    src/cache/redis_client.cpp
)

set(AB_SRCS
    src/ab/ab_test.cpp
)

set(FRAMEWORK_SRCS
    src/framework/session/session.cpp
    src/framework/session/session_factory.cpp
    src/framework/handler/base_handler.cpp
    src/framework/handler/handler_manager.cpp
    src/framework/server/server.cpp
    src/framework/processor/processor_pipeline.cpp
)

set(SERVICE_SRCS
    src/gateway/http_server.cpp
)

set(STORAGE_SRCS
    src/lib/storage/query_stats_store.cpp
    src/lib/storage/doc_cooccur_store.cpp
)

set(BIZ_SRCS
    src/biz/doc/doc_handler.cpp
    src/biz/event/event_handler.cpp
    src/biz/search/search_handler.cpp
    src/biz/sug/sug_handler.cpp
    src/biz/sug/sug_trie.cpp
    src/biz/sug/sug_factory.cpp
    src/biz/sug/processor/sug_relevance_processor.cpp
    src/biz/sug/processor/sug_post_rank_processor.cpp
    src/biz/hint/hint_handler.cpp
    src/biz/hint/hint_factory.cpp
    src/biz/hint/processor/hint_rank_processor.cpp
    src/biz/nav/nav_handler.cpp
    src/biz/nav/nav_factory.cpp
    src/biz/nav/processor/nav_score_processor.cpp
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
    ${SCHEDULER_SRCS}
    ${RECALL_SRCS}
    ${INDEX_SRCS}
    ${RANK_SRCS}
    ${FEATURE_SRCS}
    ${EMBEDDING_SRCS}
    ${FILTER_SRCS}
    ${USER_SRCS}
    ${QUERY_SRCS}
    ${CACHE_SRCS}
    ${AB_SRCS}
    ${FRAMEWORK_SRCS}
    ${SERVICE_SRCS}
    ${STORAGE_SRCS}
    ${BIZ_SRCS}
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
    $<$<BOOL:${ONNXRUNTIME_LIBRARIES}>:${ONNXRUNTIME_LIBRARIES}>
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
# 构建后自动创建 config/models/data 符号链接（在 build 目录下）
# 避免手动 ln -sf 时目录错误导致循环链接
# ============================================================
foreach(DIR_NAME config models data)
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/${DIR_NAME}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${CMAKE_SOURCE_DIR}/${DIR_NAME}"
                "${CMAKE_BINARY_DIR}/${DIR_NAME}"
        )
        message(STATUS "Symlink: build/${DIR_NAME} -> ../${DIR_NAME}")
    endif()
endforeach()

# ============================================================
# 测试
# ============================================================
if(BUILD_TESTS)
    enable_testing()

    # ── v2.0 完整集成测试（不依赖 GTest）──
    set(TEST_ALL_SRCS
        tests/test_all.cpp
        ${CORE_SRCS}
        ${SCHEDULER_SRCS}
        ${RECALL_SRCS}
        ${INDEX_SRCS}
        ${RANK_SRCS}
        ${FEATURE_SRCS}
        ${EMBEDDING_SRCS}
        ${FILTER_SRCS}
        ${USER_SRCS}
        ${QUERY_SRCS}
        ${CACHE_SRCS}
        ${AB_SRCS}
        ${SERVICE_SRCS}
        ${STORAGE_SRCS}
        ${BIZ_SRCS}
        ${FRAMEWORK_SRCS}
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
        $<$<BOOL:${ONNXRUNTIME_LIBRARIES}>:${ONNXRUNTIME_LIBRARIES}>
    )
    if(APPLE)
        target_link_directories(test_all PRIVATE "${HOMEBREW_PREFIX}/opt/abseil/lib")
        file(GLOB ABSL_LIBS_TEST "${HOMEBREW_PREFIX}/opt/abseil/lib/libabsl_*.dylib")
        target_link_libraries(test_all PRIVATE ${ABSL_LIBS_TEST})
    endif()
    add_test(NAME IntegrationTests COMMAND test_all)
    message(STATUS "Integration test (test_all) configured - v2.0")
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
