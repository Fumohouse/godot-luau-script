# Task Scheduler

`godot-luau-script` borrows the task scheduler (among other concepts) from Roblox.

Currently, the task scheduler is only responsible for resuming yielded threads and running the garbage collector.

It updates once every frame.
