# =============================================================================
# Makefile para cuyex-ik-llama-cpp (Estructura Out-of-Source Óptima + OpenBLAS estático)
# =============================================================================

# IK_LLAMA_VERSION  ?= a8aecbf15933295af96504f9a693998322185b5c
# IK_LLAMA_VERSION  ?= 35845dd9753762829fd9b5d75a0b710d9b5bacf5
IK_LLAMA_VERSION  ?= 40aae0b6d86d50c0ee7011b3ce59a233203e430a
LLAMA_REPO        ?= https://github.com/ikawrakow/ik_llama.cpp

JOBS              ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)
CURRENT_DIR       := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---------------------------------------------------------------------------
# Rutas Absolutas
# ---------------------------------------------------------------------------
PROJECT_DIR       := $(patsubst %/,%,$(CURRENT_DIR))
PARENT_DIR        := $(dir $(patsubst %/,%,$(PROJECT_DIR)))
GRPC_DIR          := $(PARENT_DIR)grpc
INSTALLED_PACKAGES:= $(GRPC_DIR)/installed_packages
BUILDS_DIR        := $(PROJECT_DIR)/builds
OUTPUT_DIR        := $(PROJECT_DIR)/output
BACKEND_PROTO_SRC := $(PARENT_DIR)backend.proto
BACKEND_PROTO_LINK:= $(PROJECT_DIR)/backend.proto

# ---------------------------------------------------------------------------
# Detección de Rutas CMake para Dependencias
# ---------------------------------------------------------------------------
define detect_cmake_dir
$(shell \
  if [ -f "$(INSTALLED_PACKAGES)/lib/cmake/$(1)/$(2)" ]; then \
    echo "$(INSTALLED_PACKAGES)/lib/cmake"; \
  elif [ -f "$(INSTALLED_PACKAGES)/lib64/cmake/$(1)/$(2)" ]; then \
    echo "$(INSTALLED_PACKAGES)/lib64/cmake"; \
  else \
    echo "NOT_FOUND"; \
  fi)
endef

GRPC_CMAKE_DIR    := $(call detect_cmake_dir,grpc,gRPCConfig.cmake)
ABSL_CMAKE_DIR    := $(call detect_cmake_dir,absl,abslConfig.cmake)
PROTOBUF_CMAKE_DIR:= $(call detect_cmake_dir,protobuf,protobuf-config.cmake)
UTF8_CMAKE_DIR    := $(call detect_cmake_dir,utf8_range,utf8_range-config.cmake)

ifeq ($(GRPC_CMAKE_DIR),NOT_FOUND)
$(error ❌ ERROR: gRPCConfig.cmake no encontrado en $(INSTALLED_PACKAGES). Ejecuta primero la instalación de gRPC en ../grpc/)
endif

# ---------------------------------------------------------------------------
# Flags Comunes de CMake
# ---------------------------------------------------------------------------
LOCAL_CMAKE_PREFIX := $(INSTALLED_PACKAGES)/lib/cmake;$(INSTALLED_PACKAGES)/lib64/cmake;$(INSTALLED_PACKAGES)
CMAKE_IGNORE := /usr/local/lib/cmake;/usr/local/share/cmake;/usr/local/lib;/usr/local/include

COMMON_CMAKE_ARGS := \
	-DCMAKE_PREFIX_PATH:STRING='$(LOCAL_CMAKE_PREFIX)' \
	-DCMAKE_IGNORE_PATH:STRING='$(CMAKE_IGNORE)' \
	-Dabsl_DIR:PATH=$(ABSL_CMAKE_DIR) \
	-DProtobuf_DIR:PATH=$(PROTOBUF_CMAKE_DIR) \
	-Dutf8_range_DIR:PATH=$(UTF8_CMAKE_DIR) \
	-DgRPC_DIR:PATH=$(GRPC_CMAKE_DIR) \
	-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES:STRING=$(INSTALLED_PACKAGES)/include \
	-D_PROTOBUF_PROTOC:FILEPATH=$(INSTALLED_PACKAGES)/bin/protoc \
	-D_GRPC_CPP_PLUGIN_EXECUTABLE:FILEPATH=$(INSTALLED_PACKAGES)/bin/grpc_cpp_plugin \
	-DBUILD_SHARED_LIBS=OFF \
	-DLLAMA_CURL=OFF \
	-DGGML_NATIVE=OFF \
	-DLLAMA_OPENSSL=OFF

