# Annotations

Class registration in `godot-luau-script` is primarily done through annotations,
an extension of Luau's syntax expressed with comments.

Annotation comments begin with three dashes in a row, and must have no code on
the same line. A block of annotation comments is delimited by a completely blank
line, and the code the annotation refers to is on the line after the last
comment line.

Annotations begin with an `@` followed by a `camelCase` name. Where there are
multiple arguments, they are separated by spaces.

An optional text comment can precede the annotation comments. This comment is
currently parsed but not used for anything.

```lua
--- This is a comment. Documentation can go here.
---
--- The empty comment above indicates a line break.
--- @annotation1 arg1 arg2 arg3
--- @annotation2 arg1 arg2 arg3
local x = 1 -- The above comment block is attached to this line.

--- This is a comment.

--- This is a different comment since the above line is empty.

-- This is not an annotation comment.

--[[
    This is not an annotation comment.
]]

print("hello world!") --- This is not an annotation comment.
```
