#ifndef oba_token_h
#define oba_token_h

#include "oba_value.h"

// Oba Tokens.
//
// WARNING: When updating this table, also update the table of GrammarRules in
// oba_compiler.c.
typedef enum {
  TOK_NOT,
  TOK_ASSIGN,
  TOK_GT,
  TOK_LT,
  TOK_GTE,
  TOK_LTE,
  TOK_EQ,
  TOK_NEQ,
  TOK_COMMA,
  TOK_SEMICOLON,
  TOK_GUARD,
  TOK_LPAREN,
  TOK_RPAREN,
  TOK_LBRACK,
  TOK_RBRACK,
  TOK_PLUS,
  TOK_MINUS,
  TOK_MULTIPLY,
  TOK_DIVIDE,
  TOK_MEMBER,

  TOK_IDENT,
  TOK_NUMBER,

  // A string literal.
  TOK_STRING,

  // A string literal with an interpolated expression.
  //
  // The string:
  //
  //     "a + b = %(a) + %(b)"
  //
  // is roughly compiled as a sequence of add operations on the tokens:
  // TOK_INTERPOLATION ("a + b = ")
  // TOK_IDENT         (a)
  // TOK_INTERPOLATION (" + ")
  // TOK_IDENT         (b)
  // TOK_STRING        ("")
  TOK_INTERPOLATION,
  TOK_NEWLINE,

  // Keywords
  TOK_DEBUG,
  TOK_LET,
  TOK_TRUE,
  TOK_FALSE,
  TOK_IF,
  TOK_ELSE,
  TOK_WHILE,
  TOK_MATCH,
  TOK_FN,
  TOK_IMPORT,
  TOK_DATA,

  TOK_ERROR,
  TOK_EOF,
} TokenType;

typedef struct {
  TokenType type;

  // The beginning of the token, pointing directly at the source.
  const char* start;

  // The number of characters in the token.
  int length;

  // The 1-based line where the token appears.
  int line;

  // The parsed value if the token is a literal.
  Value value;
} Token;

#endif
