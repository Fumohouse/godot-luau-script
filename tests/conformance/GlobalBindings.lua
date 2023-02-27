do
    -- Enums/constants
    assert(Enum.VariantOperator.BIT_NEGATE == 19)

    asserterror(function()
        return Enum.NonExistentWhat
    end, "'NonExistentWhat' is not a valid member of Enum")

    asserterror(function()
        return Enum.VariantOperator.BLAH_BLAH
    end, "'BLAH_BLAH' is not a valid member of VariantOperator")

    asserterror(function()
        return Constants.WHATEVER
    end, "'WHATEVER' is not a valid member of Constants")
end

do
    -- Utility functions
    assert(lerp(0.0, 1.0, 0.75) == 0.75)
    print_rich(1, 2, false, "[color=red]hey![/color]", " ", {})
end
