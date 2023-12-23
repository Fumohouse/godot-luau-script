do
    -- Object sandbox
    local resource = Resource.new()

    LuauInterface.SandboxService:ProtectedObjectAdd(
        resource, Enum.Permissions.OS
    )

	local status, err = pcall(function()
        resource.resourceLocalToScene = true
	end)

	assert(not status)
    assert(String.BeginsWith(err, "exec:10: !!! THREAD PERMISSION VIOLATION: attempted to access '<Resource#"))
    assert(String.EndsWith(err, ">.SetLocalToScene'. needed permissions: 2, got: 1 !!!"))
end
