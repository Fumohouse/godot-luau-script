do
    -- globals

    assert(gdtypeof(1) == Enum.VariantType.INT)
    assert(gdtypeof(1.5) == Enum.VariantType.FLOAT)

    assert(typeof(SN"x") == "StringName")
    assert(typeof(NP"/") == "NodePath")
end
