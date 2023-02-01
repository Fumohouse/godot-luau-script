# Quirks

There are some odd/unexpected quirks with the way `godot-luau-script` works, usually due to the way it's structured.

## Methods and yielding

Calling a class method which yields using the `:` operator will error
because `godot-luau-script` uses a C++-defined `__namecall` function which calls the class method from C++.
This issue is difficult to avoid because `__namecall` is the most optimized method calling route (especially vs. a `__index` function).

In order to call a class method which yields, you will need to call it "from Lua", meaning you need to call it as a regular Lua function.
Typically, this should be done by indexing the class definition.
For example, for a method `YieldingMethod` in class `MyClass`, you should call the method through `MyClass.YieldingMethod(myClassInstance)` rather than
`myClassInstance:YieldingMethod()`.