# ---------------------------------------------------------------------------
# OpenBLAS estático: configuración global (usada por todos los targets)
# ---------------------------------------------------------------------------
OPENBLAS_ROOT ?= /usr
OPENBLAS_LIBDIR ?= $(OPENBLAS_ROOT)/lib64
OPENBLAS_INCDIR ?= $(OPENBLAS_ROOT)/include

# Detectar si libopenblas.a existe (si no, usar fallback dinámico o error)
OPENBLAS_STATIC_LIB ?= $(OPENBLAS_LIBDIR)/libopenblas.a
OPENBLAS_FOUND ?= $(shell test -f "$(OPENBLAS_STATIC_LIB)" && echo "yes" || echo "no")

ifeq ($(OPENBLAS_FOUND),yes)
	# ✅ CORRECCIÓN CLAVE: Usar OPENBLAS_LIB (no OPENBLAS_LIBRARY) para forzar .a
	# llama.cpp usa OPENBLAS_LIB para elegir .a en lugar de .so
	OPENBLAS_FLAGS := \
		-DGGML_BLAS=ON \
		-DGGML_BLAS_VENDOR=OpenBLAS \
		-DOPENBLAS_INCLUDE_DIR=$(OPENBLAS_INCDIR) \
		-DOPENBLAS_LIB=$(OPENBLAS_STATIC_LIB) \
		-DOPENBLAS_LIBRARY=$(OPENBLAS_STATIC_LIB)
	OPENBLAS_LDFLAGS := -L$(OPENBLAS_LIBDIR) -Wl,-Bstatic -lopenblas -Wl,-Bdynamic
else
	OPENBLAS_FLAGS :=
	OPENBLAS_LDFLAGS :=
endif

# ---------------------------------------------------------------------------
# Flags Específicos por Variante (con soporte OpenBLAS estático)
# ---------------------------------------------------------------------------
FLAGS_AVX2 := \
	-DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_AVX512=OFF -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON

FLAGS_AVX512 := \
	-DGGML_AVX=ON -DGGML_AVX2=OFF -DGGML_AVX512=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON

FLAGS_FALLBACK := \
	-DGGML_AVX=OFF -DGGML_AVX2=OFF -DGGML_AVX512=OFF -DGGML_FMA=OFF -DGGML_F16C=OFF -DGGML_BMI2=OFF

FLAGS_2690V4_CFLAGS := -O3 -march=broadwell -mtune=broadwell -fno-math-errno -funsafe-math-optimizations -funroll-loops -flto
FLAGS_2690V4_LDFLAGS := -static-libgcc -static-libstdc++ -Wl,--as-needed $(OPENBLAS_LDFLAGS)
FLAGS_2690V4 := \
	-DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_AVX512=OFF -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON \
	-DGGML_OPENMP=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
	-DCMAKE_CXX_FLAGS="$(FLAGS_2690V4_CFLAGS)" \
	-DCMAKE_C_FLAGS="$(FLAGS_2690V4_CFLAGS)" \
	-DCMAKE_EXE_LINKER_FLAGS="$(FLAGS_2690V4_LDFLAGS)" \
	$(OPENBLAS_FLAGS)

# ---------------------------------------------------------------------------
# TARGETS PRINCIPALES
# ---------------------------------------------------------------------------

.PHONY: all clean purge llama.cpp apply-patches proto-link
.PHONY: avx2 avx512 fallback 2690v4

