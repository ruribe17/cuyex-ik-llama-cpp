// commoncod/proto_mapping.cpp
// Implementación de mapeo proto → JSON / gpt_params
//
// CARACTERÍSTICAS:
// 1. Mapeo de PredictOptions a JSON para inferencia.
// 2. Mapeo de ModelOptions a gpt_params para configuración del modelo.
// 3. Soporte para tools, tool_choice, reasoning, multimodal.
// 4. Soporte para parámetros de cache RAM.

#include "commoncod/proto_mapping.h"
#include "common/log.h"
#include <regex>
#include <cstdlib>
#include <iostream>

/**
 * @brief Convierte PredictOptions del proto a JSON para la tarea de inferencia.
 * 
 * Esta función mapea todos los parámetros de generación del protobuf a un objeto JSON
 * que será procesado por el servidor para crear una tarea de inferencia.
 * 
 * @param streaming Indicador de modo streaming (true = streaming, false = batch).
 * @param predict Puntero a las opciones de predicción del proto (backend::PredictOptions).
 * @param llama Referencia al contexto del servidor (server_context).
 * @return nlohmann::json Objeto JSON con los parámetros configurados.
 */
nlohmann::json parse_options(bool streaming, const backend::PredictOptions* predict, server_context &llama) {
    nlohmann::json data;
    data["stream"] = streaming;

    // ========================================================================
    // 1. PARÁMETROS DE GENERACIÓN
    // ========================================================================
    
    data["cache_prompt"] = predict->promptcacheall(); 
    data["n_predict"] = predict->tokens() == 0 ? -1 : predict->tokens();
    data["top_k"] = predict->topk();
    data["top_p"] = predict->topp();
    data["typical_p"] = predict->typicalp();
    data["temperature"] = predict->temperature();
    data["repeat_last_n"] = predict->repeat();
    data["repeat_penalty"] = predict->penalty();
    data["frequency_penalty"] = predict->frequencypenalty();
    data["presence_penalty"] = predict->presencepenalty();
    data["mirostat"] = predict->mirostat();
    data["mirostat_tau"] = predict->mirostattau();
    data["mirostat_eta"] = predict->mirostateta();
    data["n_keep"] = predict->nkeep(); 
    data["seed"] = predict->seed();
    data["grammar"] = predict->grammar();
    data["ignore_eos"] = predict->ignoreeos();
    data["embeddings"] = predict->embeddings();

    // Prompt handling (respetar tokenizer template)
    if (!predict->usetokenizertemplate() || predict->messages_size() == 0) {
        data["prompt"] = predict->prompt();
    }

    // ========================================================================
    // 2. LOGPROBS
    // ========================================================================
    
    if (predict->logprobs() > 0) {
        data["logprobs"] = predict->logprobs();
        data["n_probs"] = predict->logprobs();
    }
    if (predict->toplogprobs() > 0) {
        data["top_logprobs"] = predict->toplogprobs();
    }

    // ========================================================================
    // 3. LOGIT BIAS
    // ========================================================================
    
    if (!predict->logitbias().empty()) {
        try {
            nlohmann::json logit_bias_json = nlohmann::json::parse(predict->logitbias());
            data["logit_bias"] = logit_bias_json;
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "Failed to parse logit_bias: " << e.what() << std::endl;
        }
    }

    // ========================================================================
    // 4. FUNCTION CALLING: TOOLS Y TOOL_CHOICE
    // ========================================================================
    
    if (!predict->tools().empty()) {
        try {
            nlohmann::json tools_json = nlohmann::json::parse(predict->tools());
            data["tools"] = tools_json;
            LOG_INF("parse_options: %zu tools loaded", tools_json.size());
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "Failed to parse tools: " << e.what() << std::endl;
        }
    }

    if (!predict->toolchoice().empty()) {
        data["tool_choice"] = predict->toolchoice();
        LOG_INF("parse_options: tool_choice set to '%s'", predict->toolchoice().c_str());
    }

    // ========================================================================
    // 5. REASONING/THINKING (VÍA METADATA)
    // ========================================================================
    
    auto it = predict->metadata().find("enable_thinking");
    if (it != predict->metadata().end()) {
        std::string val = it->second;
        data["enable_thinking"] = (val == "true" || val == "1" || val == "yes");
        LOG_INF("parse_options: enable_thinking set to %s", val.c_str());
    }

    auto it_fmt = predict->metadata().find("reasoning_format");
    if (it_fmt != predict->metadata().end()) {
        data["reasoning_format"] = it_fmt->second;
        LOG_INF("parse_options: reasoning_format set to %s", it_fmt->second.c_str());
    }

    // ========================================================================
    // 6. MESSAGES (CHAT)
    // ========================================================================
    
    if (predict->messages_size() > 0) {
        nlohmann::json messages_json = nlohmann::json::array();
        for (int i = 0; i < predict->messages_size(); i++) {
            const auto& msg = predict->messages(i);
            nlohmann::json msg_json;
            msg_json["role"] = msg.role();
            if (!msg.content().empty()) {
                msg_json["content"] = msg.content();
            }
            if (!msg.name().empty()) {
                msg_json["name"] = msg.name();
            }
            if (!msg.tool_call_id().empty()) {
                msg_json["tool_call_id"] = msg.tool_call_id();
            }
            if (!msg.reasoning_content().empty()) {
                msg_json["reasoning_content"] = msg.reasoning_content();
            }
            if (!msg.tool_calls().empty()) {
                try {
                    nlohmann::json tool_calls = nlohmann::json::parse(msg.tool_calls());
                    msg_json["tool_calls"] = tool_calls;
                } catch (const nlohmann::json::parse_error& e) {
                    // ignore
                }
            }
            messages_json.push_back(msg_json);
        }
        data["messages"] = messages_json;
    }

    // ========================================================================
    // 7. MULTIMODAL: IMAGES, AUDIOS, VIDEOS
    // ========================================================================
    
    // ✅ CORREGIDO: Array de strings base64 puros para mtmd
    // Formato esperado por tokenize_input_prompts: ["base64_1", "base64_2", ...]
    for (int i = 0; i < predict->images_size(); i++) {
        data["image_data"].push_back(predict->images(i));
    }
    for (int i = 0; i < predict->audios_size(); i++) {
        data["audio_data"].push_back(nlohmann::json{
            {"id", i},
            {"data", predict->audios(i)},
        });
    }
    for (int i = 0; i < predict->videos_size(); i++) {
        data["video_data"].push_back(nlohmann::json{
            {"id", i},
            {"data", predict->videos(i)},
        });
    }

    // ========================================================================
    // 8. STOP PROMPTS
    // ========================================================================
    
    {
        nlohmann::json stop_array = nlohmann::json::array();
        for (int i = 0; i < predict->stopprompts_size(); ++i) {
            stop_array.push_back(predict->stopprompts(i));
        }
        data["stop"] = stop_array;
    }

    // ========================================================================
    // 9. METADATOS ADICIONALES
    // ========================================================================
    
    data["correlation_id"] = predict->correlationid();

    return data;
}

