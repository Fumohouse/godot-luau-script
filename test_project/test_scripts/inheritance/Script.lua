local Script = gdclass("Script", "Base.lua")

function Script:GetProperty2()
    return "hihi"
end

Script:RegisterProperty("property2", Enum.VariantType.STRING)
    :SetGet(nil, "GetProperty2")
    :Default("hi")

function Script:Method2()
    return "guy"
end

return Script
