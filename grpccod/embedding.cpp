// grpccod/embedding.cpp
// Implementación de Embedding()

#include "grpccod/embedding.h"
#include "commoncod/proto_mapping.h"  // ← Para parse_options
#include "examples/server/server-context.h"
#include "common/log.h"
#include <vector>
#include <string>

void handle_embedding(
    server_context &llama,
    const backend::PredictOptions* request,
    backend::EmbeddingResult* embeddingResult) {
    
    nlohmann::json data = parse_options(false, request, llama);
    std::string prompt = data["prompt"].get<std::string>();
    
    LOG_INFO("handle_embedding: starting", {{"prompt_length", prompt.size()}});
    
    std::vector<llama_token> tokens = llama.tokenize(prompt, true);
    
    if (tokens.empty()) {
        LOG_ERR("handle_embedding: tokenized empty");
        return;
    }
    
    server_task task(SERVER_TASK_TYPE_EMBEDDING);
    task.id = llama.queue_tasks.get_new_id();
    task.tokens = server_tokens(tokens, false);
    task.embedding = true;
    task.data = data;
    
    llama.queue_results.add_waiting_task_id(task.id);
    llama.queue_tasks.post(std::move(task));
    
    server_task_result result = llama.queue_results.recv(task.id);
    llama.queue_results.remove_waiting_task_id(task.id);
    
    if (!result.error && result.stop) {
        auto embedding_json = result.data.value("embedding", nlohmann::json::array());
        
        if (embedding_json.is_array() && !embedding_json.empty()) {
            if (embedding_json[0].is_array()) {
                for (float val : embedding_json[0]) {
                    embeddingResult->add_embeddings(val);
                }
            } else {
                for (float val : embedding_json) {
                    embeddingResult->add_embeddings(val);
                }
            }
        }
    }
}