all: avx2

# ---------------------------------------------------------------------------
# CREAR SYMLINK DE backend.proto
# ---------------------------------------------------------------------------
proto-link:
	@echo "🔗 Verificando symlink de backend.proto..."
	@if [ -L "$(BACKEND_PROTO_LINK)" ]; then \
		if [ ! -e "$(BACKEND_PROTO_LINK)" ]; then \
			echo "  ⚠️  Symlink roto. Recreando..."; \
			rm "$(BACKEND_PROTO_LINK)"; \
			ln -s "$(BACKEND_PROTO_SRC)" "$(BACKEND_PROTO_LINK)"; \
		fi; \
	elif [ -e "$(BACKEND_PROTO_LINK)" ]; then \
		echo "  ℹ️  backend.proto existe (archivo real). Se mantiene."; \
	else \
		if [ -f "$(BACKEND_PROTO_SRC)" ]; then \
			ln -s "$(BACKEND_PROTO_SRC)" "$(BACKEND_PROTO_LINK)"; \
			echo "  ✅ Symlink creado: $(BACKEND_PROTO_LINK)"; \
		else \
			echo "  ❌ ERROR: backend.proto no encontrado en $(BACKEND_PROTO_SRC)"; \
			exit 1; \
		fi; \
	fi

# ---------------------------------------------------------------------------
# CLONAR LLAMA.CPP
# ---------------------------------------------------------------------------
llama.cpp: proto-link
	@echo "📦 Clonando llama.cpp..."
	@if [ -d "$(PROJECT_DIR)/llama.cpp" ]; then \
		echo "  ℹ️  llama.cpp ya existe. Verificando versión..."; \
		cd $(PROJECT_DIR)/llama.cpp && git fetch origin && git checkout $(IK_LLAMA_VERSION); \
	else \
		mkdir -p $(PROJECT_DIR)/llama.cpp; \
		cd $(PROJECT_DIR)/llama.cpp && \
		git init && \
		git remote add origin $(LLAMA_REPO) && \
		git fetch origin && \
		git checkout -b build $(IK_LLAMA_VERSION); \
	fi
	cd $(PROJECT_DIR)/llama.cpp && git submodule update --init --recursive --depth 1 --single-branch