/**
 * @brief Convierte ModelOptions del proto a gpt_params para configuración del modelo.
 * 
 * Esta función mapea todos los parámetros de configuración del modelo del protobuf
 * a la estructura gpt_params de llama.cpp, que se usa para inicializar el modelo
 * y el contexto de inferencia.
 * 
 * @param request Puntero a las opciones del modelo del proto (backend::ModelOptions).
 * @param params Referencia a la estructura gpt_params a rellenar.
 * @param llama Referencia al contexto del servidor (server_context).
 */
void params_parse(const backend::ModelOptions* request, gpt_params & params, server_context &llama) {
    // ========================================================================
    // 1. CAMPOS SOLO DE REQUEST (PROTO)
    // ========================================================================
    
    params.model = request->modelfile();
    if (!request->mmproj().empty()) {
        params.mmproj.path = request->mmproj();
    }
    params.model_alias = request->modelfile();

    // NUMA
    if (request->numa()) {
        params.numa = GGML_NUMA_STRATEGY_DISTRIBUTE; 
    } else {
        params.numa = GGML_NUMA_STRATEGY_DISABLED;
    }

    if (!request->cachetypekey().empty()) {
        params.cache_type_k = request->cachetypekey();
    }
    if (!request->cachetypevalue().empty()) {
        params.cache_type_v = request->cachetypevalue();
    }

    params.n_ctx = request->contextsize();
    params.n_threads = request->threads();
    params.n_gpu_layers = request->ngpulayers();
    params.n_batch = request->nbatch();
    params.seed = request->seed(); 
    params.n_ubatch = -1;

    // ========================================================================
    // 2. DEFAULTS (VALORES POR DEFECTO)
    // ========================================================================
    // ✅ ORDEN CRÍTICO: Primero establecer defaults, luego overridear con options
    
    params.ctx_shift = false;
    params.cache_ram_mib = 8192;           // Default: 8GB
    params.cache_ram_n_min = 0;            // Default: 0 (todos los tokens)
    params.cache_ram_similarity = 0.5f;    // Default: 50% similitud
    params.n_parallel = 1;
    params.graph_reuse = true;
    params.slot_prompt_similarity = 0.1f;  // Default: 10% similitud
    params.cont_batching = true;
    params.check_tensors = false;
    params.warmup = true;
    params.ctx_checkpoints_n = 8;

    // ========================================================================
    // 3. LECTURA DE OPTIONS (EN ORDEN DE APLICACIÓN)
    // ========================================================================
    
    for (int i = 0; i < request->options_size(); i++) {
        std::string opt = request->options(i);
        if (opt.empty()) continue;

        size_t colon_pos = opt.find(':');
        std::string optname = opt.substr(0, colon_pos);
        std::string optval_str = (colon_pos == std::string::npos) ? "true" : opt.substr(colon_pos + 1);

        auto is_true = [&optval_str]() {
            return optval_str == "true" || optval_str == "1" || optval_str == "yes" ||
                   optval_str == "on" || optval_str == "enabled";
        };
        auto is_false = [&optval_str]() {
            return optval_str == "false" || optval_str == "0" || optval_str == "no" ||
                   optval_str == "off" || optval_str == "disabled";
        };

        // ====================================================================
        // 3.1 PARÁMETROS DE BATCH Y THROUGHPUT
        // ====================================================================
        
        if (optname == "n_ubatch") {
            try {
                int val = std::stoi(optval_str);
                if (val >= -1) params.n_ubatch = val;
            } catch (...) {}
            continue;
        }
        if (optname == "attn_max_batch") {
            try {
                int val = std::stoi(optval_str);
                if (val >= 0) params.attn_max_batch = val;
            } catch (...) {}
            continue;
        }

        // ====================================================================
        // 3.2 PARÁMETROS DE EXPERTOS (MOE)
        // ====================================================================
        
        if (optname == "grouped_expert_routing") {
            params.grouped_expert_routing = is_true();
            continue;
        }
        if (optname == "fused_moe_up_gate") {
            params.fused_moe_up_gate = is_true();
            continue;
        }
        if (optname == "fused_up_gate") {
            params.fused_up_gate = is_true();
            continue;
        }

        // ====================================================================
        // 3.3 PARÁMETROS DE EMBEDDINGS / RERANKING
        // ====================================================================
        
        params.embedding = request->embeddings();
        if (params.embedding) {
            if (optname == "attention") {
                if (optval_str == "causal") params.attention_type = LLAMA_ATTENTION_TYPE_CAUSAL;
                else if (optval_str == "non-causal") params.attention_type = LLAMA_ATTENTION_TYPE_NON_CAUSAL;
                continue;
            }
            if (optname == "pooling") {
                if (optval_str == "mean") params.pooling_type = LLAMA_POOLING_TYPE_MEAN;
                else if (optval_str == "cls") params.pooling_type = LLAMA_POOLING_TYPE_CLS;
                else if (optval_str == "last") params.pooling_type = LLAMA_POOLING_TYPE_LAST;
                continue;
            }
            if (optname == "embd_normalize") {
                try {
                    int val = std::stoi(optval_str);
                    if (val >= -1) params.embd_normalize = val;
                } catch (...) {}
                continue;
            }
        }

        // ====================================================================
        // 3.4 PARÁMETROS DE CACHE (ORDEN CRÍTICO)
        // ====================================================================
        
        if (optname == "cache_ram") {
            try {
                int val = std::stoi(optval_str);
                params.cache_ram_mib = val;
                LOG_INF("params_parse: cache_ram_mib set to %d MiB", val);
            } catch (...) {}
        }
        else if (optname == "cache_ram_similarity") {
            try {
                float val = std::stof(optval_str);
                params.cache_ram_similarity = val;
                LOG_INF("params_parse: cache_ram_similarity set to %.2f", val);
            } catch (...) {}
        }
        else if (optname == "cache_ram_n_min") {
            try {
                int val = std::stoi(optval_str);
                params.cache_ram_n_min = val;
                LOG_INF("params_parse: cache_ram_n_min set to %d tokens", val);
            } catch (...) {}
        }
        else if (optname == "slot_prompt_similarity" || optname == "sps") {
            try {
                float val = std::stof(optval_str);
                params.slot_prompt_similarity = val;
                LOG_INF("params_parse: slot_prompt_similarity set to %.2f", val);
            } catch (...) {}
        }
        else if (optname == "parallel" || optname == "n_parallel") {
            try {
                int val = std::stoi(optval_str);
                params.n_parallel = val;
                if (params.n_parallel > 1) params.cont_batching = true;
            } catch (...) {}
        }
        else if (optname == "context_shift") {
            params.ctx_shift = is_true();
        }
        else if (optname == "cont_batching" || optname == "continuous_batching") {
            params.cont_batching = is_true();
        }

        // ====================================================================
        // 3.5 PARÁMETROS DE THREADING
        // ====================================================================
        
        else if (optname == "n_threads_batch") {
            try {
                int val = std::stoi(optval_str);
                params.n_threads_batch = val;
                LOG_INF("params_parse: n_threads_batch set to %d", val);
            } catch (...) {}
        }

        // ====================================================================
        // 3.6 PARÁMETROS DE RED Y SERVIDORES
        // ====================================================================
        
        else if (optname == "grpc_servers" || optname == "rpc_servers") {
            params.rpc_servers = optval_str;
        }

        // ====================================================================
        // 3.7 PARÁMETROS DE JINJA Y TEMPLATES
        // ====================================================================
        
        else if (optname == "use_jinja" || optname == "jinja") {
            params.use_jinja = is_true();
        }

        // ====================================================================
        // 3.8 PARÁMETROS DE VALIDACIÓN Y DEBUG
        // ====================================================================
        
        else if (optname == "check_tensors") {
            params.check_tensors = is_true();
        }
        else if (optname == "warmup") {
            params.warmup = is_true();
        }

        // ====================================================================
        // 3.9 PARÁMETROS DE CHECKPOINTS
        // ====================================================================
        
        else if (optname == "ctx_checkpoints") {
            try {
                int val = std::stoi(optval_str);
                params.ctx_checkpoints_n = val;
                LOG_INF("params_parse: ctx_checkpoints_n set to %d", val);
            } catch (...) {}
        }
    }

    // ========================================================================
    // 4. FALLBACKS A VARIABLES DE ENTORNO
    // ========================================================================
    
    if (params.n_parallel == 1) {
        const char *env_parallel = std::getenv("LLAMACPP_PARALLEL");
        if (env_parallel != nullptr) {
            try {
                params.n_parallel = std::stoi(env_parallel);
                if (params.n_parallel > 1) {
                    params.cont_batching = true;
                }
            } catch (...) {}
        }
    }

    // ========================================================================
    // 5. KV OVERRIDES
    // ========================================================================
    
    if (request->overrides_size() > 0) {
        for (int i = 0; i < request->overrides_size(); i++) {
            string_parse_kv_override(request->overrides(i).c_str(), params.kv_overrides);
        }
        params.kv_overrides.emplace_back();
        params.kv_overrides.back().key[0] = 0;
    }

    // ========================================================================
    // 6. OTROS CAMPOS SOLO DE REQUEST
    // ========================================================================
    
    if (!request->tensorsplit().empty()) {
        std::string arg_next = request->tensorsplit();
        const std::regex regex{ R"([,/]+)" };
        std::sregex_token_iterator it{ arg_next.begin(), arg_next.end(), regex, -1 };
        std::vector<std::string> split_arg{ it, {} };

        for (size_t i_device = 0; i_device < llama_max_devices(); ++i_device) {
            if (i_device < split_arg.size()) {
                try {
                    params.tensor_split[i_device] = std::stof(split_arg[i_device]);
                } catch (...) {
                    params.tensor_split[i_device] = 0.0f;
                }
            } else {
                params.tensor_split[i_device] = 0.0f;
            }
        }
    }

    if (!request->maingpu().empty()) {
        try {
            params.main_gpu = std::stoi(request->maingpu());
        } catch (...) {}
    }

    if (!request->loraadapter().empty() && !request->lorabase().empty()) {
        float scale_factor = 1.0f;
        if (request->lorascale() != 0.0f) {
            scale_factor = request->lorascale();
        }
        std::string model_dir = params.model.substr(0, params.model.find_last_of("/\\"));
        llama_lora_adapter_info lora_info;
        lora_info.path = model_dir + "/" + request->loraadapter();
        lora_info.scale = scale_factor;
        params.lora_adapters.push_back(std::move(lora_info));
    }

    params.use_mlock = request->mlock();
    params.use_mmap = request->mmap();

    if (request->flashattention() == "on" || request->flashattention() == "enabled") {
        params.flash_attn = true;
    } else if (request->flashattention() == "off" || request->flashattention() == "disabled") {
        params.flash_attn = false;
    } else if (request->flashattention() == "auto") {
        params.flash_attn = true;
    }

    params.no_kv_offload = request->nokvoffload();

    params.embedding = request->embeddings() || request->reranking();
    if (request->reranking()) {
        params.pooling_type = LLAMA_POOLING_TYPE_CLS;
    }

    if (request->ropescaling() == "none") {
        params.rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_NONE;
    } else if (request->ropescaling() == "yarn") {
        params.rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_YARN;
    } else if (request->ropescaling() == "linear") {
        params.rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_LINEAR;
    }

    if (request->yarnextfactor() != 0.0f) params.yarn_ext_factor = request->yarnextfactor();
    if (request->yarnattnfactor() != 0.0f) params.yarn_attn_factor = request->yarnattnfactor();
    if (request->yarnbetafast() != 0.0f) params.yarn_beta_fast = request->yarnbetafast();
    if (request->yarnbetaslow() != 0.0f) params.yarn_beta_slow = request->yarnbetaslow();

    if (request->ropefreqbase() != 0.0f) {
        params.rope_freq_base = request->ropefreqbase();
    }
    if (request->ropefreqscale() != 0.0f) {
        params.rope_freq_scale = request->ropefreqscale();
    }
}
