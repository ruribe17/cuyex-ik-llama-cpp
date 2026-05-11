cmake_minimum_required(VERSION 3.15)
project(cuyex_ik_llama_grpc_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =========================================================
# Build Type
# =========================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "════════════════════════════════════════")
message(STATUS "  Proyecto: ${PROJECT_NAME}")
message(STATUS "  Build:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  Source:   ${CMAKE_SOURCE_DIR}")
message(STATUS "  Binary:   ${CMAKE_BINARY_DIR}")
message(STATUS "════════════════════════════════════════")

# =========================================================
# 🔒 BLOQUEAR /usr/local
# =========================================================
list(APPEND CMAKE_IGNORE_PATH
    "/usr/local/lib/cmake"
    "/usr/local/share/cmake"
    "/usr/local/lib"
    "/usr/local/include"
)
set(CMAKE_IGNORE_PREFIX_PATH "/usr/local")

# =========================================================
# 📦 DEPENDENCIAS: gRPC, Protobuf, absl
# =========================================================
find_package(absl      CONFIG REQUIRED)
find_package(Protobuf  CONFIG REQUIRED)
find_package(gRPC      CONFIG REQUIRED)

message(STATUS "✅ gRPC found      : ${gRPC_FOUND} (v${gRPC_VERSION})")
message(STATUS "✅ Protobuf found : ${Protobuf_VERSION}")
message(STATUS "✅ absl found      : ${absl_FOUND}")

if(NOT TARGET absl::no_destructor)
    message(WARNING "⚠️  absl::no_destructor no encontrado.")
endif()

# =========================================================
# 🛠️ HERRAMIENTAS PROTO
# =========================================================
if(NOT _PROTOBUF_PROTOC)
    set(_PROTOBUF_PROTOC "${CMAKE_PREFIX_PATH}/bin/protoc")
endif()
if(NOT _GRPC_CPP_PLUGIN_EXECUTABLE)
    set(_GRPC_CPP_PLUGIN_EXECUTABLE "${CMAKE_PREFIX_PATH}/bin/grpc_cpp_plugin")
endif()

if(NOT EXISTS "${_PROTOBUF_PROTOC}")
    message(FATAL_ERROR "❌ ERROR: protoc no encontrado en ${_PROTOBUF_PROTOC}")
endif()
if(NOT EXISTS "${_GRPC_CPP_PLUGIN_EXECUTABLE}")
    message(FATAL_ERROR "❌ ERROR: grpc_cpp_plugin no encontrado en ${_GRPC_CPP_PLUGIN_EXECUTABLE}")
endif()

message(STATUS "🛠️  protoc          : ${_PROTOBUF_PROTOC}")
message(STATUS "🛠️  grpc_cpp_plugin : ${_GRPC_CPP_PLUGIN_EXECUTABLE}")

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${Protobuf_INCLUDE_DIRS})

# =========================================================
# 📝 GENERAR CÓDIGO DESDE PROTO
# =========================================================
set(hw_proto "${CMAKE_SOURCE_DIR}/backend.proto")

if(NOT EXISTS "${hw_proto}")
    message(FATAL_ERROR "❌ ERROR: backend.proto no encontrado en ${hw_proto}")
endif()

get_filename_component(hw_proto_path "${hw_proto}" PATH)

