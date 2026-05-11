// grpccod/embedding.h
// Declaración de Embedding()

#pragma once

#include "backend.pb.h"

// Forward declaration - NO incluir server-context.h aquí
struct server_context;

// ===== Funciones =====
void handle_embedding(server_context &llama, const backend::PredictOptions* request, backend::EmbeddingResult* embeddingResult);
