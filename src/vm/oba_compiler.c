#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oba_common.h"
#include "oba_compiler.h"
#include "oba_function.h"
#include "oba_token.h"
#include "oba_value.h"
#include "oba_vm.h"

// The maximum number of locals that can be declared in any function scope.
#define MAX_LOCALS UINT8_MAX

// The maximum number of upvalues that can be closed over in any function scope.
#define MAX_UPVALUES UINT8_MAX

// The size of the buffer used to format error messages.
#define MAX_ERROR_SIZE 1024

// The maximum number of instructions that can be skipped by a jump instruction.
//
// The destination of a jump instruction is encoded as a 16-bit unsigned int.
// This value helps ensure we that the destination operand does not exceed this
// limit during compilation, which would result in the VM jumping to the wrong
// instruction.
#define MAX_JUMP UINT16_MAX

// The compiler's view of a local value that is captured by a closure.
typedef struct {
  // The stack slot of this upvalue.
  uint8_t index;

  // Whether the value is a local or an upvalue from the enclosing scope.
  bool isLocal;
} Upvalue;

// A value that lives on the stack.
typedef struct {
  Token token;
  int depth;

  // Whether this local is captured by an upvalue.
  bool isCaptured;
} Local;

typedef struct {
  ObaVM* vm;
  Token current;
  Token previous;

  // Whether the parser encountered an error.
  // Code is not executed if this is true.
  bool hasError;

  // Whether the lexer is currently inside an interpolated expression.
  bool isInterpolating;

  const char* tokenStart;
  const char* currentChar;
  const char* source;

  // The module being parsed.
  ObjModule* module;

  int currentLine;
} Parser;

typedef struct Compiler {
  struct Compiler* parent;
  ObjFunction* function;

  Local locals[MAX_LOCALS];
  Upvalue upvalues[MAX_UPVALUES];
  int localCount;
  int currentDepth;
  Parser* parser;

  // A pointer to the VM, used to store objects allocated during compilation.
  ObaVM* vm;
} Compiler;

void initCompiler(ObaVM* vm, Compiler* compiler, Parser* parser,
                  Compiler* parent) {
  compiler->vm = vm;
  compiler->parent = parent;
  compiler->parser = parser;
  compiler->localCount = 0;
  compiler->currentDepth = 0;
  compiler->function = newFunction(vm, parser->module);
}

static void printError(Compiler* compiler, const char* label,
                       const char* format, va_list args) {
  char message[MAX_ERROR_SIZE];
  int length = sprintf(message, "%s: module %s line %d: ", label,
                       compiler->parser->module->name->chars,
                       compiler->parser->currentLine);
  length += vsprintf(message + length, format, args);
  ASSERT(length > MAX_ERROR_SIZE, "Error message should not exceed buffer");
  fprintf(stderr, "%s\n", message);
}

static void lexError(Compiler* compiler, const char* format, ...) {
  compiler->parser->hasError = true;

  va_list args;
  va_start(args, format);
  printError(compiler, "Parse error", format, args);
  va_end(args);
}

static void error(Compiler* compiler, const char* format, ...) {
  compiler->parser->hasError = true;

  // The lexer already reported this error.
  if (compiler->parser->previous.type == TOK_ERROR)
    return;

  va_list args;
  va_start(args, format);
  printError(compiler, "Compile error", format, args);
  va_end(args);
}

// Forward declarations because the grammar is recursive.
static void ignoreNewlines(Compiler*);
static void statement(Compiler*);
static void grouping(Compiler*, bool);
static void unaryOp(Compiler*, bool);
static void infixOp(Compiler*, bool);
static void identifier(Compiler*, bool);
static void member(Compiler*, bool);
static void literal(Compiler*, bool);
static void interpolation(Compiler*, bool);
static void matchExpr(Compiler*, bool);
static void declaration(Compiler*);

ObjFunction* endCompiler(Compiler* compiler, const char* debugName,
                         int debugNameLength);

// Bytecode -------------------------------------------------------------------

static void emitByte(Compiler* compiler, int byte) {
  writeChunk(&compiler->function->chunk, byte);
}

static void emitOp(Compiler* compiler, OpCode code) {
  emitByte(compiler, code);
}

// Adds [value] the the Vm's constant pool.
// Returns the address of the new constant within the pool.
static int addConstant(Compiler* compiler, Value value) {
  writeValueArray(&compiler->function->chunk.constants, value);
  return compiler->function->chunk.constants.count - 1;
}

// Registers [value] as a constant value.
//
// Constants are OP_CONSTANT followed by a 16-bit argument which points to
// the constant's location in the constant pool.
static void emitConstant(Compiler* compiler, Value value) {
  // Register the constant in the VM's constant pool.
  int constant = addConstant(compiler, value);
  emitOp(compiler, OP_CONSTANT);
  emitByte(compiler, constant);
}

static void emitBool(Compiler* compiler, Value value) {
  AS_BOOL(value) ? emitOp(compiler, OP_TRUE) : emitOp(compiler, OP_FALSE);
}

