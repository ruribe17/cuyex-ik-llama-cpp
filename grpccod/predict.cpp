// grpccod/predict.cpp
// Implementación de Predict() y PredictStream() con soporte completo de Function Calling, Reasoning y Multimodal
// 
// CARACTERÍSTICAS:
// 1. Inyección de 'tools' y 'tool_choice' en el chat template (Input).
// 2. Parseo nativo de tool calls en respuesta blocking (Output).
// 3. Soporte completo de Reasoning/Thinking (enable_thinking, reasoning_format).
// 4. Soporte Multimodal con process_mtmd_prompt para Qwen3-VL y otros modelos visuales.
// 5. Uso de helpers de llama.cpp (common_chat_tools_parse_oaicompat, common_chat_parse, mtmd_default_marker).
// 6. Extracción y decodificación de imágenes desde data["image_data"] para procesamiento multimodal.

#include "grpccod/predict.h"
#include <grpcpp/support/sync_stream.h>
#include "examples/server/server-context.h"
#include "examples/server/server-common.h"  // ← Para tokenize_input_prompts, process_mtmd_prompt, base64_decode, server_tokens
#include "commoncod/proto_mapping.h"
#include "common/chat.h"
#include "common/log.h"
#include "nlohmann/json.hpp"
#include "examples/mtmd/mtmd.h"  // ← Para mtmd_default_marker()
#include <iostream>
#include <vector>
#include <string>
#include <memory>

// =============================================================================
// FUNCIONES AUXILIARES
// =============================================================================

/**
 * @brief Obtiene el marcador por defecto para imágenes en el prompt.
 * 
 * Retorna "<__media__>" que mtmd_tokenize() busca para insertar tokens de imagen.
 * Este marcador es definido por llama.cpp en mtmd.cpp.
 * 
 * @return std::string Marcador de imagen (por defecto: "<__media__>").
 */
static std::string get_media_marker() {
    return std::string(mtmd_default_marker());
}

/**
 * @brief Inserta el marcador de imagen en el último mensaje de usuario.
 * 
 * Esto es CRÍTICO para que mtmd_tokenize() detecte y procese las imágenes.
 * Sin este marcador, las imágenes se ignoran completamente.
 * 
 * NOTA: Esta función SOLO inserta marcadores si has_mctx es true.
 * Esto evita overhead innecesario en modelos de solo texto y previene
 * errores si hay imágenes pero el modelo no soporta multimodal.
 * 
 * @param data JSON con messages e image_data (se modifica in-place).
 * @param has_mctx Indicador de si el contexto multimodal está cargado.
 */
static void inject_image_markers(nlohmann::json &data, bool has_mctx) {
    // ✅ DETECCIÓN: Solo insertar marcadores si hay contexto multimodal
    if (!has_mctx) {
        LOG_VERBOSE("inject_image_markers: mctx=null, skipping image markers", {});
        return;  // ← Sin overhead para modelos de solo texto
    }
    
    if (!data.contains("image_data") || data["image_data"].empty()) {
        return;
    }
    if (!data.contains("messages") || !data["messages"].is_array()) {
        return;
    }

    std::string marker = get_media_marker();
    std::string marker_block;
    
    // Un marcador por imagen
    for (size_t k = 0; k < data["image_data"].size(); ++k) {
        marker_block += marker;
    }

    // Buscar el último mensaje "user"
    for (int i = (int)data["messages"].size() - 1; i >= 0; --i) {
        if (data["messages"][i].value("role", "") == "user") {
            std::string existing = data["messages"][i].value("content", std::string{});
            if (!existing.empty()) {
                existing += " ";
            }
            existing += marker_block;
            data["messages"][i]["content"] = existing;
            LOG_INF("inject_image_markers: inserted %zu markers in message %d", data["image_data"].size(), i);
            return;
        }
    }

    // Si no hay mensaje user, las imágenes no se procesarán
    LOG_WRN("inject_image_markers: no user message found, images may not be processed");
}

