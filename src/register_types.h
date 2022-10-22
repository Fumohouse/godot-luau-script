#pragma once

#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_luau_script_module(ModuleInitializationLevel p_level);
void uninitialize_luau_script_module(ModuleInitializationLevel p_level);