static void emitError(Compiler* compiler, const char* format, ...) {
  va_list args;
  va_start(args, format);
  char message[MAX_ERROR_SIZE];
  int length = vsprintf(message, format, args);
  ASSERT(length < MAX_ERROR_SIZE, "Error message should not exceed buffer");

  Value error = OBJ_VAL(copyString(compiler->vm, message, length));
  emitOp(compiler, OP_ERROR);
  emitByte(compiler, addConstant(compiler, error));
}

static void patchJump(Compiler* compiler, int offset) {
  Chunk* chunk = &compiler->function->chunk;

  // -2 to account for the placeholder bytes.
  int jump = chunk->count - offset - 2;
  if (jump > MAX_JUMP) {
    error(compiler, "Too much code to jump over");
    return;
  }

  chunk->code[offset] = (jump >> 8) & 0xff;
  chunk->code[offset + 1] = jump & 0xff;
}

static int emitJump(Compiler* compiler, OpCode op) {
  emitOp(compiler, op);
  emitByte(compiler, 0xff);
  emitByte(compiler, 0xff);
  return compiler->function->chunk.count - 2;
}

static void emitLoop(Compiler* compiler, int start) {
  emitOp(compiler, OP_LOOP);

  int jump = compiler->function->chunk.count - start - 2;
  if (jump > MAX_JUMP) {
    error(compiler, "Loop body too large");
    return;
  }

  // Store the exact index of the loop start instruction as the operand.
  emitByte(compiler, (start >> 8) & 0xff);
  emitByte(compiler, start & 0xff);
}

static int declareGlobal(Compiler* compiler, Value name) {
  return addConstant(compiler, name);
}

static void defineGlobal(Compiler* compiler, int global) {
  emitOp(compiler, OP_DEFINE_GLOBAL);
  emitByte(compiler, global);
}

// Declares a new local in an uninitialized state.
// Any attempt to use the local before it is initialized is an error.
static void addLocal(Compiler* compiler, Token name) {
  Local* local = &compiler->locals[compiler->localCount++];
  local->token = name;
  local->depth = -1;
  local->isCaptured = false;
}

static int addUpvalue(Compiler* compiler, int slot, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;
  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = slot;
  return compiler->function->upvalueCount++;
}

static void markInitialized(Compiler* compiler) {
  // We cannot have declared any new locals before defining this one, because
  // assignments do not nest inside expressions. The local we're defining is
  // the most recent one.
  Local* local = &compiler->locals[compiler->localCount - 1];
  local->depth = compiler->currentDepth;
}

static bool identifiersMatch(Token a, Token b) {
  return a.length == b.length && memcmp(a.start, b.start, a.length) == 0;
}

static int declareVariable(Compiler* compiler, Token name) {
  if (compiler->currentDepth == 0) {
    return declareGlobal(
        compiler, OBJ_VAL(copyString(compiler->vm, name.start, name.length)));
  }

  int slot = compiler->localCount;

  // Ensure the variable is is not already declared in this scope.
  for (int slot = compiler->localCount - 1; slot >= 0; slot--) {
    Local local = compiler->locals[slot];
    if (local.depth < compiler->currentDepth) {
      break;
    }
    if (identifiersMatch(name, local.token)) {
      error(compiler, "Variable with this name already declared in this scope");
      return 0;
    }
  }

  addLocal(compiler, name);
  return 0;
}

static void defineVariable(Compiler* compiler, int variable) {
  if (compiler->currentDepth > 0) {
    // Local variables live on the stack, so we don't need to set anything.
    markInitialized(compiler);
    return;
  }

  defineGlobal(compiler, variable);
}

// Finds a local variable named [name] in the current scope.
// Returns a negative number if it is not found.
static int resolveLocal(Compiler* compiler, Token name) {
  // Find the first local whose depth is gte the current scope and whose
  // token matches `name`.
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local local = compiler->locals[i];
    if (local.token.length == name.length &&
        identifiersMatch(local.token, name)) {
      if (local.depth < 0) {
        error(compiler, "Cannot read local variable in its own initializer");
        return -1;
      } else {
        return i;
      }
    }
  }
  return -1;
}

// Resolves an upvalue from the enclosing function scope.
//
// If this is the first time the upvalue is being resolved, and it is found in
// an outer scope of the enclosing scope, it is recursively registered as an
// upvalue in each enclosing scope to optimize future resolution.
static int resolveUpvalue(Compiler* compiler, Token name) {
  // There are no upvalues if this is the root function scope.
  if (compiler->parent == NULL)
    return -1;

  int local = resolveLocal(compiler->parent, name);
  if (local >= 0) {
    compiler->parent->locals[local].isCaptured = true;
    return addUpvalue(compiler, local, true);
  }

  int upvalue = resolveUpvalue(compiler->parent, name);
  if (upvalue >= 0) {
    return addUpvalue(compiler, upvalue, false);
  }

  return -1;
}

// Grammar --------------------------------------------------------------------

// Parse precedence table.
// Greater value == greater precedence.
typedef enum {
  PREC_NONE,
  PREC_LOWEST,
  PREC_ASSIGN,  // =
  PREC_COND,    // < > <= >= != ==
  PREC_SUM,     // + -
  PREC_PRODUCT, // * /
  PREC_MEMBER,  // ::
} Precedence;

typedef void (*GrammarFn)(Compiler*, bool canAssign);

