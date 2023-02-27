do
    -- strext

    -- Length mismatch (handling same between both methods)
    assert(strext.startswith("short", "longer") == false)

    -- startswith
    assert(strext.startswith("startswith", "starts"))
    assert(not strext.startswith("startswith", "ends"))

    -- endswith
    assert(strext.endswith("withends", "ends"))
    assert(not strext.endswith("withends", "starts"))
end