# ---------------------------------------------------------------------------
# APLICAR PARCHES
# ---------------------------------------------------------------------------
apply-patches: llama.cpp
	@echo "🔧 Aplicando parches..."
	@if [ -d "$(PROJECT_DIR)/patches" ] && [ -n "$$(ls $(PROJECT_DIR)/patches/*.patch 2>/dev/null)" ]; then \
		cd $(PROJECT_DIR)/llama.cpp; \
		for patch in ../patches/*.patch; do \
			patch_name=$$(basename "$$patch"); \
			if patch -p1 --dry-run < "$$patch" > /dev/null 2>&1; then \
				echo "  Aplicando: $$patch_name"; \
				patch -p1 < "$$patch" || (patch -R -p1 < "$$patch" 2>/dev/null; exit 1); \
			else \
				echo "  Skip (ya aplicado): $$patch_name"; \
			fi; \
		done; \
	else \
		echo "  ℹ️  No hay parches para aplicar"; \
	fi

# ---------------------------------------------------------------------------
# BUILD GENÉRICO
# ---------------------------------------------------------------------------
define build_variant
	@echo "══════════════════════════════════════════════════"
	@echo "  BUILD: $(1)"
	@echo "══════════════════════════════════════════════════"
	@mkdir -p $(BUILDS_DIR)/$(1)
	@mkdir -p $(OUTPUT_DIR)
	@echo "🧹 Limpiando build anterior..."
	@rm -rf $(BUILDS_DIR)/$(1)/*
	@echo "🔨 Configurando CMake..."
	cd $(BUILDS_DIR)/$(1) && cmake $(PROJECT_DIR) \
		$(COMMON_CMAKE_ARGS) \
		$(2)
	@echo "⚙️  Compilando..."
	cd $(BUILDS_DIR)/$(1) && cmake --build . --config Release -j $(JOBS) --target grpc-server
	@echo "📦 Copiando binario a output/..."
	cp -fv $(BUILDS_DIR)/$(1)/grpc-server $(OUTPUT_DIR)/ik-llama-cpp-$(1)
	@echo "✅ Build completado: $(OUTPUT_DIR)/ik-llama-cpp-$(1)"
endef

# ---------------------------------------------------------------------------
# VARIANTES ESPECÍFICAS (todas incluyen OpenBLAS estático si está disponible)
# ---------------------------------------------------------------------------

avx2: llama.cpp apply-patches
	$(call build_variant,avx2,$(FLAGS_AVX2) $(OPENBLAS_FLAGS))

avx512: llama.cpp apply-patches
	$(call build_variant,avx512,$(FLAGS_AVX512) $(OPENBLAS_FLAGS))

fallback: llama.cpp apply-patches
	$(call build_variant,fallback,$(FLAGS_FALLBACK) $(OPENBLAS_FLAGS))

2690v4: llama.cpp apply-patches
	$(call build_variant,2690v4,$(FLAGS_2690V4))

# ---------------------------------------------------------------------------
# LIMPIEZA
# ---------------------------------------------------------------------------

clean:
	@echo "🧹 Limpiando builds..."
	@rm -rf $(BUILDS_DIR)/*
	@rm -rf $(OUTPUT_DIR)/*
	@echo "✅ Limpieza completada"

purge: clean
	@echo "🗑️  Eliminando llama.cpp clonado..."
	@rm -rf $(PROJECT_DIR)/llama.cpp
	@echo "✅ Purge completado"

# ---------------------------------------------------------------------------
# DIAGNÓSTICO
# ---------------------------------------------------------------------------

diagnose:
	@echo "══════════════════════════════════════════════════"
	@echo "  Diagnóstico de Rutas y OpenBLAS"
	@echo "══════════════════════════════════════════════════"
	@echo "PROJECT_DIR:       $(PROJECT_DIR)"
	@echo "INSTALLED_PACKAGES:$(INSTALLED_PACKAGES)"
	@echo "GRPC_CMAKE_DIR:    $(GRPC_CMAKE_DIR)"
	@echo "OPENBLAS_ROOT:     $(OPENBLAS_ROOT)"
	@echo "OPENBLAS_FOUND:    $(OPENBLAS_FOUND)"
	@echo "OPENBLAS_LIBDIR:   $(OPENBLAS_LIBDIR)"
	@echo "OPENBLAS_STATIC_LIB: $(OPENBLAS_STATIC_LIB)"
	@echo ""
	@echo "── Verificando archivos críticos ──"
	@test -f $(INSTALLED_PACKAGES)/bin/protoc && echo "✅ protoc" || echo "❌ protoc"
	@test -f $(INSTALLED_PACKAGES)/bin/grpc_cpp_plugin && echo "✅ grpc_cpp_plugin" || echo "❌ grpc_cpp_plugin"
	@test -L $(PROJECT_DIR)/backend.proto && echo "✅ backend.proto (symlink)" || echo "❌ backend.proto"
	@test -d $(PROJECT_DIR)/grpccod && echo "✅ grpccod/" || echo "❌ grpccod/"
	@test -d $(PROJECT_DIR)/commoncod && echo "✅ commoncod/" || echo "❌ commoncod/"
	@test -d $(PROJECT_DIR)/llama.cpp && echo "✅ llama.cpp/" || echo "  ⚠️  llama.cpp/ (no clonado aún)"
	@test -f "$(OPENBLAS_STATIC_LIB)" && echo "✅ libopenblas.a (estático)" || echo "  ⚠️  libopenblas.a no encontrado (compilación sin BLAS estática)"