// Oba grammar rules.
//
// The Pratt parser tutorial at stuffwithstuff describes these as "Parselets".
// The difference between this implementation and parselets is that the prefix
// and infix parselets for the same token are stored on this one struct instead
// in separate tables. This means the same rule implements different operations
// that share the same lexeme.
//
// Additionally, our parser stores tokens internally, so GrammarFn does not
// accept the previous token as an argument, it accesses it using
// parser->previous.
typedef struct {
  GrammarFn prefix;
  GrammarFn infix;
  Precedence precedence;
  const char* name;
} GrammarRule;

// clang-format off

// Pratt parser rules.
//
// See: http://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/
#define UNUSED                     { NULL, NULL, PREC_NONE, NULL }
#define PREFIX(fn)                 { fn, NULL, PREC_NONE, NULL }
#define INFIX(prec, fn)            { NULL, fn, prec, NULL }
#define INFIX_OPERATOR(prec, name) { NULL, infixOp, prec, name }

GrammarRule rules[] =  {
  /* TOK_NOT           */ PREFIX(unaryOp),
  /* TOK_ASSIGN        */ INFIX_OPERATOR(PREC_ASSIGN, "="),
  /* TOK_GT            */ INFIX_OPERATOR(PREC_COND, ">"),
  /* TOK_LT            */ INFIX_OPERATOR(PREC_COND, "<"),
  /* TOK_GTE           */ INFIX_OPERATOR(PREC_COND, ">="),
  /* TOK_LTE           */ INFIX_OPERATOR(PREC_COND, "<="),
  /* TOK_EQ            */ INFIX_OPERATOR(PREC_COND, "=="),
  /* TOK_NEQ           */ INFIX_OPERATOR(PREC_COND, "!="),
  /* TOK_COMMA         */ UNUSED,
  /* TOK_SEMICOLON     */ UNUSED,
  /* TOK_GUARD         */ UNUSED,
  /* TOK_LPAREN        */ PREFIX(grouping),  
  /* TOK_RPAREN        */ UNUSED, 
  /* TOK_LBRACK        */ UNUSED,  
  /* TOK_RBRACK        */ UNUSED, 
  /* TOK_PLUS          */ INFIX_OPERATOR(PREC_SUM, "+"),
  /* TOK_MINUS         */ INFIX_OPERATOR(PREC_SUM, "-"),
  /* TOK_MULTIPLY      */ INFIX_OPERATOR(PREC_PRODUCT, "*"),
  /* TOK_DIVIDE        */ INFIX_OPERATOR(PREC_PRODUCT, "/"),
  /* TOK_MEMBER        */ INFIX(PREC_MEMBER, member),
  /* TOK_IDENT         */ PREFIX(identifier),
  /* TOK_NUMBER        */ PREFIX(literal),
  /* TOK_STRING        */ PREFIX(literal),
  /* TOK_INTERPOLATION */ PREFIX(interpolation),
  /* TOK_NEWLINE       */ UNUSED, 
  /* TOK_DEBUG         */ UNUSED,
  /* TOK_LET           */ UNUSED,
  /* TOK_TRUE          */ PREFIX(literal),
  /* TOK_FALSE         */ PREFIX(literal),
  /* TOK_IF            */ UNUSED,  
  /* TOK_ELSE          */ UNUSED,  
  /* TOK_WHILE         */ UNUSED,  
  /* TOK_MATCH         */ PREFIX(matchExpr),
  /* TOK_FN            */ UNUSED,
  /* TOK_IMPORT        */ UNUSED,
  /* TOK_DATA          */ UNUSED,
  /* TOK_ERROR         */ UNUSED,  
  /* TOK_EOF           */ UNUSED,
};

// Gets the [GrammarRule] associated with tokens of [type].
static GrammarRule* getRule(TokenType type) {
  return &rules[type];
}

typedef struct {
  const char* lexeme;
  size_t length;
  TokenType type;
} Keyword;

static Keyword keywords[] = {
    {"data",   4, TOK_DATA},
    {"debug",  5, TOK_DEBUG},
    {"false",  5, TOK_FALSE},
    {"let",    3, TOK_LET},
    {"true",   4, TOK_TRUE},
    {"if",     2, TOK_IF},
    {"else",   4, TOK_ELSE},
    {"while",  5, TOK_WHILE},
    {"match",  5, TOK_MATCH},
    {"fn",     2, TOK_FN},
    {"import", 6, TOK_IMPORT},
    {NULL,     0, TOK_EOF}, // Sentinel to mark the end of the array.
};

// clang-format on

// Parsing --------------------------------------------------------------------

static char peekChar(Compiler* compiler) {
  return *compiler->parser->currentChar;
}

static char nextChar(Compiler* compiler) {
  char c = peekChar(compiler);
  compiler->parser->currentChar++;
  if (c == '\n')
    compiler->parser->currentLine++;
  return c;
}

static bool matchChar(Compiler* compiler, char c) {
  if (peekChar(compiler) != c)
    return false;
  nextChar(compiler);
  return true;
}

// Returns the type of the current token.
static TokenType peek(Compiler* compiler) {
  return compiler->parser->current.type;
}

