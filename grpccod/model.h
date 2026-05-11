// grpccod/model.h
// Declaración de funciones de modelo

#pragma once

#include "backend.pb.h"

// Forward declaration
struct server_context;

// Funciones
void handle_load_model(server_context &llama, const backend::ModelOptions* request, backend::Result* result);
void handle_tokenize_string(server_context &llama, const backend::PredictOptions* request, backend::TokenizationResponse* response);
void handle_get_metrics(server_context &llama, const backend::MetricsRequest* request, backend::MetricsResponse* response);
void handle_model_metadata(server_context &llama, const backend::ModelOptions* request, backend::ModelMetadataResponse* response);
