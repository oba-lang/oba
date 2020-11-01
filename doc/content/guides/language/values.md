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

# Optional Values

Oba uses a built-in [Data Type] named `Option` to indicate the presence or
absence of a value. The `None` constructor is used to signifiy the absence of a
value, while the `Some` constructor is used to signify the presence of a value:

<!-- example option -->

The `Option` data type is visible to all Oba programs. It can optionally (hehe)
be imported from the "option" module:

<!-- example importOption -->

[Data Type]: {{< relref path="/guides/language/data.md" >}}
