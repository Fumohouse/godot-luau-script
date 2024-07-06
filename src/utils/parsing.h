#pragma once

#include <godot_cpp/variant/string.hpp>

using namespace godot;

bool is_whitespace(char p_c);
void skip_whitespace(const char *&ptr);
String read_until_whitespace(const char *&ptr);
String read_until_end(const char *&ptr);
bool is_comment_prefix(const char *p_ptr);

template <typename T>
bool to_number(const String &p_str, T &r_out);

template <typename T>
bool read_number(const char *&ptr, T &r_out) {
	String str = read_until_whitespace(ptr);
	return to_number<T>(str, r_out);
}