static void makeToken(Compiler* compiler, TokenType type) {
  compiler->parser->current.type = type;
  compiler->parser->current.start = compiler->parser->tokenStart;
  compiler->parser->current.length =
      (int)(compiler->parser->currentChar - compiler->parser->tokenStart);
  compiler->parser->current.line = compiler->parser->currentLine;

  // Make line tokens appear on the line containing the "\n".
  if (type == TOK_NEWLINE)
    compiler->parser->current.line--;
}

static void makeNumber(Compiler* compiler) {
  double value = strtod(compiler->parser->tokenStart, NULL);
  compiler->parser->current.value = OBA_NUMBER(value);
  makeToken(compiler, TOK_NUMBER);
}

static bool isName(char c) { return isalpha(c) || c == '_'; }

static bool isNumber(char c) { return isdigit(c); }

// Finishes lexing a string.
static void readString(Compiler* compiler) {
  ByteBuffer buffer;
  initByteBuffer(&buffer);

  TokenType type = TOK_STRING;

  for (;;) {
    char c = nextChar(compiler);
    if (c == '"')
      break;

    if (c == '\0') {
      lexError(compiler, "Unterminated string.");
      compiler->parser->currentChar--;
      break;
    }

    if (c == '%') {
      if (nextChar(compiler) != '(') {
        lexError(compiler, "Expected '(' after '%%'.");
      }
      compiler->parser->isInterpolating = true;
      type = TOK_INTERPOLATION;
      break;
    }

    if (c == '\\') {
      char nc = nextChar(compiler);
      switch (nc) {
      case '"':
        writeByteBuffer(&buffer, '"');
        break;
      case '%':
        writeByteBuffer(&buffer, '%');
        break;
      case '\\':
        writeByteBuffer(&buffer, '\\');
        break;
      case 'n':
        writeByteBuffer(&buffer, '\n');
        break;
      case 'r':
        writeByteBuffer(&buffer, '\r');
        break;
      default:
        lexError(compiler, "Invalid escape character '%c'.", nc);
        break;
      }
    } else {
      writeByteBuffer(&buffer, c);
    }
  }

  makeToken(compiler, type);

  Value string = OBJ_VAL(copyString(compiler->vm, buffer.bytes, buffer.count));
  freeByteBuffer(&buffer);
  compiler->parser->current.value = string;
}

// Finishes lexing an identifier.
static void readName(Compiler* compiler) {
  while (isName(peekChar(compiler)) || isdigit(peekChar(compiler))) {
    nextChar(compiler);
  }

  size_t length = compiler->parser->currentChar - compiler->parser->tokenStart;
  for (int i = 0; keywords[i].lexeme != NULL; i++) {
    if (length == keywords[i].length &&
        memcmp(compiler->parser->tokenStart, keywords[i].lexeme, length) == 0) {
      makeToken(compiler, keywords[i].type);
      return;
    }
  }
  makeToken(compiler, TOK_IDENT);
}

static void readNumber(Compiler* compiler) {
  while (isNumber(peekChar(compiler))) {
    nextChar(compiler);
  }
  makeNumber(compiler);
}

static void skipLineComment(Compiler* compiler) {
  // A comment goes until the end of the line.
  while (peekChar(compiler) != '\n' && peekChar(compiler) != '\0') {
    nextChar(compiler);
  }
}

// Lexes the next token and stores it in [parser.current].
static void nextToken(Compiler* compiler) {
  compiler->parser->previous = compiler->parser->current;

  if (compiler->parser->current.type == TOK_EOF)
    return;

#define IF_MATCH_NEXT(next, matched, unmatched)                                \
  do {                                                                         \
    if (matchChar(compiler, next))                                             \
      makeToken(compiler, matched);                                            \
    else                                                                       \
      makeToken(compiler, unmatched);                                          \
  } while (0)

  while (peekChar(compiler) != '\0') {
    compiler->parser->tokenStart = compiler->parser->currentChar;
    char c = nextChar(compiler);
    switch (c) {
    case ' ':
    case '\r':
    case '\t':
      break;
    case '\n':
      makeToken(compiler, TOK_NEWLINE);
      return;
    case ',':
      makeToken(compiler, TOK_COMMA);
      return;
    case ';':
      makeToken(compiler, TOK_SEMICOLON);
      return;
    case '|':
      makeToken(compiler, TOK_GUARD);
      return;
    case '(':
      makeToken(compiler, TOK_LPAREN);
      return;
    case ')': {
      if (compiler->parser->isInterpolating) {
        // This is the end of an interpolated expression.
        compiler->parser->isInterpolating = false;
        readString(compiler);
        return;
      }
      makeToken(compiler, TOK_RPAREN);
      return;
    }
    case '{':
      makeToken(compiler, TOK_LBRACK);
      return;
    case '}':
      makeToken(compiler, TOK_RBRACK);
      return;
    case '+':
      makeToken(compiler, TOK_PLUS);
      return;
    case '-':
      makeToken(compiler, TOK_MINUS);
      return;
    case '*':
      makeToken(compiler, TOK_MULTIPLY);
      return;
    case '!':
      IF_MATCH_NEXT('=', TOK_NEQ, TOK_NOT);
      return;
    case '>':
      IF_MATCH_NEXT('=', TOK_GTE, TOK_GT);
      return;
    case '<':
      IF_MATCH_NEXT('=', TOK_LTE, TOK_LT);
      return;
    case '=':
      IF_MATCH_NEXT('=', TOK_EQ, TOK_ASSIGN);
      return;
    case '/':
      if (matchChar(compiler, '/')) {
        skipLineComment(compiler);
        break;
      }
      makeToken(compiler, TOK_DIVIDE);
      return;
    case '"':
      readString(compiler);
      return;
    case ':':
      if (matchChar(compiler, ':')) {
        makeToken(compiler, TOK_MEMBER);
        return;
      }
      // Fallthrough to error handler.
    default:
      if (isName(c)) {
        readName(compiler);
        return;
      }
      if (isNumber(c)) {
        readNumber(compiler);
        return;
      }
      lexError(compiler, "Invalid character '%c'.", c);
      compiler->parser->current.type = TOK_ERROR;
      compiler->parser->current.length = 0;
      return;
    }
  }

#undef IF_MATCH_NEXT

  // No more source left.
  compiler->parser->tokenStart = compiler->parser->currentChar;
  makeToken(compiler, TOK_EOF);
}

