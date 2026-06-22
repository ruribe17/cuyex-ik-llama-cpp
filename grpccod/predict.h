// grpccod/predict.h
// Declaración de Predict() y PredictStream()
//
// CAMBIOS REALIZADOS:
// 1. Actualizada la firma de parse_tool_calls_from_content para recibir reasoning_format.
// 2. Agregados los headers necesarios para common_reasoning_format.

#pragma once

#include "backend.pb.h"
#include <grpcpp/grpcpp.h>
#include "json.hpp"
#include "llama.h"
#include "common/common.h"  // ← Necesario para common_reasoning_format

// Forward declaration - NO incluir server-context.h aquí
struct server_context;

// ===== Tipos auxiliares =====

/**
 * @brief Tokeniza un prompt JSON usando la configuración del servidor.
 * 
 * Esta función maneja tanto prompts como string como arrays de tokens/strings.
 * 
 * @param vocab Vocabulario del modelo.
 * @param ctx Contexto de llama.
 * @param json_prompt Prompt en formato JSON (string o array).
 * @param add_bos Añadir token BOS al inicio.
 * @param force_special Forzar tokens especiales.
 * @return std::vector<std::vector<llama_token>> Tokens tokenizados.
 */
std::vector<std::vector<llama_token>> tokenize_input_prompts(
    const llama_vocab *vocab,
    llama_context *ctx,
    const nlohmann::json &json_prompt,
    bool add_bos,
    bool force_special);

/**
 * @brief Parsea el contenido generado extrayendo tool calls y reasoning_content.
 * 
 * Usa el parser PEG de llama.cpp para extraer tool calls y reasoning del texto.
 * Solo se usa en modo blocking (handle_predict) para simplificar.
 * 
 * @param content Contenido generado por el modelo.
 * @param reasoning_format Formato de reasoning a usar (DEEPSEEK, AUTO, NONE, etc.).
 * @param reply Puntero al mensaje de respuesta gRPC a rellenar.
 */
void parse_tool_calls_from_content(
    const std::string &content,
    common_reasoning_format reasoning_format,  // <- Nuevo parámetro
    backend::Reply *reply);

// ===== Funciones principales =====

/**
 * @brief Maneja una request de predicción en modo blocking (no streaming).
 * 
 * @param ctx Contexto del servidor.
 * @param request Opciones de predicción del proto.
 * @param reply Respuesta a rellenar con el resultado.
 */
void handle_predict(server_context &ctx, const backend::PredictOptions* request, backend::Reply* reply);

/**
 * @brief Maneja una request de predicción en modo streaming.
 * 
 * @param ctx Contexto del servidor.
 * @param request Opciones de predicción del proto.
 * @param writer Writer de gRPC para enviar respuestas parciales.
 */
void handle_predict_stream(server_context &ctx, const backend::PredictOptions* request, grpc::ServerWriter<backend::Reply>* writer);
