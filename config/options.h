// config/options.h
// Opciones por defecto para ik_llama_backend

#pragma once

#include "gpt_params.h"
#include "common_params_sampling.h"

// ===== Opciones por defecto =====
namespace default_options {
    // Context & Threads
    constexpr int32_t DEFAULT_N_CTX = 2048;
    constexpr int32_t DEFAULT_N_THREADS = 4;
    constexpr int32_t DEFAULT_N_THREADS_BATCH = -1;
    constexpr int32_t DEFAULT_N_BATCH = 512;
    constexpr int32_t DEFAULT_N_UBATCH = -1;
    constexpr int32_t DEFAULT_ATTN_MAX_BATCH = -1;
    
    // GPU & Memory
    constexpr int32_t DEFAULT_N_GPU_LAYERS = 0;
    constexpr int32_t DEFAULT_MAIN_GPU = 0;
    constexpr int32_t DEFAULT_MAX_MODEL_LEN = 2048;
    constexpr int32_t DEFAULT_SWAP = 0;
    constexpr float DEFAULT_GPU_MEMORY_UTILIZATION = 0.9f;
    constexpr bool DEFAULT_F16_MEMORY = false;
    constexpr bool DEFAULT_MLOCK = false;
    constexpr bool DEFAULT_MMAP = true;
    constexpr bool DEFAULT_LOW_VRAM = false;
    constexpr bool DEFAULT_VOCAB_ONLY = false;
    constexpr bool DEFAULT_NUMA = false;
    constexpr std::vector<float> DEFAULT_TENSOR_SPLIT = {};
    constexpr std::string DEFAULT_QUANTIZATION = "";
    constexpr std::string DEFAULT_DTYPE = "";
    constexpr std::string DEFAULT_LOAD_FORMAT = "";
    
    // Model & Architecture
    constexpr int32_t DEFAULT_NGQA = 1;
    constexpr float DEFAULT_RMS_NORM_EPS = 1e-5f;
    constexpr float DEFAULT_ROPE_FREQ_BASE = 10000.0f;
    constexpr float DEFAULT_ROPE_FREQ_SCALE = 1.0f;
    constexpr int32_t DEFAULT_ROPE_SCALING = LLAMA_ROPE_SCALING_TYPE_NONE;
    constexpr int32_t DEFAULT_ATTENTION_TYPE = LLAMA_ATTENTION_TYPE_CAUSAL;
    constexpr int32_t DEFAULT_POOLING_TYPE = LLAMA_POOLING_TYPE_UNSPECIFIED;
    constexpr bool DEFAULT_FLASH_ATTN = true;
    constexpr bool DEFAULT_NO_KV_OFFLOAD = false;
    constexpr bool DEFAULT_GROUPED_EXPERT_ROUTING = false;
    constexpr bool DEFAULT_FUSED_MOE_UP_GATE = false;
    constexpr bool DEFAULT_FUSED_UP_GATE = false;
    constexpr bool DEFAULT_NO_MUL_MAT_Q = false;
    
    // Generation & Sampling
    constexpr int32_t DEFAULT_SEED = -1;
    constexpr float DEFAULT_SLOT_PROMPT_SIMILARITY = 0.1f;
    constexpr int32_t DEFAULT_CTX_CHECKPOINTS = 8;
    constexpr bool DEFAULT_USE_JINJA = false;
    constexpr bool DEFAULT_PREFILL_ASSISTANT = false;
    constexpr bool DEFAULT_REASONING_FORMAT = false;
    constexpr float DEFAULT_YARN_EXT_FACTOR = 0.0f;
    constexpr float DEFAULT_YARN_ATTN_FACTOR = 0.0f;
    constexpr float DEFAULT_YARN_BETA_FAST = 32.0f;
    constexpr float DEFAULT_YARN_BETA_SLOW = 1.0f;
    constexpr float DEFAULT_LORA_SCALE = 1.0f;
    constexpr float DEFAULT_CFG_SCALE = 1.0f;
    constexpr int32_t DEFAULT_EMBD_NORMALIZE = 2;
    
    // Multimodal & Media Limits
    constexpr int32_t DEFAULT_LIMIT_IMAGE_PER_PROMPT = 1;
    constexpr int32_t DEFAULT_LIMIT_VIDEO_PER_PROMPT = 1;
    constexpr int32_t DEFAULT_LIMIT_AUDIO_PER_PROMPT = 1;
    constexpr std::string DEFAULT_MMPROJ = "";
    constexpr std::string DEFAULT_CLIP_MODEL = "";
    constexpr std::string DEFAULT_CLIP_SUBFOLDER = "";
    constexpr int32_t DEFAULT_CLIP_SKIP = 1;
    constexpr std::string DEFAULT_CONTROLNET = "";
    constexpr std::string DEFAULT_AUDIO_PATH = "";
    constexpr std::string DEFAULT_DRAFT_MODEL = "";
    
    // LoRA & Control Vectors
    constexpr std::vector<llama_lora_adapter_info> DEFAULT_LORA_ADAPTERS = {};
    constexpr std::vector<float> DEFAULT_LORA_SCALES = {};
    constexpr std::string DEFAULT_LORA_ADAPTER = "";
    constexpr std::string DEFAULT_LORA_BASE = "";
    constexpr std::vector<std::string> DEFAULT_OVERIDES = {};
    constexpr std::vector<std::string> DEFAULT_OPTIONS = {};
    constexpr std::vector<common_grammar_trigger> DEFAULT_GRAMMAR_TRIGGERS = {};
    
    // Chat & Templates
    constexpr std::string DEFAULT_TOKENIZER = "";
    constexpr std::string DEFAULT_PIPELINE = "";
    constexpr std::string DEFAULT_SCHEDULER = "";
    constexpr std::string DEFAULT_MODEL_PATH = "";
    constexpr std::string DEFAULT_MODEL_FILE = "";
    constexpr std::string DEFAULT_MODEL_ALIAS = "";
    constexpr std::string DEFAULT_ENGINE_ARGS = "";
    
    // Flags
    constexpr bool DEFAULT_RERANKING = false;
    constexpr bool DEFAULT_EMBEDDINGS = false;
    constexpr bool DEFAULT_CUDA = false;
    constexpr bool DEFAULT_TRUST_REMOTE_CODE = false;
    constexpr bool DEFAULT_ENFORCE_EAGER = false;
    constexpr bool DEFAULT_DISABLE_LOG_STATUS = false;
    constexpr int32_t DEFAULT_TENSOR_PARALLEL_SIZE = 1;
    constexpr int32_t DEFAULT_CONTEXT_SIZE = 2048;
}
