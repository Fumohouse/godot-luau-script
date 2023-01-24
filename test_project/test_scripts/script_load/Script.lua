local TestClass = gdclass("TestClass")
    :Tool(true)

TestClass:RegisterSignal("testSignal")
    :Args(
        { name = "arg1", type = Enum.VariantType.FLOAT }
    )

TestClass:RegisterRpc("TestRpc", {
    rpcMode = MultiplayerAPI.RPCMode.ANY_PEER,
    transferMode = MultiplayerPeer.TransferMode.RELIABLE,
    callLocal = true,
    channel = 4
})

function TestClass:TestMethod()
end

TestClass:RegisterMethod("TestMethod")

function TestClass:__WeirdMethodName()
end

TestClass:RegisterMethod("__WeirdMethodName")

TestClass:RegisterProperty("testProperty", Enum.VariantType.FLOAT)
    :Default(5.5)

return TestClass