/**
 * @brief Aplica el chat template inyectando las tools correctamente.
 * 
 * CORRECCIÓN CLAVE: Antes no se pasaban las tools a 'inputs',
 * por lo que el modelo nunca veía el schema de las funciones.
 * 
 * Esta función:
 * 1. Parsea mensajes a formato interno de llama.cpp.
 * 2. Construye inputs para el template con messages, tools, tool_choice.
 * 3. Configura enable_thinking y reasoning_format para modelos de reasoning.
 * 4. Aplica el template Jinja y retorna el prompt final.
 * 
 * @param ctx Contexto del servidor.
 * @param data JSON con los parámetros de la request (incluye messages, tools, tool_choice).
 * @param chat_params_result Salida: parámetros de chat resultantes (prompt, grammar, format).
 * @return true si se aplicó el template, false si se usó prompt crudo.
 */
bool apply_chat_template_with_tools(
    server_context &ctx,
    const nlohmann::json &data,
    common_chat_params &chat_params_result) {

    // Verificar si hay messages y template disponible
    if (!data.contains("messages") || 
        !data["messages"].is_array() || 
        data["messages"].empty() ||
        ctx.chat_params.tmpls == nullptr) {
        return false;
    }

    try {
        // 1. Parsear mensajes a formato interno de llama.cpp
        auto messages_json = data.value("messages", nlohmann::json::array());
        // Usar ordered_json para compatibilidad con los helpers de chat.h
        nlohmann::ordered_json messages_ordered = nlohmann::ordered_json::parse(messages_json.dump());
        auto messages = common_chat_msgs_parse_oaicompat(messages_ordered);

        // 2. Construir inputs para el template
        common_chat_templates_inputs inputs;
        inputs.messages = messages;
        inputs.add_generation_prompt = true;
        inputs.use_jinja = true;

        // 3. ✅ REASONING: Configurar enable_thinking
        inputs.enable_thinking = data.value("enable_thinking", false);
        
        // 4. ✅ REASONING: Configurar reasoning_format
        std::string fmt = data.value("reasoning_format", "auto");
        if (fmt == "deepseek") {
            inputs.reasoning_format = COMMON_REASONING_FORMAT_DEEPSEEK;
        } else if (fmt == "deepseek_legacy") {
            inputs.reasoning_format = COMMON_REASONING_FORMAT_DEEPSEEK_LEGACY;
        } else if (fmt == "none") {
            inputs.reasoning_format = COMMON_REASONING_FORMAT_NONE;
        } else {
            // Default: AUTO
            inputs.reasoning_format = COMMON_REASONING_FORMAT_AUTO;
        }

        LOG_INF("apply_chat_template: enable_thinking=%d, reasoning_format=%d (%s)", 
                inputs.enable_thinking, 
                static_cast<int>(inputs.reasoning_format),
                fmt.c_str());

        // 5. ✅ Inyectar TOOLS
        if (data.contains("tools") && !data["tools"].is_null()) {
            try {
                nlohmann::ordered_json tools_ordered = nlohmann::ordered_json::parse(data["tools"].dump());
                inputs.tools = common_chat_tools_parse_oaicompat(tools_ordered);
                
                LOG_INF("apply_chat_template: %zu tools injected", inputs.tools.size());
            } catch (const std::exception& e) {
                LOG_WRN("apply_chat_template: failed to parse tools: %s", e.what());
            }
        }

        // 6. ✅ Inyectar TOOL_CHOICE
        if (data.contains("tool_choice")) {
            try {
                std::string choice_str = data["tool_choice"].get<std::string>();
                inputs.tool_choice = common_chat_tool_choice_parse_oaicompat(choice_str);
                
                LOG_INF("apply_chat_template: tool_choice set to '%s'", choice_str.c_str());
            } catch (const std::exception& e) {
                LOG_WRN("apply_chat_template: failed to parse tool_choice: %s", e.what());
            }
        }

        // 7. Aplicar template
        chat_params_result = common_chat_templates_apply(ctx.chat_params.tmpls.get(), inputs);
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERR("apply_chat_template: exception: %s", e.what());
        return false;
    }
}

/**
 * @brief Parsea el contenido generado extrayendo tool calls y reasoning_content estructurados.
 * 
 * Usa el parser PEG de llama.cpp para extraer tool calls y reasoning del texto.
 * Solo se usa en modo blocking (handle_predict) para simplificar.
 * 
 * En modo streaming, LocalAI (Go) se encarga de parsear los tool calls y reasoning
 * directamente del contenido textual.
 * 
 * @param content Contenido generado por el modelo.
 * @param reasoning_format Formato de reasoning a usar (DEEPSEEK, AUTO, NONE, etc.).
 * @param reply Puntero al mensaje de respuesta gRPC a rellenar.
 */
