#include "utils/parsing.h"

#include <godot_cpp/variant/string.hpp>

using namespace godot;

bool is_whitespace(char p_c) {
	return p_c == ' ' || p_c == '\t' || p_c == '\v' || p_c == '\f';
}

void skip_whitespace(const char *&ptr) {
	while (is_whitespace(*ptr)) {
		ptr++;
	}
}

String read_until_whitespace(const char *&ptr) {
	String out;

	while (*ptr && !is_whitespace(*ptr)) {
		out += *ptr;
		ptr++;
	}

	skip_whitespace(ptr);
	return out;
}

String read_until_end(const char *&ptr) {
	String out;

	while (*ptr) {
		out += *ptr;
		ptr++;
	}

	return out;
}

bool is_comment_prefix(const char *p_ptr) {
	return *p_ptr == '-' && *(p_ptr + 1) == '-';
}

template <>
bool to_number<int>(const String &p_str, int &r_out) {
	if (!p_str.is_valid_int())
		return false;

	r_out = p_str.to_int();
	return true;
}

template <>
bool to_number<float>(const String &p_str, float &r_out) {
	if (!p_str.is_valid_float())
		return false;

	r_out = p_str.to_float();
	return true;
}
