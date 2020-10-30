---
title: "Pattern Matching"
date: 2020-10-30T10:23:58-04:00
---

Oba supports pattern matching on both literal values and [data types] using
_match expressions_. Match expressions have the form:

```
match <value> | <pattern> = <expression> | <pattern> = <expression> ... ;
```

The rules for pattern matching are:

* `value` can be a literal or a variable.
* `pattern` can be a literal or an instance of a data type (more on this below).

Pattern order also matters: patterns are considered from top to bottom, and the
first one that matches the value wins.

## Literal Patterns

Literal patterns are the simplest: They match if the given value is equal to the
literal:

<!-- example literalPatterns -->

The expression above always evaluates to "one", because the literal pattern `1`
always matches the value `1`.

## Variable Patterns

Variable patterns are almost as simple as literal patterns: They match if the
given value has the same value as the variable:

<!-- example variablePatterns -->

The expression above always evaluates to "one", because the variable pattern
`x` always matches the value stored in the variable `x`, by definition.

Variable patterns are a good way to specify a catch-all pattern if none of the
other patterns in the expression match a value:

<!-- example catchAllPattern -->

## Constructor Patterns

You can use pattern matching to both detect an instance's [data type] _and_
grab the values stored in in its fields. For example:

<!-- example constructorPatterns -->

## Mixed Patterns

A match expression can use many different patterns at once. Here's an example
that uses literal, variable, and constructor patterns all at once:

<!-- example mixedPatterns -->

## Error handling

It's possible that none of the patterns in a match expression match the given
value. In this case an error is raised at runtime. As a best practice, always
use a catch-all, variable pattern when possible.

[data types]: {{< relref path="/guides/language/data.md" >}}

