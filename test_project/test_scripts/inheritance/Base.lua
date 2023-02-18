local BaseImpl = {}
local Base = gdclass("Base", Object)
    :RegisterImpl(BaseImpl)

function BaseImpl._Init(obj, tbl)
    tbl.property1 = "hey"
    tbl.property2 = "hi"
end

Base:RegisterProperty("property1", Enum.VariantType.STRING)
    :Default("hey")

Base:RegisterProperty("property2", Enum.VariantType.STRING)
    :Default("hi")

function BaseImpl:Method1()
    return "there"
end

Base:RegisterMethod("Method1")
    :ReturnVal({ type = Enum.VariantType.STRING })

function BaseImpl:Method2()
    return "world"
end

Base:RegisterMethod("Method2")
    :ReturnVal({ type = Enum.VariantType.STRING })

return Base
