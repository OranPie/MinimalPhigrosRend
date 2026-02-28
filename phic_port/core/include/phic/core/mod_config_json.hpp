#pragma once

#include "phic/core/types.hpp"

#include <nlohmann/json.hpp>

namespace phic {

// Merges JSON object values into an existing mod config.
// Supports both flat keys and Python-style nested mod objects.
void merge_mod_config_from_json(ModConfig& mods, const nlohmann::json& root);

}  // namespace phic
