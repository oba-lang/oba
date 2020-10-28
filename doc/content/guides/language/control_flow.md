---
title: "Control Flow"
date: 2020-10-22T09:12:01-04:00
draft: false
---

## If statements

If-statements are used to branch on values that are `true` or `false`:

```
if true {
 // Do something.
} else {
 // Do something else.
}
```

Notice that an if-statement does not need a following `else` block.

## Loops

The `while` keyword can be used to implement a loop:

```
import "system"

while true {
  system::print("looping")     
}
```

## Conditionals

The `true` in `if true` is known as the _conditional expression_.  In Oba, the
conditional expression _must_ evaluate to either `true` or `false`.  Values are
not "truthy" or "falsey" as they are in other scripting languages such as
Python:

```
if 1 { // Error
  ...
}
```

