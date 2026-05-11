// grpccod/predict.h
// Declaración de Predict() y PredictStream()

#pragma once

#include "backend.pb.h"
#include <grpcpp/grpcpp.h>
#include "json.hpp"
#include "llama.h"

// Forward declaration - NO incluir server-context.h aquí
struct server_context;

// ===== Tipos auxiliares =====
std::vector<std::vector<llama_token>> tokenize_input_prompts(
    const llama_vocab *vocab,
    llama_context *ctx,
    const nlohmann::json &json_prompt,
    bool add_bos,
    bool force_special);

// ===== Funciones principales =====
void handle_predict(server_context &ctx, const backend::PredictOptions* request, backend::Reply* reply);

void handle_predict_stream(server_context &ctx, const backend::PredictOptions* request, grpc::ServerWriter<backend::Reply>* writer);
