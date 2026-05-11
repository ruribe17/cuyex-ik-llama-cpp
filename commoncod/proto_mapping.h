// commoncod/proto_mapping.h
// Mapeo de proto → JSON / gpt_params
// Versión mejorada para ik_llama.cpp (server_context)
// Sin conflictos de namespace json

#pragma once

// =============================================================================
// 1. HEADERS DE PROTOBUF (Generados desde backend.proto)
// =============================================================================
#include "backend.pb.h"

// Forward declarations
struct gpt_params;
struct server_context;

// =============================================================================
// 2. HEADERS DE LLAMA.CPP
// =============================================================================

// common.h: Define gpt_params, common_params_sampling, etc.
// Ruta relativa desde CMAKE_SOURCE_DIR (configurado en CMakeLists.txt)
#include "llama.cpp/common/common.h"

// =============================================================================
// 3. WRAPPER DEL SERVIDOR (Nuestro wrapper seguro)
// =============================================================================

// server/context.h: Define server_context, server_slot, server_queue
// Este wrapper ya incluye server-context.h y protege contra inclusiones múltiples

// =============================================================================
// 4. JSON (Nlohmann)
// =============================================================================

// Incluimos el header completo para usar nlohmann::json
// NOTA: No usamos 'using json = ...' aquí para evitar conflicto con:
// - llama.cpp/examples/server/server-task.h que define:
//   using json = nlohmann::ordered_json;
// En su lugar, usamos 'nlohmann::json' explícitamente en las firmas.
#include "nlohmann/json.hpp"

// =============================================================================
// 5. DECLARACIONES DE FUNCIONES
// =============================================================================

/**
 * @brief Mapea PredictOptions del proto a un objeto JSON para la tarea de inferencia.
 * 
 * Esta función convierte los parámetros de predicción del protobuf a un objeto JSON
 * que será procesado por el servidor para crear una tarea de inferencia.
 * 
 * @param streaming Indicador de modo streaming (true = streaming, false = batch).
 * @param predict Puntero a las opciones de predicción del proto (backend::PredictOptions).
 * @param llama Referencia al contexto del servidor (server_context).
 *              Nota: Se pasa por referencia pero no se modifica directamente.
 *                    La configuración específica por-request se guarda en el JSON retornado.
 * 
 * @return nlohmann::json Objeto JSON con los parámetros configurados para la tarea.
 *         Incluye: stream, n_predict, top_k, top_p, temperature, grammar, messages, etc.
 */
nlohmann::json parse_options(bool streaming, const backend::PredictOptions* predict, server_context &llama);

/**
 * @brief Mapea ModelOptions del proto a gpt_params para la configuración del modelo.
 * 
 * Esta función convierte los parámetros de configuración del modelo del protobuf
 * a la estructura gpt_params de llama.cpp, que se usa para inicializar el modelo
 * y el contexto de inferencia.
 * 
 * @param request Puntero a las opciones del modelo del proto (backend::ModelOptions).
 * @param params Referencia a la estructura gpt_params a rellenar.
 *               Se modifica directamente con los valores del proto.
 * @param llama Referencia al contexto del servidor (server_context).
 *              Nota: Actualmente no se usa directamente, pero se mantiene por compatibilidad.
 * 
 * @return void
 */
void params_parse(const backend::ModelOptions* request, gpt_params & params, server_context &llama);

// =============================================================================
// 6. NOTAS IMPORTANTES
// =============================================================================
// - NO definir 'using json = ...' en este header para evitar conflictos.
// - Usar 'nlohmann::json' explícitamente en las firmas de funciones.
// - En los archivos .cpp, se puede definir 'using json = nlohmann::json;' localmente.
// - server_context está definido en server/context.h → llama.cpp/examples/server/server-context.h
// - gpt_params está definido en llama.cpp/common/common.h

// Al final de proto_mapping.h, agregar si es necesario:
#ifdef PROTO_MAPPING_IMPLEMENTATION
#include "examples/server/server-context.h"
#endif

