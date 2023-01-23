### FastDelete

FastDelete is a Windows command-line tool similar to the command-prompt "rd /s /q"
(or rm -rf if you prefer) designed for multi-core systems with fast SSD storage.

```
Syntax: FastDelete [--keep-root] <Directory1> [Directory2...]
```

If --keep-root is passed the directories specified will be emptied but not deleted.

You probably won't notice a difference for small numbers of files but for a million
files nested in 96 directories, FastDelete is over twice as fast on my i7-9750H laptop:

* `rd /s /q` took 84 seconds and used about 1.5 CPU cores 
* `FastDelete` took 32 seconds and worked all threads to the fullest

Note: it splits work by directory and does a depth-first scan which means it won't 
help if you're deleting a single directory.

