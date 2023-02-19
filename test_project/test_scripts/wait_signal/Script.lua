local Script = gdclass()

Script:RegisterSignal("testSignal")
    :Args(
        { name = "arg1", type = Enum.VariantType.FLOAT },
        { name = "arg2", type = Enum.VariantType.STRING }
    )

return Script
