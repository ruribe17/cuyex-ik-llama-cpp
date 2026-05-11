// server/update_slots.h — versión CORREGIDA
// Declaración de funciones para procesar tokens, slots y respuestas

#pragma once

// Headers originales de llama.cpp
#include "llama.h"
#include "common.h"
#include "json.hpp"
#include "examples/server/server-context.h"  // ← Incluye server_slot, server_queue, etc.
#include "sampling.h"
#include "common/chat.h"

// Forward declarations si son necesarias
struct completion_token_output;

// ===== Funciones principales =====

// Procesa un token generado y decide si continuar o detenerse
bool process_token(completion_token_output& result, server_slot& slot);

// Procesa imágenes multimodales (si hay)
bool process_images(server_slot& slot);

// Actualiza los slots (lógica personalizada)
void update_slots(server_context& ctx);
