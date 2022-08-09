extends LuauTest


func assert_eq(got: Variant, expected: Variant):
	if expected is float:
		assert(is_equal_approx(got, expected))
	else:
		assert(got == expected)

	print("Got: %s, Expected: %s" % [got, expected])


func _ready():
	const TEST_BOOL := true
	push_boolean(TEST_BOOL)
	assert_eq(get_boolean(-1), TEST_BOOL)

	const TEST_INT := 12
	push_integer(TEST_INT)
	assert_eq(get_integer(-1), TEST_INT)

	const TEST_NUM := 3.14
	push_number(TEST_NUM)
	assert_eq(get_number(-1), TEST_NUM)

	const TEST_STR := "hello there! おはようございます"
	push_string(TEST_STR)
	assert_eq(get_string(-1), TEST_STR)

	set_top(0)
