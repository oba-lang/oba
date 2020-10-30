---
title: "Data Types"
date: 2020-10-30T10:23:58-04:00
---

Oba does not support classes in the same way that object-oriented languages do.
Instead oba supports "Data types" which are sort of like Python's named
tuples. A data type has a _family_ along with a number of named _constructors_
for the type. For example:

```
data Cycle = Bicycle | Motorcycle color
```

The above example is a simple definition of a `Cycle` data type family. A 
`Cycle` can be created using either of the two constructors `Bicycle` and
`Motorcycle`. The `color` name above represents a parameter for the
`Motorcycle` constructor.  It's important to note that `color` is _not_ the
_name_ of a field, it is just a placeholder that tells Oba, "The Motorcycle
constructor requires one argument". The name `color` is only symbolic, and
may indicate to readers that a string representing the color of the motorcycle
should be held in that field.

## Instances

Instances are unique occurences of data types. These are similar to instances in
object-oriented programming, except they do not have methods; They are just
ordered collections of values belonging to a particular data type family.

Here's an example of how to create a `Cycle` using either constructor:

```
let bicycle = Bicycle()
let motorcycle = Motorcycle("Red")
```

If you print an instance, you'll see its data type family, constructor, and the
fields that it contains:

```
system::print(Motorcycle("Red")) // Prints: (Cycle::Motorcycle,Red)
```

## Pattern Matching

Data types can be used as patterns in `match` expressions:

```
let color = match motorcycle
  | Bicycle          = "grey"
  | Motorcycle color = color
  ;

system::print(color) // Prints: "Red"
```

For more information on pattern-matching, see [Pattern Matching].

[Pattern Matching]: {{< relref path="pattern_matching.md" >}}