set(hw_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/backend.pb.cc")
set(hw_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/backend.pb.h")
set(hw_grpc_srcs  "${CMAKE_CURRENT_BINARY_DIR}/backend.grpc.pb.cc")
set(hw_grpc_hdrs  "${CMAKE_CURRENT_BINARY_DIR}/backend.grpc.pb.h")

add_custom_command(
    OUTPUT "${hw_proto_srcs}" "${hw_proto_hdrs}" "${hw_grpc_srcs}" "${hw_grpc_hdrs}"
    COMMAND "${_PROTOBUF_PROTOC}"
    ARGS
        --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out  "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${hw_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${hw_proto}"
    DEPENDS "${hw_proto}"
    COMMENT "Generating gRPC sources from backend.proto"
)

add_library(hw_grpc_proto STATIC
    ${hw_grpc_srcs}
    ${hw_proto_srcs}
)
target_include_directories(hw_grpc_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(hw_grpc_proto PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
)

# =========================================================
# 🧠 MOTOR: llama.cpp (Subdirectorio)
# =========================================================
set(LLAMA_CPP_DIR "${CMAKE_SOURCE_DIR}/llama.cpp")

if(NOT EXISTS "${LLAMA_CPP_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "❌ ERROR: Motor llama.cpp no encontrado en ${LLAMA_CPP_DIR}. Ejecuta: make llama.cpp")
endif()

set(LLAMA_SERVER OFF CACHE BOOL "Disable llama.cpp http server" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs" FORCE)

add_subdirectory(${LLAMA_CPP_DIR} ${CMAKE_CURRENT_BINARY_DIR}/llama.cpp_build)

# mtmd is not included by llama.cpp's top-level CMake — must add explicitly
# Disable the CLI executables inside mtmd to avoid pulling in unneeded deps
set(LLAMA_TOOLS_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${LLAMA_CPP_DIR}/examples/mtmd ${CMAKE_CURRENT_BINARY_DIR}/mtmd_build)

# =========================================================
# 📁 MÓDULOS DEL PROYECTO
# =========================================================

# Shared include directories — reused across all targets
set(LLAMA_INCLUDES
    ${LLAMA_CPP_DIR}
    ${LLAMA_CPP_DIR}/src
    ${LLAMA_CPP_DIR}/common
    ${LLAMA_CPP_DIR}/vendor
    ${LLAMA_CPP_DIR}/vendor/nlohmann
    ${LLAMA_CPP_DIR}/examples/mtmd
    ${LLAMA_CPP_DIR}/examples/llava
    ${LLAMA_CPP_DIR}/examples/server
)

# 1. commoncod
add_library(commoncod STATIC
    commoncod/proto_mapping.cpp
    commoncod/utils.cpp
)
target_include_directories(commoncod PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(commoncod PRIVATE ${LLAMA_INCLUDES})
target_link_libraries(commoncod PRIVATE ggml llama common mtmd)

# 2. server
# FIX: include ALL server/*.cpp files except server.cpp (has its own main).
# server-common.cpp  → server_log, server_verbose, validate_utf8, tokens_to_output_formatted_string, etc.
# server-task.cpp    → server_tokens::*, server_queue::*, server_response::*, to_json_* methods
# server-queue.cpp   → server_queue::start_loop, on_new_task, post, get_new_id, terminate
# server-context.cpp → server_context, server_slot, load_model, update_slots, etc.
add_library(server STATIC
    ${LLAMA_CPP_DIR}/examples/server/server-common.cpp
    ${LLAMA_CPP_DIR}/examples/server/server-task.cpp
    ${LLAMA_CPP_DIR}/examples/server/server-queue.cpp
    ${LLAMA_CPP_DIR}/examples/server/server-context.cpp
    server/server_globals.cpp
    server/update_slots.cpp
)
target_include_directories(server PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(server PRIVATE ${LLAMA_INCLUDES})
# FIX: link 'common' — provides common_tokenize, common_sampler_*, common_batch_*, etc.
target_link_libraries(server PUBLIC commoncod ggml llama common mtmd)

# 3. grpc_backend
add_library(grpc_backend STATIC
    grpccod/service.cpp
    grpccod/predict.cpp
    grpccod/embedding.cpp
    grpccod/model.cpp
)
target_include_directories(grpc_backend PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(grpc_backend PRIVATE ${LLAMA_INCLUDES})
target_link_libraries(grpc_backend PUBLIC
    server
    commoncod
    hw_grpc_proto
    ggml
    llama
    common
    mtmd
)

# =========================================================
# 🚀 EJECUTABLE FINAL
# =========================================================
add_executable(grpc-server main.cpp)

target_include_directories(grpc-server PRIVATE
    ${LLAMA_INCLUDES}
    ${CMAKE_SOURCE_DIR}/config
)

# =========================================================
# 📌 AÑADIR OpenBLAS ESTÁTICO (si está disponible y GGML_BLAS=ON)
# =========================================================
# ✅ CORRECCIÓN CLAVE: Forzar que OpenBLAS_LIBRARIES sea .a ANTES de find_package
# Esto evita que find_package elija .so por defecto
if(GGML_BLAS)
    if(OPENBLAS_LIB)
        set(OpenBLAS_LIBRARIES "${OPENBLAS_LIB}")
        message(STATUS "✅ Forzando OpenBLAS estático: ${OpenBLAS_LIBRARIES}")
    else()
        find_package(OpenBLAS CONFIG QUIET)
        if(OpenBLAS_FOUND)
            message(STATUS "✅ OpenBLAS found: ${OpenBLAS_LIBRARIES}")
        endif()
    endif()

    # Verificar que sea estático
    if(OpenBLAS_LIBRARIES MATCHES "\\.a$")
        message(STATUS "✅ Using static OpenBLAS: ${OpenBLAS_LIBRARIES}")
    else()
        message(WARNING "⚠️  OpenBLAS found but not static: ${OpenBLAS_LIBRARIES}. Using dynamic linking.")
    endif()

    # Añadir explícitamente (aunque ggml ya lo haga, para redundancia y control)
    target_link_libraries(grpc-server PRIVATE ${OpenBLAS_LIBRARIES})
endif()

# =========================================================
# Vinculación final
# =========================================================
target_link_libraries(grpc-server PRIVATE
    grpc_backend
    server
    commoncod
    hw_grpc_proto
    llama
    ggml
    common
    mtmd
    gRPC::grpc++
    gRPC::grpc
    protobuf::libprotobuf
)

message(STATUS "✅ Configuración CMake completada")