// Returns true iff the next token has the [expected] Type.
static bool match(Compiler* compiler, TokenType expected) {
  if (peek(compiler) != expected)
    return false;
  nextToken(compiler);
  return true;
}

static bool matchLine(Compiler* compiler) {
  if (!match(compiler, TOK_NEWLINE))
    return false;
  while (match(compiler, TOK_NEWLINE))
    ;
  return true;
}

static void ignoreNewlines(Compiler* compiler) { matchLine(compiler); }

// Moves past the next token which must have the [expected] type.
// If the type is not as expected, this emits an error and attempts to continue
// parsing at the next token.
static void consume(Compiler* compiler, TokenType expected,
                    const char* errorMessage) {
  nextToken(compiler);
  if (compiler->parser->previous.type != expected) {
    error(compiler, errorMessage);
    if (compiler->parser->current.type == expected) {
      nextToken(compiler);
    }
  }
}

// AST ------------------------------------------------------------------------

static void parse(Compiler* compiler, int precedence) {
  nextToken(compiler);
  Token token = compiler->parser->previous;

  GrammarFn prefix = rules[token.type].prefix;
  if (prefix == NULL) {
    error(compiler, "Parse error %d", token.type);
    return;
  }

  bool canAssign = precedence < PREC_COND;
  prefix(compiler, canAssign);

  while (precedence < rules[compiler->parser->current.type].precedence) {
    nextToken(compiler);
    GrammarFn infix = rules[compiler->parser->previous.type].infix;
    infix(compiler, canAssign);
  }
}

static void expression(Compiler* compiler) { parse(compiler, PREC_LOWEST); }

static void constructor(Compiler* compiler, ObjString* family) {
  consume(compiler, TOK_IDENT, "Expected an identifier");

  Token nameToken = compiler->parser->previous;
  ObjString* name = copyString(compiler->vm, nameToken.start, nameToken.length);
  int variable = declareVariable(compiler, nameToken);

  // constructor names are just for show, and may be used to indicated the
  // expected type of a filed to readers. The only thing the VM keeps track of
  // is the arity of the constructor.
  int arity = 0;
  while (match(compiler, TOK_IDENT))
    arity++;

  ObjCtor* ctor = newCtor(compiler->vm, family, name, arity);

  // This always creates a global because data types can only be declared at
  // the top-level.
  emitConstant(compiler, OBJ_VAL(ctor));
  defineVariable(compiler, variable);
}

static void data(Compiler* compiler) {
  consume(compiler, TOK_IDENT, "Expected an identifier.");
  ObjString* family = copyString(compiler->vm, compiler->parser->previous.start,
                                 compiler->parser->previous.length);
  consume(compiler, TOK_ASSIGN, "Expected '='");

  do {
    ignoreNewlines(compiler);
    constructor(compiler, family);
  } while (match(compiler, TOK_GUARD));
}

static void variableDeclaration(Compiler* compiler) {
  consume(compiler, TOK_IDENT, "Expected an identifier.");
  // Get the name, but don't declare it yet; A variable should not be in scope
  // in its own initializer.
  Token name = compiler->parser->previous;
  int variable = declareVariable(compiler, name);

  // Compile the initializer.
  consume(compiler, TOK_ASSIGN, "Expected '='");
  expression(compiler);

  // Now define the variable.
  defineVariable(compiler, variable);
}

static void debugStmt(Compiler* compiler) {
  expression(compiler);
  emitOp(compiler, OP_DEBUG);
}

static void enterScope(Compiler* compiler) { compiler->currentDepth++; }

static void exitScope(Compiler* compiler) {
  compiler->currentDepth--;
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local local = compiler->locals[i];
    if (local.depth > compiler->currentDepth) {
      compiler->localCount--;
      if (local.isCaptured) {
        emitOp(compiler, OP_CLOSE_UPVALUE);
      } else {
        emitOp(compiler, OP_POP);
      }
    } else {
      break;
    }
  }
}

