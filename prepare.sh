#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
BACKEND_PROTO_SRC="/root/backendikllama-cpp2/LocalAI/backend/backend.proto"
BACKEND_PROTO_LINK="$PROJECT_DIR/backend.proto"

echo "════════════════════════════════════════════════════"
echo "  📋 prepare.sh (Modo Out-of-Source)"
echo "════════════════════════════════════════════════════"

# ---------------------------------------------------------------------------
# 1. VERIFICAR/CREAR SYMLINK DE backend.proto
# ---------------------------------------------------------------------------
echo "🔗 Verificando symlink de backend.proto..."

if [ -L "$BACKEND_PROTO_LINK" ]; then
    # El symlink existe, verificar que no esté roto
    if [ ! -e "$BACKEND_PROTO_LINK" ]; then
        echo "  ⚠️  Symlink roto. Recreando..."
        rm "$BACKEND_PROTO_LINK"
        ln -s "$BACKEND_PROTO_SRC" "$BACKEND_PROTO_LINK"
    else
        echo "  ✅ Symlink existente y válido"
    fi
elif [ -e "$BACKEND_PROTO_LINK" ]; then
    # Es un archivo real, no un symlink
    echo "  ⚠️  backend.proto es un archivo real, no un symlink. Se mantiene."
else
    # No existe, crear symlink
    if [ -f "$BACKEND_PROTO_SRC" ]; then
        ln -s "$BACKEND_PROTO_SRC" "$BACKEND_PROTO_LINK"
        echo "  ✅ Symlink creado: $BACKEND_PROTO_LINK -> $BACKEND_PROTO_SRC"
    else
        echo "  ❌ ERROR: backend.proto no encontrado en $BACKEND_PROTO_SRC"
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# 2. VERIFICAR ESTRUCTURA DE DIRECTORIOS
# ---------------------------------------------------------------------------
echo "🔍 Verificando estructura de directorios..."

check_dir() {
    if [ -d "$1" ]; then
        echo "  ✅ $2"
    else
        echo "  ❌ ERROR: $2 no encontrado en $1"
        exit 1
    fi
}

check_file() {
    if [ -e "$1" ]; then
        echo "  ✅ $2"
    else
        echo "  ❌ ERROR: $2 no encontrado en $1"
        exit 1
    fi
}

check_dir "$PROJECT_DIR/llama.cpp" "llama.cpp/"
check_dir "$PROJECT_DIR/grpccod" "grpccod/"
check_dir "$PROJECT_DIR/common" "common/"
check_dir "$PROJECT_DIR/server" "server/"
check_file "$PROJECT_DIR/main.cpp" "main.cpp"
check_file "$PROJECT_DIR/CMakeLists.txt" "CMakeLists.txt"
check_file "$PROJECT_DIR/Makefile" "Makefile"

# ---------------------------------------------------------------------------
# 3. VERIFICAR DEPENDENCIAS EXTERNAS
# ---------------------------------------------------------------------------
echo "🔍 Verificando dependencias externas (../grpc/)..."

GRPC_DIR="$(dirname "$PROJECT_DIR")/grpc"
INSTALLED_PACKAGES="$GRPC_DIR/installed_packages"

if [ -d "$INSTALLED_PACKAGES" ]; then
    echo "  ✅ grpc/installed_packages encontrado"
    check_file "$INSTALLED_PACKAGES/bin/protoc" "protoc"
    check_file "$INSTALLED_PACKAGES/bin/grpc_cpp_plugin" "grpc_cpp_plugin"
else
    echo "  ⚠️  ADVERTENCIA: grpc/installed_packages no encontrado."
    echo "     Asegúrate de haber compilado gRPC en ../grpc/ antes de build."
fi

# ---------------------------------------------------------------------------
# 4. FINALIZAR
# ---------------------------------------------------------------------------
echo "════════════════════════════════════════════════════"
echo "  ✅ prepare.sh completado"
echo "  📝 Nota: Con la estructura out-of-source, ya no es necesario"
echo "     ejecutar prepare.sh antes de cada build. El Makefile"
echo "     gestiona todo directamente."
echo "════════════════════════════════════════════════════"
