extends LuauTest


func assert_eq(got: Variant, expected: Variant):
	print("Got: %s, Expected: %s" % [got, expected])

	if expected is float:
		assert(is_equal_approx(got, expected))
	else:
		assert(got == expected)


func assert_eval_ok(src: String):
	var result := exec(src)
	if result.has("error"):
		push_error("Luau Error: ", result.error)

	assert(result.status == OK)


func assert_eval_eq(src: String, expected: Variant):
	assert_eval_ok(src)
	assert_eq(get_variant(-1), expected)


func _test_stack_ops():
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

	set_top(0)


func _test_builtins():
	# constructor
	assert_eval_eq("return Vector3(1, 2, 3)", Vector3(1, 2, 3))

	# constructor w/ other builtin
	assert_eval_eq("return Vector3(Vector3i(4, 5, 6))", Vector3(4, 5, 6))

	# method
	assert_eval_eq("return Vector2(1, 2):Dot(Vector2(1, 2))", 5)

	# method invoked from global table
	assert_eval_eq("return Vector2.Dot(Vector2(3, 4), Vector2(5, 6))", 39)

	# static function
	assert_eval_eq("return Vector2.FromAngle(5)", Vector2.from_angle(5))

	# member access
	assert_eval_eq("return Vector2(123, 456).y", 456)

	# index access
	assert_eval_eq("return Vector2(123, 456)[2]", 456)

	# member/index set
	assert_eval_eq("""\
local a = Vector2(1, 2)
a.x = 3
a[2] = 4

return a
""", Vector2(3, 4))

	# operator: equality
	assert_eval_eq("return Vector2(1, 2) == Vector2(1, 2)", true)

	# operator: inequality
	assert_eval_eq("return Vector2(1, 2) ~= Vector2(1, 2)", false)

	# operator: addition
	assert_eval_eq("return Vector2(1, 2) + Vector2(3, 4)", Vector2(4, 6))

	# operator: unary minus
	assert_eval_eq("return -Vector2(1, 2)", Vector2(-1, -2))

	# operator special case: length
	var arr := PackedStringArray(["a", "b", "c", "d", "e", "f"])
	push_packed_string_array(arr)
	set_global("testArr")

	assert_eval_eq("return #testArr", arr.size())

	# constants
	assert_eval_eq("return Vector2.ONE", Vector2.ONE)

	# constants are pushed anew each time
	assert_eval_eq("""\
Vector2.ONE.x = 0
return Vector2.ONE
""", Vector2.ONE)

	# enums
	assert_eval_eq("return Vector3.Axis.AXIS_Z", 2)

	# varargs (not working)
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


func _test_refcount():
	var test_refcounted := PhysicsRayQueryParameters3D.new()

	push_object(test_refcounted)
	set_top(0)
	gc_collect()

	var weak_ref = weakref(test_refcounted)
	assert(is_instance_valid(weak_ref.get_ref()))

	return weak_ref


func _test_classes():
	# reference counting
	assert_eq(is_instance_valid(_test_refcount().get_ref()), false)

	# constructor & collecting
	assert_eval_ok("return PhysicsRayQueryParameters3D()")
	var obj = weakref(get_object(-1))
	assert_eq(obj.get_ref().get_class(), "PhysicsRayQueryParameters3D")
	set_top(0)
	gc_collect()
	assert_eq(is_instance_valid(obj.get_ref()), false)

	set_top(0)


func _ready():
	_test_stack_ops()
	_test_builtins()
	_test_classes()