static void blockStmt(Compiler* compiler) {
  enterScope(compiler);

  ignoreNewlines(compiler);

  do {
    statement(compiler);
    ignoreNewlines(compiler);
  } while (peek(compiler) != TOK_RBRACK && peek(compiler) != TOK_EOF);

  ignoreNewlines(compiler);
  consume(compiler, TOK_RBRACK, "Expected '}' at the end of block");
  exitScope(compiler);
}

static void ifStmt(Compiler* compiler) {
  // Compile the conditional.
  expression(compiler);

  // Emit the jump instruction.
  // When the VM reaches this, the value of the conditional is on the top of the
  // stack, and it will jump based on that value's truthiness.
  int offset = emitJump(compiler, OP_JUMP_IF_FALSE);
  // Compile the "then" branch.
  statement(compiler);
  patchJump(compiler, offset);

  // Compile the "else" branch.
  if (match(compiler, TOK_ELSE)) {
    // At this point the value of the conditional is still on the stack. Skip
    // the else clause if it was truthy.
    offset = emitJump(compiler, OP_JUMP_IF_TRUE);
    statement(compiler);
    patchJump(compiler, offset);
  }

  // Don't forget to pop the conditional
  emitOp(compiler, OP_POP);
}

static void whileStmt(Compiler* compiler) {
  int loopStart = compiler->function->chunk.count;

  // Compile the conditional.
  expression(compiler);
  int offset = emitJump(compiler, OP_JUMP_IF_FALSE);
  statement(compiler);

  // Pop the conditional before looping, since the value is recompiled each time
  // based on the new stack contents.
  emitOp(compiler, OP_POP);
  emitLoop(compiler, loopStart);
  patchJump(compiler, offset);
}

static void functionBlockBody(Compiler* compiler) {
  consume(compiler, TOK_LBRACK, "Expected '{' before function body");
  ignoreNewlines(compiler);

  while (!match(compiler, TOK_RBRACK)) {
    statement(compiler);
    ignoreNewlines(compiler);
  }
}

static void functionBody(Compiler* compiler) {
  if (peek(compiler) == TOK_LBRACK) {
    functionBlockBody(compiler);
    return;
  }

  if (match(compiler, TOK_ASSIGN)) {
    expression(compiler);
    return;
  }

  error(compiler, "Missing function body");
}

static void parameterList(Compiler* compiler) {
  while (match(compiler, TOK_IDENT)) {
    int local = declareVariable(compiler, compiler->parser->previous);
    defineVariable(compiler, local);
    compiler->function->arity++;
  }
}

// TODO(kendal): Dedup this with function definition.
static int lambda(Compiler* compiler) {
  Compiler fnCompiler;
  initCompiler(compiler->vm, &fnCompiler, compiler->parser, compiler);

  enterScope(&fnCompiler);
  parameterList(&fnCompiler);
  ignoreNewlines(&fnCompiler);
  consume(&fnCompiler, TOK_ASSIGN, "Missing lambda expression");
  expression(&fnCompiler);

  ObjFunction* fn = endCompiler(&fnCompiler, "", 0);
  if (fn == NULL)
    return -1;

  emitOp(compiler, OP_CLOSURE);
  emitByte(compiler, addConstant(compiler, OBJ_VAL(fn)));

  for (int i = 0; i < fn->upvalueCount; i++) {
    emitByte(compiler, fnCompiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler, fnCompiler.upvalues[i].index);
  }

  return fn->arity;
}

static void functionDefinition(Compiler* compiler) {
  if (!match(compiler, TOK_IDENT)) {
    error(compiler, "Expected an identifier");
    return;
  }

  Compiler fnCompiler;
  initCompiler(compiler->vm, &fnCompiler, compiler->parser, compiler);

  Token name = compiler->parser->previous;

  enterScope(&fnCompiler);
  parameterList(&fnCompiler);
  ignoreNewlines(&fnCompiler);
  functionBody(&fnCompiler);

  ObjFunction* fn = endCompiler(&fnCompiler, name.start, name.length);
  if (fn == NULL)
    return;

  emitOp(compiler, OP_CLOSURE);
  emitByte(compiler, addConstant(compiler, OBJ_VAL(fn)));

  for (int i = 0; i < fn->upvalueCount; i++) {
    emitByte(compiler, fnCompiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler, fnCompiler.upvalues[i].index);
  }
  defineVariable(compiler, declareVariable(compiler, name));
}

static void statement(Compiler* compiler) {
  if (match(compiler, TOK_FN)) {
    functionDefinition(compiler);
  } else if (match(compiler, TOK_LET)) {
    variableDeclaration(compiler);
  } else if (match(compiler, TOK_DEBUG)) {
    debugStmt(compiler);
  } else if (match(compiler, TOK_LBRACK)) {
    blockStmt(compiler);
  } else if (match(compiler, TOK_IF)) {
    ifStmt(compiler);
  } else if (match(compiler, TOK_WHILE)) {
    whileStmt(compiler);
  } else {
    expression(compiler);
    // TODO(kendal): Add a return-statement and just always pop the result of an
    // expression statement.
    if (compiler->currentDepth == 0) {
      emitOp(compiler, OP_POP);
    }
  }
}

