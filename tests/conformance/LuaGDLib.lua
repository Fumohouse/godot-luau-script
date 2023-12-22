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
