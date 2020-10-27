---
title: "Modules"
date: 2020-10-22T09:11:40-04:00
draft: false
---

Modules are reusable units Oba code. Modules correspond to individual `.oba`
files and can be used to break programs into smaller pieces.

All of the top-level function declarations and global variables defined in a
module are "public" which means that other modules can import that module and
read those symbols.

## Imports

Modules are included into other modules using the `import` keyword:

```
import "system"
```

A module's symbols can be accessed using the `::` operator:

```
system::print("print from another module")
```

For now, Oba only supports importing core modules, which are built into the
interpreter and made available to all programs. In a future release users will
be able to write and import their own modules.

## Core modules

Core modules are imported by name. e.g. `import "system"`

* `system` - Basic I/O and interaction with the host system.
* `time`   - Timing routines.
