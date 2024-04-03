do
    -- globals

    assert(gdtypeof(1) == Enum.VariantType.INT)
    assert(gdtypeof(1.5) == Enum.VariantType.FLOAT)

    assert(typeof(SN"x") == "StringName")
    assert(typeof(NP"/") == "NodePath")

    local int = tointeger(1.01)
    assert(int == 1)
    assert(gdtypeof(int) == Enum.VariantType.INT)

    local float = tofloat(0)
    assert(float > 0)
    assert(float < 0.001)
    assert(gdtypeof(float) == Enum.VariantType.FLOAT)
end

do
    -- 64-bit integer support

    local i1Str = "9007199254740993"
    local i1 = I64(i1Str)
    local i2 = I64(2)
    local i3 = I64(5)

    assert(i2 + i3 == 7)
    assert(i2 - i3 == -3)
    assert(i2 * i3 == 10)
    assert(i2 / i3 == 0.4)
    assert(i2 % i3 == 2)
    assert(i2 ^ i3 == 32)

    assert(-i2 == -2)

    assert(i2 == I64(2))
    assert(i2 ~= i3)
    assert(i2 < i3)
    assert(i3 > i2)
    assert(i2 <= i3)
    assert(i3 >= i2)

    assert(tostring(i1) == i1Str)

    -- Interop. with Luau number type
    assert(2 + i2 == 4)
    assert(2 - i2 == 0)
    assert(2 * i2 == 4)
    assert(2 / i2 == 1)
    assert(2 % i2 == 0)
    assert(2 ^ i2 == 4)
end
