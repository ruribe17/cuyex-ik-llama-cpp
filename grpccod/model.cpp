// grpccod/model.cpp
// Implementación de funciones de modelo: LoadModel, TokenizeString, GetMetrics, ModelMetadata
//
// CARACTERÍSTICAS:
// 1. Carga de modelo con soporte para mmproj (multimodal).
// 2. Tokenización de strings.
// 3. Métricas del servidor.
// 4. Metadata del modelo.

#include "grpccod/model.h"
#include "commoncod/proto_mapping.h"
#include "examples/server/server-context.h"
#include "common/log.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>

// Variables globales para sincronización de carga de modelo
extern std::mutex model_mutex;
extern std::condition_variable model_cv;
extern std::atomic<bool> loaded_model;

/**
 * @brief Maneja la carga de un modelo desde una request gRPC.
 * 
 * Esta función:
 * 1. Configura los parámetros del modelo desde el proto.
 * 2. Carga el modelo y el contexto.
 * 3. Inicializa el server_context.
 * 4. Notifica a otros threads que el modelo está listo.
 * 
 * @param llama Referencia al server_context.
 * @param request Opciones del modelo del proto (ModelOptions).
 * @param result Resultado a rellenar (success, message).
 */
void handle_load_model(server_context &llama, const backend::ModelOptions* request, backend::Result* result) {
    gpt_params params;
    params.model              = request->modelfile();
    params.n_ctx              = request->contextsize();
    params.n_threads          = request->threads();
    params.n_gpu_layers       = request->ngpulayers();
    params.n_batch            = request->nbatch();
    params.embedding          = request->embeddings();

    // ✅ CRÍTICO: mmproj debe estar en params para que server_context cargue el contexto multimodal
    if (!request->mmproj().empty()) {
        params.mmproj.path = request->mmproj();
        LOG_INF("handle_load_model: mmproj=%s", params.mmproj.path.c_str());
    }

    LOG_INF("handle_load_model: starting, model=%s\n", params.model.c_str());

    if (!llama.load_model(params)) {
        LOG_ERR("handle_load_model: failed to load model\n");
        result->set_success(false);
        result->set_message("Failed to load model");
        return;
    }

    // Inicializar el contexto del servidor
    llama.init();

    // ✅ CRÍTICO: Set success ANTES del mensaje
    result->set_success(true);
    result->set_message("Loading succeeded");

    // Notificar que el modelo está cargado
    {
        std::lock_guard<std::mutex> lock(model_mutex);
        loaded_model = true;
    }
    model_cv.notify_all();

    LOG_INF("handle_load_model: model loaded successfully, success=%d\n", result->success());
}

/**
 * @brief Maneja la tokenización de un string.
 * 
 * Esta función convierte un prompt de texto a tokens usando el vocabulario del modelo.
 * Se usa para debugging y para clientes que necesitan saber el número de tokens.
 * 
 * @param llama Referencia al server_context.
 * @param request Opciones de predicción del proto (contiene el prompt).
 * @param response Respuesta a rellenar con los tokens y longitud.
 */
void handle_tokenize_string(server_context &llama, const backend::PredictOptions* request, backend::TokenizationResponse* response) {
    nlohmann::json data = parse_options(false, request, llama);
    std::string prompt = data["prompt"].get<std::string>();

    LOG_INF("handle_tokenize_string: prompt_length=%zu\n", prompt.size());

    // Tokenizar usando el contexto del servidor
    std::vector<llama_token> tokens = llama.tokenize(prompt, true);

    if (tokens.empty()) {
        LOG_ERR("handle_tokenize_string: tokenized empty\n");
        return;
    }

    // Llenar la respuesta
    for (int i = 0; i < (int)tokens.size(); i++) {
        response->add_tokens(tokens[i]);
    }
    response->set_length(tokens.size());
}

