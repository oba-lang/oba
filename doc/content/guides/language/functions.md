---
title: "Functions"
date: 2020-10-22T09:11:47-04:00
draft: false
---

Oba supports functions and closures. Functions in Oba are first-class values:
they can be stored in variables and passed to other functions as arguments.

Functions in oba are declared using the `fn` keyword. The first identifier after
`fn` is the function name, and the rest are the function paramters:

```
fn add a b = a + b
```

A function body may be an expression, or a block-statement. The above code
sample is an example of a function with an expression body. We can write the
same function with a block-statement body, using { and }:

```
fn add a b {
  return a + b
}
```

You may have noticed that functions with expression bodies do not use return
statements. Instead, the compiler implicitly inserts a return statement for you.

## Types

Oba is a dynamic language, so functions do not declare their return types or
their parameter types. Instead, it is common to document the expected inputs
and outputs of a function in a doc comment:

```
// Adds [a] and [b], which must be numbers or strings.
// Both arguments must have the same type.
fn add a b {
  return a + b
}
```

## Closures

Closures are created when a function definition "closes over" a value in the
surrounding scope:

<!-- example closure -->

In the above example, we say that `print` "captures" `value`, since `value` is
defined outside of the body of `print`.

