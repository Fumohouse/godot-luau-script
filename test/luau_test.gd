extends LuauTest


func assert_eq(got: Variant, expected: Variant):
	print("Got: %s, Expected: %s" % [got, expected])

	if expected is float:
		assert(is_equal_approx(got, expected))
	else:
		assert(got == expected)


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

	var test_transform := Transform3D.IDENTITY.rotated(Vector3.ONE.normalized(), PI / 4)
	push_transform3D(test_transform)
	assert_eq(get_transform3D(-1), test_transform)

	var result := exec("return Vector3(1, 2, 3)")
	assert(result.status == OK)
	assert_eq(get_vector3(-1), Vector3(1, 2, 3))

	var result2 := exec("return Vector3(Vector3i(1, 2, 3))")
	assert(result2.status == OK)
	assert_eq(get_vector3(-1), Vector3(1, 2, 3))

	var result3 = exec("return Vector2(1, 2):Dot(Vector2(1, 2))")
	assert(result3.status == OK)
	assert_eq(get_number(-1), 5)

	var result4 = exec("return Vector2.Dot(Vector2(3, 4), Vector2(5, 6))")
	assert(result4.status == OK)
	assert_eq(get_number(-1), 39)

	var result5 = exec("return Vector2.FromAngle(5)")
	assert(result5.status == OK)
	assert_eq(get_vector2(-1), Vector2.from_angle(5))

	"""
	var a: int
	const A := 1

	var b: String
	const B := "hello world!"

	var c: Vector2
	const C := Vector2(4, 2)

	var callable := func(p_a, p_b, p_c):
		a = p_a
		b = p_b
		c = p_c
	;

	push_callable(callable)
	set_global("testCallable")

	var result4 = exec("testCallable:Call(1, \"hello world!\", Vector2(4, 2))")
	assert(result4.status == OK)
	assert_eq(a, A)
	assert_eq(b, B)
	assert_eq(c, C)
	"""

	set_top(0)
