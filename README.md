### FastDelete

FastDelete is a Windows command-line tool similar to the command-prompt "rd /s
/q" (or rm -rf if you prefer) designed for multi-core systems with fast SSD
storage.

```
Syntax: FastDelete [--keep-root] <Directory1> [Directory2...]
```

Note it does not support deleting single files (just directories of files).

If --keep-root is passed the directories specified will be emptied but not
deleted.

You probably won't notice a difference for small numbers of files but for a
million files nested in a bunch of directories, FastDelete is over twice as
fast on my i7-9750H laptop:

* `rd /s /q` took 84 seconds (20% CPU load)
* `FastDelete` took 32 seconds (80% CPU load)

On my Ryzen 9 3950X workstation the difference is more striking presumably
because it has more cores to work with:

* `rd /s /q` took 283 seconds (6% CPU load)
* `FastDelete` took 39 seconds (50% CPU load)

Because Windows doesn't let you delete read-only files without a separate
API call first it's considerably slower to delete them. But, for a million 
read-only files, FastDelete is still twice as fast on my i7-9750H laptop:

* `rd /s /q` took 149 seconds (20% CPU load)
* `FastDelete` took 65 seconds (60% CPU load)

Note: it splits work by directory and does a breadth-first scan which means it won't 
help if you're deleting a single directory.

