// grpccod/service.h
#pragma once

#include <grpcpp/grpcpp.h>
#include "backend.grpc.pb.h"
#include <atomic>

// Forward declaration
struct server_context;

// ← CRÍTICO: Declarar loaded_model como extern
extern std::atomic<bool> loaded_model;

class BackendServiceImpl final : public backend::Backend::Service {
public:
    explicit BackendServiceImpl(server_context& ctx);

    // Health check
    grpc::Status Health(
        grpc::ServerContext* context,
        const backend::HealthMessage* request,
        backend::Reply* reply
    ) override;

    // ← CRÍTICO: Status() para el WatchDog de LocalAI
    grpc::Status Status(
        grpc::ServerContext* context,
        const backend::HealthMessage* request,
        backend::StatusResponse* response
    ) override;

    // Cargar modelo
    grpc::Status LoadModel(
        grpc::ServerContext* context,
        const backend::ModelOptions* request,
        backend::Result* result
    ) override;

    // Inferencia (streaming)
    grpc::Status PredictStream(
        grpc::ServerContext* context,
        const backend::PredictOptions* request,
        grpc::ServerWriter<backend::Reply>* writer
    ) override;

    // Inferencia (blocking)
    grpc::Status Predict(
        grpc::ServerContext* context,
        const backend::PredictOptions* request,
        backend::Reply* reply
    ) override;

    // Embeddings
    grpc::Status Embedding(
        grpc::ServerContext* context,
        const backend::PredictOptions* request,
        backend::EmbeddingResult* embeddingResult
    ) override;

    // Tokenización
    grpc::Status TokenizeString(
        grpc::ServerContext* context,
        const backend::PredictOptions* request,
        backend::TokenizationResponse* response
    ) override;

    // Métricas
    grpc::Status GetMetrics(
        grpc::ServerContext* context,
        const backend::MetricsRequest* request,
        backend::MetricsResponse* response
    ) override;

    // Metadata del modelo
    grpc::Status ModelMetadata(
        grpc::ServerContext* context,
        const backend::ModelOptions* request,
        backend::ModelMetadataResponse* response
    ) override;

private:
    server_context& ctx_server;
};
