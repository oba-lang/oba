---
title: "Syntax"
date: 2020-10-22T09:11:26-04:00
draft: false
---

## Comments

Oba has line comments, only, which start with `//` and end on a newline:

```
// This is a comment
```

## Reserved words

```
debug false let true if else while match fn import
```

## Identifiers

Identifiers start with an underscore or letter and contain letters, digits, and
underscores. Examples:

```
_i
camelCase
PascalCase
_snake_case
alph4num3r1c
ALL_CAPS
```

## Newlines

Oba does not have semicolons, except for in special cases. Instead, newlines
(\n) are used to terminate statements:

```
system::print("hello")
system::print("there")
```

Newlines in the middle of statements are ignored to allow long statements to
be broken across multiple lines:

```
call_function_with_many_args(
  arg0, arg1, arg2, arg3, arg4,
  arg5, arg6, arg7, arg8, arg9
)
```

## Blocks

Blocks are used to create nested scopes and are delimited by curly braces. They
are used as the bodies of functions, if-statements and loops. Some examples:

```
{
  // x is a local variable in this block's scope.
  let x = 1
}

if (true) {
  // This is a different local variable named 'x'.
  let x = 2;
}

while (true) {
  // Yet another unique 'x'.
  let x = 3;
}

fn foo {
  let x = 4; // You get the idea...
}
```

## Strings

Oba strings are surrounded by double-quotes and can span newlines:

```
"This is a string"

"This
is
also
a
string
"
```

Values can be interpolated within strings using `%()` notation:

```
import "system"

let a = 1;
let b = 2;

system::print("%(a) + %(b) = %(a + b)") // Prints: 1 + 2 = 3
```

The interpolated expression can span multiple lines as well. This prints the
same thing as the above example:

```
system::print("%(a) + %(b) = %(
a + b
)")
```

## Precedence and associativity

Oba has both prefix and infix operators. Unary operators such as the "not" 
operator `!` are always right-associative. Binary operators such as `+-*/` are
always left-associative.  The full set of operators is described in the table
below:

| Precedence | Operators       | Description     | Associativity |
|------------|-----------------|-----------------|---------------|
| 1          | ! =             | Not, Assignment | Right         |
| 2          | < > <= >= != == | Comparison      | Left          |
| 3          | + -             | Add,Subtract    | Left          |
| 4          | / *             | Multiply,Divide | Left          |
| 5          | ::              | Membership      | Left          |

## Grammar

```
declaration   = import
              | statement
import        = 'import' STRING
statement     = functionDefn
              | variableDecl
              | debugStmt
              | blockStmt
              | ifStmt
              | whileStmt
              | expression
functionDefn  = 'fn' IDENT parameterList functionBody
variableDecl  = 'let' IDENT '=' expression
debugStmt     = 'debug' expression
blockStmt     = '{' declaration* '}'
ifStmt        = 'if' expression statement ('else' statement)?
whileStmt     = 'while' expression statement
expression    = grouping
              | PREFIX_OP expression
              | TERM INIFIX_OP expression
grouping      = '(' expression* ')
parameterList = IDENT*
functionBody  = expression
              | blockStmt
TERM          = IDENT
              | NUMBER
              | STRING
              | functionCall
IDENT         = ([a-zA-Z_][a-zA-Z_0-9]*) ('::' IDENT)*
STRING        = '"' [^'"']* '"'
NUMBER        = [0-9]+
functionCall  = IDENT '(' (expression ','?)* ')'
PREFIX_OP     = '!'
INIFIX_OP     = '+' '-' '*' '/'

```
