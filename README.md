### FastDelete

FastDelete is a Windows command-line tool similar to the command-prompt "rd /s
/q" (or rm -rf if you prefer) designed for multi-core systems with fast SSD
storage.

```
Syntax: FastDelete [--keep-root] <Directory1> [Directory2...]
```

If --keep-root is passed the directories specified will be emptied but not
deleted.

You probably won't notice a difference for small numbers of files but for a
million files nested in a bunch of directories, FastDelete is over twice as
fast on my i7-9750H laptop:

* `rd /s /q` took 84 seconds (12% CPU load)
* `FastDelete` took 32 seconds (100% CPU load)

On my Ryzen 9 3950X workstation the difference is more striking presumably
because it has more cores to work with:

* `rd /s /q` took 283 seconds (6% CPU load)
* `FastDelete` took 39 seconds (50% CPU load)

Note: it splits work by directory and does a breadth-first scan which means it won't 
help if you're deleting a single directory.

