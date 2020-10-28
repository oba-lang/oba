---
title: "Variables"
date: 2020-10-22T09:12:08-04:00
draft: false
---

Oba has two kinds of variables: Globals and Locals.

__Globals__ are symbols that are declared in a module's top-level scope. This
includes global variables and function names:

```
// module: foo

let x = 1

fn foo {
  system::print("foo")
}
```

In the above example, `a` and `foo` are both globals.

__Locals__ are variables that are declared in a nested scope, such as function
bodies, if-statements, loops, etc.

## Visibility

Globals are visible to other modules that import the current module:

```
// module: bar

import "foo"

foo::foo() // Prints: foo
```

Imported module names are exceptions to this rule: The imported module name
becomes a global variable in the importing module, but is not exposed to other
modules:

```
// module: foo

import "system"
import "time"

system::print(time::now())
```
```
// module: bar

import "system"
import "foo"

system::print(time::now()) // error: time is undefined
```

## Types

Oba's type system is both dynamic and strong. That means:

1. A variable's type is decided at runtime.
2. A variable's type does not change after assignment.

For example: Once a variable holds a number value, it must always hold a number
value:

```
fn makeVar {
  let x = 1
  x = "" // Error
  x = 10 // OK
}
```

## Assignment

All variables must be assigned a value when they are defined. Global variables
cannot be reassigned, but local variables can be reassigned freely:

```
let x // Error

let x = 1
x = 2 // Error

{
  let y = 1
  y = 2 // OK
}
```
