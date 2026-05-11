// commoncod/proto_mapping.cpp
// Implementación de mapeo proto → JSON / gpt_params

#include "commoncod/proto_mapping.h"
#include "common/log.h"
#include <regex>
#include <cstdlib>
#include <iostream>

nlohmann::json parse_options(bool streaming, const backend::PredictOptions* predict, server_context &llama) {
    nlohmann::json data;
    data["stream"] = streaming;

    // ✅ CORRECT MAPPING
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

    // ✅ CORREGIDO: No setear prompt cuando UseTokenizerTemplate=true y hay messages
    // (igual que grpc-server.cpp oficial líneas 221-225)
    if (!predict->usetokenizertemplate() || predict->messages_size() == 0) {
        data["prompt"] = predict->prompt();
    }
    // Si UseTokenizerTemplate=true y hay messages, NO setear prompt aquí
    // Se aplicará el chat template en predict.cpp

    // ✅ Support for logprobs, top_logprobs
    if (predict->logprobs() > 0) {
        data["logprobs"] = predict->logprobs();
        data["n_probs"] = predict->logprobs();
    }
    if (predict->toplogprobs() > 0) {
        data["top_logprobs"] = predict->toplogprobs();
    }

    // ✅ Support for logit_bias
    if (!predict->logitbias().empty()) {
        try {
            nlohmann::json logit_bias_json = nlohmann::json::parse(predict->logitbias());
            data["logit_bias"] = logit_bias_json;
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "Failed to parse logit_bias: " << e.what() << std::endl;
        }
    }

    // ✅ Support for tools and tool_choice
    if (!predict->tools().empty()) {
        try {
            nlohmann::json tools_json = nlohmann::json::parse(predict->tools());
            data["tools"] = tools_json;
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "Failed to parse tools: " << e.what() << std::endl;
        }
    }
    if (!predict->toolchoice().empty()) {
        try {
            nlohmann::json tool_choice_json = nlohmann::json::parse(predict->toolchoice());
            data["tool_choice"] = tool_choice_json;
        } catch (const nlohmann::json::parse_error& e) {
            data["tool_choice"] = predict->toolchoice();
        }
    }

    // ✅ CRÍTICO: Guardar messages en data para que predict.cpp pueda aplicar chat template
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

    // ✅ Support for images, audios, videos
    for (int i = 0; i < predict->images_size(); i++) {
        data["image_data"].push_back(nlohmann::json{
            {"id", i},
            {"data", predict->images(i)},
        });
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

    // ✅ Support for stop prompts
    {
        nlohmann::json stop_array = nlohmann::json::array();
        for (int i = 0; i < predict->stopprompts_size(); ++i) {
            stop_array.push_back(predict->stopprompts(i));
        }
        data["stop"] = stop_array;
    }

    // ✅ Support for correlation_id
    data["correlation_id"] = predict->correlationid();

    return data;
}

void params_parse(const backend::ModelOptions* request, gpt_params & params, server_context &llama) {
    // === 1. CAMPOS SOLO DE REQUEST (PROTO) ===
    params.model = request->modelfile();
    if (!request->mmproj().empty()) {
        params.mmproj.path = request->mmproj();
    }
    params.model_alias = request->modelfile();

    // ✅ CORREGIDO: params.numa es un enum ggml_numa_strategy
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

    // === 2. DEFAULTS ===
    params.ctx_shift = false;
    params.cache_ram_mib = -1;
    params.n_parallel = 1;
    params.graph_reuse = true;
    params.slot_prompt_similarity = 0.1f;
    params.cont_batching = true;
    params.check_tensors = false;
    params.warmup = true;
    params.ctx_checkpoints_n = 8;

    // === 3. LECTURA DE OPTIONS ===
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

        if (optname == "cache_ram") {
            try { params.cache_ram_mib = std::stoi(optval_str); } catch (...) {}
        }
        else if (optname == "parallel" || optname == "n_parallel") {
            try {
                int val = std::stoi(optval_str);
                params.n_parallel = val;
                if (params.n_parallel > 1) params.cont_batching = true;
            } catch (...) {}
        }
        else if (optname == "grpc_servers" || optname == "rpc_servers") {
            params.rpc_servers = optval_str;
        }
        else if (optname == "context_shift") {
            params.ctx_shift = is_true();
        }
        else if (optname == "use_jinja" || optname == "jinja") {
            params.use_jinja = is_true();
        }
        else if (optname == "slot_prompt_similarity" || optname == "sps") {
            try { params.slot_prompt_similarity = std::stof(optval_str); } catch (...) {}
        }
        else if (optname == "cont_batching") {
            params.cont_batching = is_true();
        }
        else if (optname == "check_tensors") {
            params.check_tensors = is_true();
        }
        else if (optname == "warmup") {
            params.warmup = is_true();
        }
        else if (optname == "n_threads_batch") {
            try { params.n_threads_batch = std::stoi(optval_str); } catch (...) {}
        }
        else if (optname == "ctx_checkpoints") {
            try { params.ctx_checkpoints_n = std::stoi(optval_str); } catch (...) {}
        }
    }

    // === 4. KV OVERRIDES ===
    if (request->overrides_size() > 0) {
        for (int i = 0; i < request->overrides_size(); i++) {
            string_parse_kv_override(request->overrides(i).c_str(), params.kv_overrides);
        }
        params.kv_overrides.emplace_back();
        params.kv_overrides.back().key[0] = 0;
    }

    // === 5. OTROS CAMPOS ===
    if (!request->tensorsplit().empty()) {
        std::string arg_next = request->tensorsplit();
        const std::regex regex{ R"([,/]+)" };
        std::sregex_token_iterator it{ arg_next.begin(), arg_next.end(), regex, -1 };
        std::vector<std::string> split_arg{ it, {} };
        for (size_t i_device = 0; i_device < llama_max_devices(); ++i_device) {
            if (i_device < split_arg.size()) {
                try { params.tensor_split[i_device] = std::stof(split_arg[i_device]); }
                catch (...) { params.tensor_split[i_device] = 0.0f; }
            } else {
                params.tensor_split[i_device] = 0.0f;
            }
        }
    }

    if (!request->maingpu().empty()) {
        try { params.main_gpu = std::stoi(request->maingpu()); } catch (...) {}
    }

    if (!request->loraadapter().empty() && !request->lorabase().empty()) {
        float scale_factor = 1.0f;
        if (request->lorascale() != 0.0f) scale_factor = request->lorascale();
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

    if (request->ropescaling() == "none") params.rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_NONE;
    else if (request->ropescaling() == "yarn") params.rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_YARN;
    else params.rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_LINEAR;

    if (request->yarnextfactor() != 0.0f) params.yarn_ext_factor = request->yarnextfactor();
    if (request->yarnattnfactor() != 0.0f) params.yarn_attn_factor = request->yarnattnfactor();
    if (request->yarnbetafast() != 0.0f) params.yarn_beta_fast = request->yarnbetafast();
    if (request->yarnbetaslow() != 0.0f) params.yarn_beta_slow = request->yarnbetaslow();
    if (request->ropefreqbase() != 0.0f) params.rope_freq_base = request->ropefreqbase();
    if (request->ropefreqscale() != 0.0f) params.rope_freq_scale = request->ropefreqscale();
}