void parse_tool_calls_from_content(
    const std::string &content,
    common_reasoning_format reasoning_format,
    backend::Reply *reply) {

    if (content.empty()) {
        return;
    }

    try {
        // Configurar parámetros del parser
        common_chat_parser_params parser_params;
        parser_params.format = COMMON_CHAT_FORMAT_CONTENT_ONLY;
        parser_params.parse_tool_calls = true;  // ← Habilitar parseo de tool calls
        
        // ✅ REASONING: Usar el formato configurado
        parser_params.reasoning_format = reasoning_format;
        
        LOG_INF("parse_tool_calls: using reasoning_format=%d", static_cast<int>(reasoning_format));

        // Parsear el contenido completo
        common_chat_msg parsed_msg = common_chat_parse(content, false, parser_params);

        // Enviar si hay tool calls O reasoning_content
        if (!parsed_msg.tool_calls.empty() || !parsed_msg.reasoning_content.empty()) {
            LOG_INF("parse_tool_calls: detected %zu tool calls, %zu chars reasoning", 
                    parsed_msg.tool_calls.size(), 
                    parsed_msg.reasoning_content.size());

            // Crear un ChatDelta para este bloque
            auto *chat_delta = reply->add_chat_deltas();
            
            // ✅ REASONING: Enviar reasoning_content si existe
            if (!parsed_msg.reasoning_content.empty()) {
                chat_delta->set_reasoning_content(parsed_msg.reasoning_content);
            }
            
            // El contenido de texto va en 'content' (puede estar vacío si solo hubo tool calls)
            chat_delta->set_content(parsed_msg.content);

            // Llenar tool_calls
            for (size_t i = 0; i < parsed_msg.tool_calls.size(); ++i) {
                const auto &tc = parsed_msg.tool_calls[i];
                auto *tool_delta = chat_delta->add_tool_calls();
                
                tool_delta->set_index(static_cast<int32_t>(i));
                tool_delta->set_name(tc.name);
                tool_delta->set_arguments(tc.arguments);
                if (!tc.id.empty()) {
                    tool_delta->set_id(tc.id);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_WRN("parse_tool_calls: failed: %s", e.what());
        // Si falla el parseo, el contenido textual ya está en reply->message()
    }
}

// =============================================================================
// FUNCIONES PRINCIPALES
// =============================================================================

void handle_predict_stream(
    server_context &ctx,
    const backend::PredictOptions* request,
    grpc::ServerWriter<backend::Reply>* writer) {
    
    // 1. Parsear opciones del proto a JSON
    nlohmann::json data = parse_options(true, request, ctx);
    data["stream"] = true;
    
    // ✅ DETECCIÓN: ¿Hay contexto multimodal cargado?
    bool has_mctx = (ctx.mctx != nullptr);
    LOG_VERBOSE("handle_predict_stream: has_mctx=%d", has_mctx);
    
    // ✅ CRÍTICO: Insertar marcadores de imagen ANTES del chat template
    // Esto SOLO se ejecuta si has_mctx es true (evita overhead en texto puro)
    inject_image_markers(data, has_mctx);
    
    // 2. Aplicar Chat Template CON TOOLS y REASONING
    common_chat_params chat_params_result;
    if (apply_chat_template_with_tools(ctx, data, chat_params_result)) {
        data["prompt"] = chat_params_result.prompt;
        LOG_INF("handle_predict_stream: applied chat template with tools, prompt_length=%zu",
                chat_params_result.prompt.size());
    }
    
    // 3. Validar prompt
    std::string prompt = data.value("prompt", std::string{});
    if (prompt.empty()) {
        LOG_WRN("handle_predict_stream: empty prompt");
        return;
    }
    
    // 4. ✅ MULTIMODAL: Extraer imágenes de data["image_data"] y decodificar
    std::vector<raw_buffer> files;
    if (data.contains("image_data") && data["image_data"].is_array()) {
        for (const auto& img : data["image_data"]) {
            std::string base64_str = img.get<std::string>();
            
            // Strip del prefijo "data:image/...;base64," si existe
            // Esto permite compatibilidad con formatos data: URI y base64 puro
            size_t comma_pos = base64_str.find(",");
            if (comma_pos != std::string::npos) {
                base64_str = base64_str.substr(comma_pos + 1);
                LOG_VERBOSE("handle_predict_stream: stripped data: URI prefix", {});
            }
            
            // Decodificar base64 a bytes
            auto decoded = base64_decode(base64_str);
            
            // ✅ CORREGIDO: raw_buffer es std::vector<uint8_t>, usar directamente
            files.push_back(std::move(decoded));
            
            LOG_INF("handle_predict_stream: extracted image (%zu bytes)", files.back().size());
        }
    }
    
    // 5. ✅ Tokenización con soporte multimodal
    // Usar process_mtmd_prompt cuando hay mctx Y archivos de imagen
    // De lo contrario, usar tokenize_input_prompts para texto puro
    std::vector<server_tokens> inputs;
    if (has_mctx && !files.empty()) {
        // ✅ MULTIMODAL: Usar process_mtmd_prompt para procesar imágenes
        inputs.push_back(process_mtmd_prompt(ctx.mctx, prompt, std::move(files)));
        LOG_INF("handle_predict_stream: using process_mtmd_prompt with %zu images", files.size());
    } else {
        // ✅ TEXTO PURO: Usar tokenize_input_prompts normal
        nlohmann::json prompt_obj = nlohmann::json::array();
        prompt_obj.push_back(prompt);
        inputs = tokenize_input_prompts(
            llama_model_get_vocab(ctx.model),
            ctx.mctx,  // ← Pasar mctx (puede ser nullptr, la función lo maneja)
            prompt_obj,
            true,
            true
        );
        LOG_VERBOSE("handle_predict_stream: using tokenize_input_prompts (text-only)", {});
    }
    
    if (inputs.empty() || inputs[0].empty()) {
        LOG_WRN("handle_predict_stream: tokenized empty (check mmproj and image markers if applicable)");
        return;
    }
    
    // 6. Crear tarea
    server_task task(SERVER_TASK_TYPE_COMPLETION);
    task.id = ctx.queue_tasks.get_new_id();
    task.tokens = std::move(inputs[0]);  // ← ✅ Usar std::move() (server_tokens no es copiable)
    task.params.stream = true;
    task.params.cache_prompt = true;
    task.params.n_predict = data.value("n_predict", -1);
    task.data = data;
    
    ctx.queue_results.add_waiting_task_id(task.id);
    ctx.queue_tasks.post(std::move(task));
    
    // 7. Bucle de Streaming
    // NOTA: En streaming, enviamos texto plano. LocalAI (Go) parsea los tool calls y reasoning.
    while (true) {
        auto result_ptr = ctx.queue_results.recv_with_timeout({task.id}, 1000);
        
        if (!result_ptr) {
            break;
        }
        
        // Check final
        if (result_ptr->is_stop()) {
            auto* final = dynamic_cast<server_task_result_cmpl_final*>(result_ptr.get());
            if (final) {
                backend::Reply final_reply;
                final_reply.set_message("");  // Vacío en el último chunk
                final_reply.set_tokens(final->timings.predicted_n);
                final_reply.set_prompt_tokens(final->timings.prompt_n);
                final_reply.set_timing_prompt_processing(final->timings.prompt_ms);
                final_reply.set_timing_token_generation(final->timings.predicted_ms);
                
                if (final_reply.tokens() > 0) {
                    writer->Write(final_reply);
                }
            }
            break;
        }
        
        // Extraer contenido parcial
        std::string content;
        auto* partial = dynamic_cast<server_task_result_cmpl_partial*>(result_ptr.get());
        
        if (partial) {
            // Usar to_json para extraer content consistentemente
            nlohmann::json result_json = partial->to_json_non_oaicompat_partial();
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
    
    // 1. Parsear opciones
    nlohmann::json data = parse_options(false, request, ctx);
    data["stream"] = false;
    
    // ✅ DETECCIÓN: ¿Hay contexto multimodal cargado?
    bool has_mctx = (ctx.mctx != nullptr);
    LOG_VERBOSE("handle_predict: has_mctx=%d", has_mctx);
    
    // ✅ CRÍTICO: Insertar marcadores de imagen ANTES del chat template
    inject_image_markers(data, has_mctx);
    
    // 2. Aplicar Chat Template CON TOOLS y REASONING
    common_chat_params chat_params_result;
    if (apply_chat_template_with_tools(ctx, data, chat_params_result)) {
        data["prompt"] = chat_params_result.prompt;
        LOG_INF("handle_predict: applied chat template with tools, prompt_length=%zu",
                chat_params_result.prompt.size());
    }
    
    // 3. Validar prompt
    std::string prompt = data.value("prompt", std::string{});
    if (prompt.empty()) {
        reply->set_message("");
        return;
    }
    
    // 4. ✅ MULTIMODAL: Extraer imágenes de data["image_data"] y decodificar
    std::vector<raw_buffer> files;
    if (data.contains("image_data") && data["image_data"].is_array()) {
        for (const auto& img : data["image_data"]) {
            std::string base64_str = img.get<std::string>();
            
            // Strip del prefijo "data:image/...;base64," si existe
            size_t comma_pos = base64_str.find(",");
            if (comma_pos != std::string::npos) {
                base64_str = base64_str.substr(comma_pos + 1);
                LOG_VERBOSE("handle_predict: stripped data: URI prefix", {});
            }
            
            // Decodificar base64 a bytes
            auto decoded = base64_decode(base64_str);
            
            // ✅ CORREGIDO: raw_buffer es std::vector<uint8_t>, usar directamente
            files.push_back(std::move(decoded));
            
            LOG_INF("handle_predict: extracted image (%zu bytes)", files.back().size());
        }
    }
    
    // 5. ✅ Tokenización con soporte multimodal
    std::vector<server_tokens> inputs;
    if (has_mctx && !files.empty()) {
        // ✅ MULTIMODAL: Usar process_mtmd_prompt para procesar imágenes
        inputs.push_back(process_mtmd_prompt(ctx.mctx, prompt, std::move(files)));
        LOG_INF("handle_predict: using process_mtmd_prompt with %zu images", files.size());
    } else {
        // ✅ TEXTO PURO: Usar tokenize_input_prompts normal
        nlohmann::json prompt_obj = nlohmann::json::array();
        prompt_obj.push_back(prompt);
        inputs = tokenize_input_prompts(
            llama_model_get_vocab(ctx.model),
            ctx.mctx,
            prompt_obj,
            true,
            true
        );
        LOG_VERBOSE("handle_predict: using tokenize_input_prompts (text-only)", {});
    }
    
    if (inputs.empty() || inputs[0].empty()) {
        LOG_WRN("handle_predict: tokenized empty (check mmproj and image markers if applicable)");
        reply->set_message("");
        return;
    }
    
    // 6. Crear tarea
    server_task task(SERVER_TASK_TYPE_COMPLETION);
    task.id = ctx.queue_tasks.get_new_id();
    task.tokens = std::move(inputs[0]);  // ← ✅ Usar std::move()
    task.params.stream = false;
    task.params.cache_prompt = true;
    task.params.n_predict = data.value("n_predict", -1);
    task.data = data;
    
    ctx.queue_results.add_waiting_task_id(task.id);
    ctx.queue_tasks.post(std::move(task));
    
    // 7. ✅ REASONING: Guardar el formato usado para el parser
    std::string fmt = data.value("reasoning_format", "auto");
    common_reasoning_format used_reasoning_format = COMMON_REASONING_FORMAT_AUTO;
    if (fmt == "deepseek") {
        used_reasoning_format = COMMON_REASONING_FORMAT_DEEPSEEK;
    } else if (fmt == "deepseek_legacy") {
        used_reasoning_format = COMMON_REASONING_FORMAT_DEEPSEEK_LEGACY;
    } else if (fmt == "none") {
        used_reasoning_format = COMMON_REASONING_FORMAT_NONE;
    }
    
    // 8. Recibir respuesta completa
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
            nlohmann::json result_json = partial->to_json_non_oaicompat_partial();
            full_content += result_json.value("content", "");
        }
    }
    
    ctx.queue_results.remove_waiting_task_id(task.id);
    
    // 9. Setear mensaje textual (siempre)
    reply->set_message(full_content);
    
    // 10. ✅ OPTIMIZACIÓN: Parseo nativo de tool calls Y reasoning para respuesta blocking
    // Esto llena reply->chat_deltas[] con estructura OpenAI-compatible
    parse_tool_calls_from_content(full_content, used_reasoning_format, reply);
}