/**
 * @brief Maneja la request de métricas del servidor.
 * 
 * Esta función:
 * 1. Envía una tarea SERVER_TASK_TYPE_METRICS a la cola.
 * 2. Espera la respuesta con las métricas de todos los slots.
 * 3. Extrae las métricas y las pone en la respuesta gRPC.
 * 
 * @param llama Referencia al server_context.
 * @param request Request de métricas (actualmente no usada).
 * @param response Respuesta a rellenar con las métricas.
 */
void handle_get_metrics(server_context &llama, const backend::MetricsRequest* /*request*/, backend::MetricsResponse* response) {
    // 1. ✅ Crear tarea de métricas
    int task_id = llama.queue_tasks.get_new_id();
    
    server_task task(SERVER_TASK_TYPE_METRICS);
    task.id = task_id;
    task.id_target = -1;  // -1 = métricas de todos los slots
    
    llama.queue_results.add_waiting_task_id(task_id);
    llama.queue_tasks.post(std::move(task));
    
    // 2. ✅ Esperar resultado (usar recv_with_timeout para obtener pointer)
    std::unordered_set<int> id_tasks = {task_id};
    server_task_result_ptr result = llama.queue_results.recv_with_timeout(id_tasks, 5000);  // 5 segundos timeout
    llama.queue_results.remove_waiting_task_id(task_id);
    
    if (result == nullptr || result->is_error()) {
        LOG_WRN("handle_get_metrics: error or null result");
        response->set_slot_id(0);
        response->set_prompt_json_for_slot("");
        response->set_tokens_per_second(0);
        response->set_tokens_generated(0);
        response->set_prompt_tokens_processed(0);
        return;
    }
    
    // 3. ✅ Extraer métricas del resultado JSON
    // El resultado tiene: {"idle": N, "processing": N, "slots": [...], "n_prompt_tokens_processed_total": N, ...}
    try {
        json result_json = result->to_json();
        
        // Calcular tokens por segundo (promedio global)
        uint64_t n_prompt_total = result_json.value("n_prompt_tokens_processed_total", 0);
        uint64_t t_prompt_total = result_json.value("t_prompt_processing_total", 0);
        uint64_t n_predicted_total = result_json.value("n_tokens_predicted_total", 0);
        
        float tokens_per_second = 0.0f;
        if (t_prompt_total > 0) {
            // tokens por segundo = total tokens / total tiempo en segundos
            tokens_per_second = static_cast<float>(n_prompt_total + n_predicted_total) / (static_cast<float>(t_prompt_total) / 1000.0f);
        }
        
        response->set_slot_id(0);
        response->set_prompt_json_for_slot("");
        response->set_tokens_per_second(tokens_per_second);
        response->set_tokens_generated(static_cast<int32_t>(n_predicted_total));
        response->set_prompt_tokens_processed(static_cast<int32_t>(n_prompt_total));
        
        LOG_INF("handle_get_metrics: tokens_per_second=%.2f, generated=%lu, prompt=%lu",
                tokens_per_second, n_predicted_total, n_prompt_total);
    } catch (const std::exception& e) {
        LOG_ERR("handle_get_metrics: failed to parse result: %s", e.what());
        response->set_slot_id(0);
        response->set_prompt_json_for_slot("");
        response->set_tokens_per_second(0);
        response->set_tokens_generated(0);
        response->set_prompt_tokens_processed(0);
    }
}

/**
 * @brief Maneja la request de metadata del modelo.
 * 
 * Retorna información sobre las capacidades del modelo cargado.
 * Actualmente retorna valores básicos - puede extenderse para incluir
 * información del chat template, capacidades multimodales, etc.
 * 
 * @param llama Referencia al server_context.
 * @param request Opciones del modelo (actualmente no usadas).
 * @param response Respuesta a rellenar con la metadata.
 */
void handle_model_metadata(server_context &llama, const backend::ModelOptions* /*request*/, backend::ModelMetadataResponse* response) {
    response->set_supports_thinking(false);
    response->set_rendered_template("");
}
