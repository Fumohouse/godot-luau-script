#pragma once

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void initialize_luau_script_module(ModuleInitializationLevel p_level);
void uninitialize_luau_script_module(ModuleInitializationLevel p_level);
