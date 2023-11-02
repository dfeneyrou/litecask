This folder contains the code of a simple tool for the litecask library.

```
Litecask utility to dump statistics or fully merge a datastore

Syntax: ./build/bin/litecask_tool (stat | file | merge) <db path> [ options ]

  Options:
   -v    verbose (in datastore log file)
   -vv   more verbose logs
   -s=<dataFileMaxBytes>   Used by the merge command. Default is 100000000

  Commands:
   'stats' provides a summary of the database figures (size, items, ...)
   'file'  dumps the high level statistics of each data file
   'merge' performs an offline full merge of the datastore.
```