static void import(Compiler* compiler) {
  if (!match(compiler, TOK_STRING)) {
    error(compiler, "Expected a string after 'import'");
    return;
  }

  Token token = compiler->parser->previous;
  Value value =
      OBJ_VAL(copyString(compiler->vm, token.start + 1, token.length - 2));
  int constant = addConstant(compiler, value);

  emitOp(compiler, OP_IMPORT_MODULE);
  emitByte(compiler, (uint8_t)constant);
}

static void declaration(Compiler* compiler) {
  if (match(compiler, TOK_IMPORT)) {
    import(compiler);
  } else if (match(compiler, TOK_DATA)) {
    data(compiler);
  } else {
    statement(compiler);
  }
}

// A parenthesized expression.
static void grouping(Compiler* compiler, bool canAssign) {
  expression(compiler);
  consume(compiler, TOK_RPAREN, "Expected ')' after expression.");
}

static void interpolation(Compiler* compiler, bool canAssign) {
  bool first = true;
  do {
    // The opening string.
    literal(compiler, false);
    ignoreNewlines(compiler);

    // The interpolated expression.
    expression(compiler);
    ignoreNewlines(compiler);

    // Convert the expression result to a string and add it to the previous
    // string literal.
    emitOp(compiler, OP_STRING);
    emitOp(compiler, OP_ADD);

    // If this is not the first set of TOK_INTERPOLATION + TOK_EXPRESSION then
    // add it to the previous one.
    if (!first) {
      emitOp(compiler, OP_ADD);
    }
    first = false;
  } while (match(compiler, TOK_INTERPOLATION));

  // The trailing string.
  consume(compiler, TOK_STRING, "Expect end of string interpolation.");
  literal(compiler, false);
  emitOp(compiler, OP_ADD);
}

static void variable(Compiler* compiler, bool canAssign) {
  OpCode getOp;
  OpCode setOp;

  Token name = compiler->parser->previous;
  bool set = canAssign && match(compiler, TOK_ASSIGN);

  int arg = resolveLocal(compiler, name);
  if (arg >= 0) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(compiler, name)) >= 0) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    if (set) {
      error(compiler, "Cannot reassign global variable");
    }
    Value value = OBJ_VAL(copyString(compiler->vm, name.start, name.length));
    arg = addConstant(compiler, value);
    getOp = OP_GET_GLOBAL;
  }

  if (set) {
    expression(compiler);
  }

  emitOp(compiler, set ? setOp : getOp);
  emitByte(compiler, (uint8_t)arg);
}

static uint8_t argumentList(Compiler* compiler) {
  uint8_t argCount = 0;

  if (peek(compiler) == TOK_RPAREN) {
    return argCount;
  }

  do {
    argCount++;
    expression(compiler);
  } while (match(compiler, TOK_COMMA));
  return argCount;
}

static void functionCall(Compiler* compiler, bool canAssign) {
  consume(compiler, TOK_LPAREN, "Expected '(' before parameter list");
  uint8_t argCount = argumentList(compiler);
  consume(compiler, TOK_RPAREN, "Expected ')' after parameter list");
  emitOp(compiler, OP_CALL);
  emitByte(compiler, argCount);
}

static void identifier(Compiler* compiler, bool canAssign) {
  variable(compiler, canAssign);
  if (peek(compiler) == TOK_LPAREN) {
    functionCall(compiler, canAssign);
  }
}

static void member(Compiler* compiler, bool canAssign) {
  nextToken(compiler);
  Token token = compiler->parser->previous;

  // TODO(kendal) It feels like either identifier() or variable() should be able
  // to handle this case, and member() should not be needed. Find a way to merge
  // them.
  Value value = OBJ_VAL(copyString(compiler->vm, token.start, token.length));
  int arg = addConstant(compiler, value);
  emitOp(compiler, OP_GET_IMPORTED_VARIABLE);
  emitByte(compiler, (uint8_t)arg);

  if (peek(compiler) == TOK_LPAREN) {
    functionCall(compiler, canAssign);
  }
}

static void pattern(Compiler* compiler) {
  nextToken(compiler);

  Token token = compiler->parser->previous;
  switch (token.type) {
  case TOK_TRUE:
    emitOp(compiler, OP_TRUE);
    break;
  case TOK_FALSE:
    emitOp(compiler, OP_FALSE);
    break;
  case TOK_NUMBER:
    emitConstant(compiler, token.value);
    break;
  case TOK_STRING:
    emitConstant(compiler, token.value);
    break;
  case TOK_IDENT:
    variable(compiler, false);
    break;
  default:
    error(compiler, "Expected a constant value.");
  }
}

static void equation(Compiler* compiler) {
  pattern(compiler);

  // Compile the RHS lambda expression, which only gets evaluated if the pattern
  // above matched.
  int arity = lambda(compiler);

  int skipThisEquation = emitJump(compiler, OP_JUMP_IF_NOT_MATCH);

  // The lambda's arguments are already in the correct stack slots if the
  // pattern matched; call the lambda immediately.
  emitOp(compiler, OP_CALL);
  emitByte(compiler, arity);

  int skipRemainingEquations = emitJump(compiler, OP_JUMP);
  patchJump(compiler, skipThisEquation);

  // Compile the remaining equations.
  ignoreNewlines(compiler);
  if (match(compiler, TOK_GUARD)) {
    equation(compiler);
  } else {
    // This is the last equation. Insert an error because the entire match
    // evaluates to nothing if this one is not matched.
    emitError(compiler, "Match expression evaluated to nothing");
  }

  patchJump(compiler, skipRemainingEquations);
}

