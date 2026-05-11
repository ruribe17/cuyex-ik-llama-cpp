// grpccod/service.cpp
#include "grpccod/service.h"
#include "grpccod/predict.h"
#include "grpccod/embedding.h"
#include "grpccod/model.h"
#include "commoncod/proto_mapping.h"
#include "commoncod/utils.h"

#include <mutex>
#include <condition_variable>
#include <atomic>

extern std::mutex model_mutex;
extern std::condition_variable model_cv;
extern std::atomic<bool> loaded_model;  // ← CRÍTICO

// Constructor
BackendServiceImpl::BackendServiceImpl(server_context& ctx) : ctx_server(ctx) {}

// Health check
grpc::Status BackendServiceImpl::Health(
    grpc::ServerContext* /*context*/,
    const backend::HealthMessage* /*request*/,
    backend::Reply* reply
) {
    reply->set_message("OK");
    return grpc::Status::OK;
}

// ← CRÍTICO: Status() para el WatchDog de LocalAI
grpc::Status BackendServiceImpl::Status(
    grpc::ServerContext* /*context*/,
    const backend::HealthMessage* /*request*/,
    backend::StatusResponse* response
) {
    if (!loaded_model.load()) {
        // Durante la carga: BUSY → WatchDog NO mata la conexión
        response->set_state(backend::StatusResponse::BUSY);
    } else {
        // Modelo listo: READY
        response->set_state(backend::StatusResponse::READY);
    }
    return grpc::Status::OK;
}

// LoadModel
grpc::Status BackendServiceImpl::LoadModel(
    grpc::ServerContext* /*context*/,
    const backend::ModelOptions* request,
    backend::Result* result
) {
    handle_load_model(ctx_server, request, result);
    return grpc::Status::OK;
}

// PredictStream
grpc::Status BackendServiceImpl::PredictStream(
    grpc::ServerContext* /*context*/,
    const backend::PredictOptions* request,
    grpc::ServerWriter<backend::Reply>* writer
) {
    handle_predict_stream(ctx_server, request, writer);
    return grpc::Status::OK;
}

// Predict
grpc::Status BackendServiceImpl::Predict(
    grpc::ServerContext* /*context*/,
    const backend::PredictOptions* request,
    backend::Reply* reply
) {
    handle_predict(ctx_server, request, reply);
    return grpc::Status::OK;
}

// Embedding
grpc::Status BackendServiceImpl::Embedding(
    grpc::ServerContext* /*context*/,
    const backend::PredictOptions* request,
    backend::EmbeddingResult* embeddingResult
) {
    handle_embedding(ctx_server, request, embeddingResult);
    return grpc::Status::OK;
}

// TokenizeString
grpc::Status BackendServiceImpl::TokenizeString(
    grpc::ServerContext* /*context*/,
    const backend::PredictOptions* request,
    backend::TokenizationResponse* response
) {
    handle_tokenize_string(ctx_server, request, response);
    return grpc::Status::OK;
}

// GetMetrics
grpc::Status BackendServiceImpl::GetMetrics(
    grpc::ServerContext* /*context*/,
    const backend::MetricsRequest* request,
    backend::MetricsResponse* response
) {
    handle_get_metrics(ctx_server, request, response);
    return grpc::Status::OK;
}

// ModelMetadata
grpc::Status BackendServiceImpl::ModelMetadata(
    grpc::ServerContext* /*context*/,
    const backend::ModelOptions* request,
    backend::ModelMetadataResponse* response
) {
    handle_model_metadata(ctx_server, request, response);
    return grpc::Status::OK;
}
