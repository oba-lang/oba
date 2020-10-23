---
title: "Values"
date: 2020-10-22T09:11:50-04:00
draft: false
---

Values are built-in object types. They can be created through *literals*,
which are expressions that evaluate to a value. Values are immutable by
default.

# Booleans

There are two boolean literals, `true` and `false`.

```
true
false
```

# Numbers

Oba uses double-precision floating point for all numeric values

```
0
1234
-1234
3.14159
```

# Functions

Functions are first class values in Oba. They can be stored in variables and
passed as arguments to other functions.

<!-- example functions -->

# Null

Oba has a singleton `null` value which indicates the absence of a value. It
is often used as a return value from built-in functions which don't return
anything. It is not possible for user-code to reference `null` directly. For
example, the following is not valid code:

```
let x = null
```

