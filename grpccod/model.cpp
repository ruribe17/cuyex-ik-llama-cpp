// grpccod/model.cpp
#include "grpccod/model.h"
#include "commoncod/proto_mapping.h"
#include "examples/server/server-context.h"
#include "common/log.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>

extern std::mutex model_mutex;
extern std::condition_variable model_cv;
extern std::atomic<bool> loaded_model;

void handle_load_model(server_context &llama, const backend::ModelOptions* request, backend::Result* result) {
    gpt_params params;
    params.model              = request->modelfile();
    params.n_ctx              = request->contextsize();
    params.n_threads          = request->threads();
    params.n_gpu_layers       = request->ngpulayers();
    params.n_batch            = request->nbatch();
    params.embedding          = request->embeddings();

    LOG_INF("handle_load_model: starting, model=%s\n", params.model.c_str());

    if (!llama.load_model(params)) {
        LOG_ERR("handle_load_model: failed to load model\n");
        result->set_success(false);  // ← AGREGADO
        result->set_message("Failed to load model");
        return;
    }

    llama.init();

    // ✅ CRÍTICO: Set success ANTES del mensaje
    result->set_success(true);  // ← ESTO FALTABA
    result->set_message("Loading succeeded");

    {
        std::lock_guard<std::mutex> lock(model_mutex);
        loaded_model = true;
    }
    model_cv.notify_all();

    LOG_INF("handle_load_model: model loaded successfully, success=%d\n", result->success());
}

void handle_tokenize_string(server_context &llama, const backend::PredictOptions* request, backend::TokenizationResponse* response) {
    nlohmann::json data = parse_options(false, request, llama);
    std::string prompt = data["prompt"].get<std::string>();

    LOG_INF("handle_tokenize_string: prompt_length=%zu\n", prompt.size());

    std::vector<llama_token> tokens = llama.tokenize(prompt, true);

    if (tokens.empty()) {
        LOG_ERR("handle_tokenize_string: tokenized empty\n");
        return;
    }

    for (int i = 0; i < (int)tokens.size(); i++) {
        response->add_tokens(tokens[i]);
    }
    response->set_length(tokens.size());
}

void handle_get_metrics(server_context &llama, const backend::MetricsRequest* /*request*/, backend::MetricsResponse* response) {
    response->set_slot_id(0);
    response->set_prompt_json_for_slot("");
    response->set_tokens_per_second(0);
    response->set_tokens_generated(0);
    response->set_prompt_tokens_processed(0);
}

void handle_model_metadata(server_context &llama, const backend::ModelOptions* /*request*/, backend::ModelMetadataResponse* response) {
    response->set_supports_thinking(false);
    response->set_rendered_template("");
}