// TODO(kendal): Emit an error if there are constructor patterns in this match
// expression, and some of the constructors in the family aren't handled.
static void matchExpr(Compiler* compiler, bool canAssign) {
  // Compile the expression to push the value to match onto the stack.
  expression(compiler);
  ignoreNewlines(compiler);

  if (!match(compiler, TOK_GUARD)) {
    error(compiler, "Expected guard after match expression");
    return;
  }

  equation(compiler);
  consume(compiler, TOK_SEMICOLON, "Expected ';'");
}

static void literal(Compiler* compiler, bool canAssign) {
  switch (compiler->parser->previous.type) {
  case TOK_TRUE:
    emitBool(compiler, OBA_BOOL(true));
    break;
  case TOK_FALSE:
    emitBool(compiler, OBA_BOOL(false));
    break;
  case TOK_NUMBER:
  case TOK_STRING:
  case TOK_INTERPOLATION:
    emitConstant(compiler, compiler->parser->previous.value);
    break;
  default:
    error(compiler, "Expected a boolean or number value.");
  }
}

static void unaryOp(Compiler* compiler, bool canAssign) {
  GrammarRule* rule = getRule(compiler->parser->previous.type);
  TokenType opType = compiler->parser->previous.type;

  ignoreNewlines(compiler);

  // Compile the right hand side (right-associative).
  parse(compiler, rule->precedence);

  switch (opType) {
  case TOK_NOT:
    emitOp(compiler, OP_NOT);
    break;
  default:
    error(compiler, "Invalid operator %s", rule->name);
  }
}

static void infixOp(Compiler* compiler, bool canAssign) {
  GrammarRule* rule = getRule(compiler->parser->previous.type);
  TokenType opType = compiler->parser->previous.type;

  ignoreNewlines(compiler);

  // Compile the right hand side (left-associative).
  parse(compiler, rule->precedence);

  switch (opType) {
  case TOK_PLUS:
    emitOp(compiler, OP_ADD);
    return;
  case TOK_MINUS:
    emitOp(compiler, OP_MINUS);
    return;
  case TOK_MULTIPLY:
    emitOp(compiler, OP_MULTIPLY);
    return;
  case TOK_DIVIDE:
    emitOp(compiler, OP_DIVIDE);
    return;
  case TOK_GT:
    emitOp(compiler, OP_GT);
    return;
  case TOK_LT:
    emitOp(compiler, OP_LT);
    return;
  case TOK_GTE:
    emitOp(compiler, OP_GTE);
    return;
  case TOK_LTE:
    emitOp(compiler, OP_LTE);
    return;
  case TOK_EQ:
    emitOp(compiler, OP_EQ);
    return;
  case TOK_NEQ:
    emitOp(compiler, OP_NEQ);
    return;
  default:
    error(compiler, "Invalid operator %s", rule->name);
    return;
  }
}

// Compiling ------------------------------------------------------------------

ObjFunction* endCompiler(Compiler* compiler, const char* debugName,
                         int debugNameLength) {
  if (compiler->parser->hasError) {
    return NULL;
  }

  if (compiler->parent == NULL) {
    emitOp(compiler, OP_END_MODULE);
  } else {
    // Make sure we don't leave the parent compiler's parser "behind".

    // TODO(kendal): Consider just keeping a stack of compilers on the VM,
    // which would also prevent us from having to fixup the VM's ip and frame
    // before executing the compiled code.
    compiler->function->name =
        copyString(compiler->vm, debugName, debugNameLength);
    compiler->parent->parser = compiler->parser;

    emitOp(compiler, OP_RETURN);
  }

  // There is always an OP_END_MODULE or OP_RETURN instruction before this.
  // It is only reached when the module we just compiled is not the "main"
  // module.
  emitOp(compiler, OP_EXIT);
  return compiler->function;
}

ObjFunction* compile(ObaVM* vm, ObjModule* module, const char* source,
                     Compiler* parent, const char* name, int nameLength) {
  // Skip the UTF-8 BOM if there is one.
  if (strncmp(source, "\xEF\xBB\xBF", 3) == 0)
    source += 3;

  Parser parser;
  parser.module = module;
  parser.source = source;
  parser.tokenStart = source;
  parser.currentChar = source;
  parser.currentLine = 1;
  parser.current.type = TOK_ERROR;
  parser.current.start = source;
  parser.current.length = 0;
  parser.current.line = 0;
  parser.hasError = false;
  parser.isInterpolating = false;

  Compiler compiler;
  initCompiler(vm, &compiler, &parser, parent);

  nextToken(&compiler);
  ignoreNewlines(&compiler);

  while (!match(&compiler, TOK_EOF)) {
    declaration(&compiler);
    // If no newline, the file must end on this line.
    if (!matchLine(&compiler)) {
      consume(&compiler, TOK_EOF, "Expected end of file.");
      break;
    }
  }

  return endCompiler(&compiler, name, nameLength);
}

ObjFunction* obaCompile(ObaVM* vm, ObjModule* module, const char* source) {
  return compile(vm, module, source, NULL, "(script)", 8);
}
