do
    -- Reference counting
    local params = PhysicsRayQueryParameters3D.new()
    assert(is_instance_valid(params))
    params = nil
    gccollect()
    assert(not is_instance_valid(params))
end

do
    -- Constructor
    assert(PhysicsRayQueryParameters3D.new():GetClass() == "PhysicsRayQueryParameters3D")
end

do
    -- Singleton getter
    assert(Engine.singleton:GetClass() == "Engine")
end

do
    -- Enums/constants
    assert(Object.ConnectFlags.ONE_SHOT == 4)
    assert(Object.NOTIFICATION_PREDELETE == 1)
end

do
    -- Methods/functions

    -- Namecall
    local params = PhysicsRayQueryParameters3D.new()
    assert(params:Get("collide_with_areas") == false)

    -- Free
    local obj = Object.new()
    obj:Free()
    assert(not is_instance_valid(obj))

    local rc = RefCounted.new()
    asserterror(function()
        rc:Free()
    end, "cannot free a RefCounted object")

    -- IsA
    assert(rc:IsA(Object))

    -- Global table
    assert(PhysicsRayQueryParameters3D.IsCollideWithAreasEnabled(params) == false)

    -- Varargs
    params:Call("set", "collide_with_areas", true)
    assert(params.collideWithAreas == true)

    -- Default args
    local params2 = PhysicsRayQueryParameters3D.Create(Vector3.new(1, 2, 3), Vector3.new(4, 5, 6))
    assert(params2.from == Vector3.new(1, 2, 3))
    assert(params2.to == Vector3.new(4, 5, 6))
    assert(params2.exclude:Size() == 0)

    -- Ref return
    params2 = nil
    gccollect()
    assert(not is_instance_valid(params2))
end

do
    -- Setget

    -- Signal
    local node = Node3D.new()
    assert(node.visibilityChanged == Signal.new(node, "visibility_changed"))

    asserterror(function()
        node.visibilityChanged = Signal.new()
    end, "cannot assign to signal 'visibilityChanged'")

    node:Free()

    -- Member access
    local params = PhysicsRayQueryParameters3D.new()
    assert(params.collideWithAreas == false)

    params.collideWithAreas = true
    assert(params.collideWithAreas)

    -- Access with index
    local styleBox = StyleBox.new()
    styleBox.contentMarginBottom = 4.25
    assert(styleBox.contentMarginBottom == 4.25)
end

do
    -- tostring
    local node = Node3D.new()
    assert(String.BeginsWith(tostring(node), "<Node3D#"))
    node:Free()
    assert(tostring(node) == "<Freed Object>")
end

do
    -- Permissions
    asserterror(function()
        FileAccess.Open("res://blah", FileAccess.ModeFlags.READ)
    end, "!!! THREAD PERMISSION VIOLATION: attempted to access 'FileAccess.Open'. needed permissions: 4, got: 1 !!!")
end

do
    -- Invalid global access
    asserterror(function()
        return Object.duhduhduh
    end, "'duhduhduh' is not a valid member of Object")
end

do
    -- Equality
    local obj1 = PhysicsRayQueryParameters3D.new()
    local obj2 = PhysicsRayQueryParameters3D.new()

    assert(obj1 == obj1)
    assert(obj1 ~= obj2)
end
