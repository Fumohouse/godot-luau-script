local Base = gdclass("Base", Object)

function Base._Init(obj, tbl)
    tbl.property1 = "hey"
    tbl.property2 = "hi"
end

Base:RegisterProperty("property1", Enum.VariantType.STRING)
    :Default("hey")

Base:RegisterProperty("property2", Enum.VariantType.STRING)
    :Default("hi")

function Base:Method1()
    return "there"
end

Base:RegisterMethod("Method1")
    :ReturnVal({ type = Enum.VariantType.STRING })

function Base:Method2()
    return "world"
end

Base:RegisterMethod("Method2")
    :ReturnVal({ type = Enum.VariantType.STRING })

return Base
