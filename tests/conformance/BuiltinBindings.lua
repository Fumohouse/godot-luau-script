do
    -- Constructor
    local vec = Vector3.new(1, 2, 3)
    assert(vec.x == 1)
    assert(vec.y == 2)
    assert(vec.z == 3)

    local vec2 = Vector3.new(Vector3i.new(4, 5, 6))
    assert(vec2.x == 4)
    assert(vec2.y == 5)
    assert(vec2.z == 6)
end

do
    -- Methods/functions

    -- Namecall
    assert(Vector2.new(1, 2):Dot(Vector2.new(1, 2)) == 5)

    -- Static
    local vec = Vector2.FromAngle(5)
    assert(is_equal_approx(vec.x, math.cos(5)))

    -- Varargs
    local styleBox = StyleBox.new()
    local callable = Callable.new(styleBox, "SetContentMargin")
    callable:Call(Enum.Side.BOTTOM, 1.5)

    assert(styleBox.contentMarginBottom == 1.5)

    -- Default arguments
    -- Non-vararg
    assert(Color.FromHsv(0.5, 0.5, 0.5) == Color.FromHsv(0.5, 0.5, 0.5, 1))

    -- No method exists with vararg and default args

    -- Non-const
    -- Non-vararg
    local array = PackedStringArray.new()
    array:PushBack("hello")
    array:PushBack("hi")
    assert(array:Size() == 2)
    assert(array:Get(1) == "hello")
    assert(array:Get(2) == "hi")

    -- No non-const vararg method exists
end

do
    -- Setget
    assert(Vector2.new(123, 456).y == 456)

    asserterror(function()
        Vector2.new(123, 456).y = 0
    end, "type 'Vector2' is read-only")

    -- Indexed
    local array = PackedStringArray.new()
    array:PushBack("hi there")
    array:Set(1, "hello")
    assert(array:Size() == 1)
    assert(array:Get(1) == "hello")

    -- Keyed
    local dict = Dictionary.new()
    dict:Set(Vector2.new(1, 2), "hi!")
    assert(dict:Get(Vector2.new(1, 2)) == "hi!")

    asserterror(function()
        local dict = Dictionary.new()
        dict:Get("hi")
    end, "this Dictionary does not have key 'hi'")
end

do
    -- Operators
    assert(Vector2.new(1, 2) == Vector2.new(1, 2))
    assert(Vector2.new(1, 2) ~= Vector2.new(2, 2))
    assert(Vector2.new(1, 2) + Vector2.new(3, 4) == Vector2.new(4, 6))
    assert(-Vector2.new(1, 2) == Vector2.new(-1, -2))

    -- Forward and reverse multiply
    assert(Vector3.ONE * 3 == Vector3.new(3, 3, 3))
    assert(3 * Vector3.ONE == Vector3.new(3, 3, 3))

    -- Special case: length
    local arr = PackedStringArray.new()
    arr:PushBack("a")
    arr:PushBack("b")
    arr:PushBack("c")
    arr:PushBack("d")
    arr:PushBack("e")
    assert(#arr == 5)
end

do
    -- Enums/constants
    assert(Vector3.Axis.Z == 2)
    assert(Vector2.ONE == Vector2.new(1, 1))
end

do
    -- tostring
    assert(tostring(Vector3.new(0, 1, 2)) == "(0, 1, 2)")
end

do
    -- Invalid global access
    asserterror(function()
        return Vector3.duhduhduh
    end, "'duhduhduh' is not a valid member of Vector3")
end

do
    -- Array __iter
    local array = PackedStringArray.new()
    array:PushBack("1!")
    array:PushBack("2!")
    array:PushBack("3!")

    local copy = PackedStringArray.new()
    for _, v in array do
        copy:PushBack(v)
    end

    assert(array == copy)
end

do
    -- Dictionary __iter
    local dict = Dictionary.new()
    dict:Set("a", "A")
    dict:Set("b", "B")
    dict:Set("c", "C")

    local copy = Dictionary.new()
    for k, v in dict do
        copy:Set(k, v)
    end

    assert(dict == copy)
end

do
    -- Callable constructor
    local params = PhysicsRayQueryParameters3D.new()

    local callable = Callable.new(params, "GetClass")
    assert(callable:GetObject() == params)
    assert(callable:GetMethod() == "get_class")

    asserterror(function()
        return Callable.new(params, "whatwhatwhat")
    end, "'whatwhatwhat' is not a valid method of this object")

    assert(function()
        local server = TCPServer.new()
        return Callable.new(server, "GetLocalPort")
    end, "!!! THREAD PERMISSION VIOLATION: attempted to access 'Godot.Object.Object.Call'. needed permissions: 1, got: 0 !!!")
end
