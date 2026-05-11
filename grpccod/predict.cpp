// grpccod/predict.cpp
#include "grpccod/predict.h"
#include <grpcpp/support/sync_stream.h>
#include "examples/server/server-context.h"
#include "commoncod/proto_mapping.h"
#include "common/chat.h"
#include "common/log.h"
#include <iostream>
#include <vector>
#include <string>
#include <memory>

std::vector<std::vector<llama_token>> tokenize_input_prompts(
    const llama_vocab *vocab,
    llama_context *ctx,
    const nlohmann::json &json_prompt,
    bool add_bos,
    bool force_special) {
    
    std::vector<std::vector<llama_token>> tokenized;
    
    if (json_prompt.is_array()) {
        for (const auto& p : json_prompt) {
            if (p.is_string()) {
                auto s = p.get<std::string>();
                auto tokens = common_tokenize(vocab, s, add_bos, force_special);
                tokenized.push_back(tokens);
            } else if (p.is_number_integer()) {
                tokenized.push_back({p.get<llama_token>()});
            }
        }
    } else if (json_prompt.is_string()) {
        auto s = json_prompt.get<std::string>();
        auto tokens = common_tokenize(vocab, s, add_bos, force_special);
        tokenized.push_back(tokens);
    }
    
    return tokenized;
}

void handle_predict_stream(
    server_context &ctx,
    const backend::PredictOptions* request,
    grpc::ServerWriter<backend::Reply>* writer) {
    
    nlohmann::json data = parse_options(true, request, ctx);
    data["stream"] = true;
    
    if (request->usetokenizertemplate() &&
        request->messages_size() > 0 &&
        ctx.chat_params.tmpls != nullptr) {
        
        auto messages_json = data.value("messages", nlohmann::json::array());
        auto messages = common_chat_msgs_parse_oaicompat(messages_json);

        common_chat_templates_inputs inputs;
        inputs.messages = messages;
        inputs.add_generation_prompt = true;
        inputs.use_jinja = true;
        inputs.enable_thinking = false;

        auto chat_params_result = common_chat_templates_apply(ctx.chat_params.tmpls.get(), inputs);
        data["prompt"] = chat_params_result.prompt;

        LOG_INF("handle_predict_stream: applied chat template, prompt_length=%zu\n",
                chat_params_result.prompt.size());
    }
    
    std::string prompt = data.value("prompt", std::string{});
    if (prompt.empty()) {
        LOG_WRN("handle_predict_stream: empty prompt\n");
        return;
    }
    
    auto tokenized = tokenize_input_prompts(
        llama_model_get_vocab(ctx.model),
        ctx.ctx,
        data["prompt"],
        true,
        true
    );
    
    if (tokenized.empty() || tokenized[0].empty()) {
        LOG_WRN("handle_predict_stream: tokenized empty\n");
        return;
    }
    
    server_task task(SERVER_TASK_TYPE_COMPLETION);
    task.id = ctx.queue_tasks.get_new_id();
    task.tokens = server_tokens(tokenized[0], false);
    task.params.stream = true;
    task.params.cache_prompt = true;
    task.params.n_predict = data.value("n_predict", -1);
    task.data = data;
    
    ctx.queue_results.add_waiting_task_id(task.id);
    ctx.queue_tasks.post(std::move(task));
    
    // ← BUCLE DE STREAMING CORREGIDO
    while (true) {
        auto result_ptr = ctx.queue_results.recv_with_timeout({task.id}, 1000);
        
        if (!result_ptr) {
            break;
        }
        
        // ← CRÍTICO: Check is_final() PRIMERO
        if (result_ptr->is_stop()) {
            auto* final = dynamic_cast<server_task_result_cmpl_final*>(result_ptr.get());
            if (final) {
                backend::Reply final_reply;
                final_reply.set_message("");  // ← VACÍO
                final_reply.set_tokens(final->timings.predicted_n);
                final_reply.set_prompt_tokens(final->timings.prompt_n);
                final_reply.set_timing_prompt_processing(final->timings.prompt_ms);
                final_reply.set_timing_token_generation(final->timings.predicted_ms);
                
                if (final_reply.tokens() > 0) {
                    writer->Write(final_reply);
                }
            }
            break;  // ← Salir INMEDIATAMENTE
        }
        
        // Para parciales, usar to_json() para extraer content
        std::string content;
        auto* partial = dynamic_cast<server_task_result_cmpl_partial*>(result_ptr.get());
        
        if (partial) {
            json result_json = partial->to_json_non_oaicompat_partial();
            content = result_json.value("content", "");
        } else {
            content = result_ptr->content;
        }
        
        if (!content.empty()) {
            backend::Reply reply;
            reply.set_message(content);
            writer->Write(reply);
        }
    }
    
    ctx.queue_results.remove_waiting_task_id(task.id);
}

void handle_predict(
    server_context &ctx,
    const backend::PredictOptions* request,
    backend::Reply* reply) {
    
    nlohmann::json data = parse_options(false, request, ctx);
    data["stream"] = false;
    
    if (request->usetokenizertemplate() &&
        request->messages_size() > 0 &&
        ctx.chat_params.tmpls != nullptr) {
        
        auto messages_json = data.value("messages", nlohmann::json::array());
        auto messages = common_chat_msgs_parse_oaicompat(messages_json);

        common_chat_templates_inputs inputs;
        inputs.messages = messages;
        inputs.add_generation_prompt = true;
        inputs.use_jinja = true;
        inputs.enable_thinking = false;

        auto chat_params_result = common_chat_templates_apply(ctx.chat_params.tmpls.get(), inputs);
        data["prompt"] = chat_params_result.prompt;
    }
    
    std::string prompt = data.value("prompt", std::string{});
    if (prompt.empty()) {
        reply->set_message("");
        return;
    }
    
    auto tokenized = tokenize_input_prompts(
        llama_model_get_vocab(ctx.model),
        ctx.ctx,
        data["prompt"],
        true,
        true
    );
    
    if (tokenized.empty() || tokenized[0].empty()) {
        reply->set_message("");
        return;
    }
    
    server_task task(SERVER_TASK_TYPE_COMPLETION);
    task.id = ctx.queue_tasks.get_new_id();
    task.tokens = server_tokens(tokenized[0], false);
    task.params.stream = false;
    task.params.cache_prompt = true;
    task.params.n_predict = data.value("n_predict", -1);
    task.data = data;
    
    ctx.queue_results.add_waiting_task_id(task.id);
    ctx.queue_tasks.post(std::move(task));
    
    std::string full_content;
    while (true) {
        auto result_ptr = ctx.queue_results.recv_with_timeout({task.id}, 30000);
        if (!result_ptr) break;
        
        if (result_ptr->is_stop()) {
            auto* final = dynamic_cast<server_task_result_cmpl_final*>(result_ptr.get());
            if (final) full_content = final->content;
            break;
        }
        
        auto* partial = dynamic_cast<server_task_result_cmpl_partial*>(result_ptr.get());
        if (partial) {
            json result_json = partial->to_json_non_oaicompat_partial();
            full_content += result_json.value("content", "");
        }
    }
    
    ctx.queue_results.remove_waiting_task_id(task.id);
    reply->set_message(full_content);
}
