#pragma once

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

namespace nb {

// This is a pretty big hack.
// Avoids the creation of instance bindings by using a protected constructor and not using memnew.
template <typename T>
class WrappedNoBinding : public T {
	static WrappedNoBinding<T> singleton;

public:
	static WrappedNoBinding<T> *get_singleton_nb() {
		if (!singleton._owner) {
			StringName singleton_name = T::get_class_static();
			singleton._owner = internal::gdextension_interface_global_get_singleton(&singleton_name);
		}

		return &singleton;
	}

	WrappedNoBinding(GodotObject *p_obj) :
			T(p_obj) {}
};

template <typename T>
WrappedNoBinding<T> WrappedNoBinding<T>::singleton = nullptr;

typedef WrappedNoBinding<godot::Object> Object;
typedef WrappedNoBinding<godot::RefCounted> RefCounted;
typedef WrappedNoBinding<godot::Engine> Engine;
typedef WrappedNoBinding<godot::ResourceLoader> ResourceLoader;
typedef WrappedNoBinding<godot::ResourceSaver> ResourceSaver;
typedef WrappedNoBinding<godot::OS> OS;
typedef WrappedNoBinding<godot::Time> Time;
typedef WrappedNoBinding<godot::EditorInterface> EditorInterface;
typedef WrappedNoBinding<godot::ClassDBSingleton> ClassDB;

} // namespace nb
