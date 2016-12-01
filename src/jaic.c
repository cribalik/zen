#define DEBUG

#include "terminal.c"
#include "memarena.c"
#include "utils.c"
#include "array.c"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef enum {
  TOK_EOF = -1,
  TOK_IDENTIFIER = -3,
  TOK_FLOAT = -4,
  TOK_INT = -5,
  TOK_UNKNOWN = -6,
  TOK_ARROW = -7,
} Token;


// values set by the tokenizer
static char* identifierStr;
static double floatVal;
static int intVal;
static int line;
static int column;

// the input source file
static FILE* file;
static char* filename;

static char getChar(FILE* file) {
  char c = getc(file);
  ++column;
  if (c == '\n') {
    ++line;
    column = 0;
  }
  return c;
}

static int curr_char = ' ';
static int gettok() {
  #define BUFFER_SIZE 512
  static char buf[BUFFER_SIZE];

  while (isspace(curr_char)) {
    curr_char = getChar(file);
    }

  // check for comments
  while (curr_char == '/') {
    curr_char = getChar(file);
    if (curr_char == '/') {
      do {
        curr_char = getChar(file);
      } while (curr_char != EOF && curr_char != '\n' && curr_char != '\r');
      if (curr_char == EOF) {
        return TOK_EOF;
      }
      goto endofcomment;
    }
    else if (curr_char == '*') {
      int depth = 1;
      curr_char = getChar(file);
      while (1) {
        if (curr_char == '*') {
          curr_char = getChar(file);
          if (curr_char == '/') {
            --depth;
            if (depth == 0) {
              curr_char = getChar(file);
              goto endofcomment;
            }
          }
        }
        else if (curr_char == '/') {
          curr_char = getChar(file);
          if (curr_char == '*') {
            ++depth;
          }
        } else if (curr_char == EOF) {
          printf("File ended while still in block comment\n");
          return TOK_UNKNOWN;
        }
        curr_char = getChar(file);
      }
    }
    else {
      return '/';
    }
    endofcomment:
    while (isspace(curr_char)) {curr_char = getChar(file);};
  }

  if (isalpha(curr_char)) {
    int i = 0;
    for (i = 0; i < BUFFER_SIZE && (isalpha(curr_char) || isdigit(curr_char)); ++i) {
      buf[i] = curr_char;
      curr_char = getChar(file);
    }
    if (i == BUFFER_SIZE) {
      printf("Reached identifier size limit\n");
      return TOK_UNKNOWN;
    }
    else {
      buf[i] = '\0';
      identifierStr = buf;
      return TOK_IDENTIFIER;
    }
  }

  else if (isdigit(curr_char)) {
    int i;
    for (i = 0; i < BUFFER_SIZE && isdigit(curr_char); ++i) {
      buf[i] = curr_char;
      curr_char = getChar(file);
    }

    if (i == BUFFER_SIZE) {
      logError("Reached max size for int\n");
    }
    else if (curr_char == '.') {
      curr_char = getChar(file);
      for (; i < BUFFER_SIZE && isdigit(curr_char); ++i) {
        buf[i] = curr_char;
        curr_char = getChar(file);
      }

      if (i == BUFFER_SIZE) {
        logError("Reached max size for float number\n");
      }
      // TODO: check for suffixes here e.g. 30.0f
      else {
        buf[i] = '\0';
        floatVal = strtod(buf, NULL);
        return TOK_FLOAT;
      }
    }
    // TODO: check for suffixes here e.g. 10u32
    else {
      buf[i] = '\0';
      intVal = atoi(buf);
      return TOK_INT;
    }
  }

  else if (curr_char == EOF) {
    return TOK_EOF;
  }

  else if (curr_char == '-') {
    curr_char = getChar(file);
    if (curr_char == '>') {
      curr_char = getChar(file);
      return TOK_ARROW;
    } else {
      return '-';
    }
  }

  else {
    int r = curr_char;
    curr_char = getChar(file);
    return r;
  }

  UNREACHABLE;
  #undef BUFFER_SIZE
}

static int token;
static char* print_token() {
  static char buffer[2];
  switch (token) {
    case TOK_FLOAT:
      return "<float>";
    case TOK_INT:
      return "<int>";
    case TOK_UNKNOWN:
      return "<unknown>";
    case TOK_IDENTIFIER:
      return identifierStr;
    case TOK_EOF:
      return "<eof>";
    case TOK_ARROW:
      return "->";
    default:
      buffer[0] = token;
      return buffer;
  }
  UNREACHABLE;
}

static void eatToken() {
  token = gettok();
  // printf("%s\n", print_token());
}

typedef enum {
  FLOAT_AST,
  INT_AST,
  CALL_AST,
  VARIABLE_AST,
} ExpressionType;

typedef struct {
  ExpressionType expr_type;
} ExpressionAST;

/*
typedef struct {
  char* name;
  char* type;
} ArgumentAST;
*/

typedef enum {
  STRUCT,
  INT,
  FLOAT,
} Type;

typedef struct {
  char* name;
  Type type;
} TypeAST;

typedef struct {
  char* name;
  int num_args;
  char* args; // continuous strings of name1,type1, name2,type2, ...
  TypeAST* return_type;
  char* return_type_name;
  // TODO: body??
} FunctionDeclarationAST;

typedef struct {
  char* name;
  char* type_name;
  TypeAST* type;
  ExpressionAST* value;
} VariableDeclarationAST;

/* Expressions */

typedef struct {
  ExpressionAST parent;
  char* name;
} VariableGetAST;

typedef struct {
  ExpressionAST parent;
  float value;
} FloatAST;

typedef struct {
  ExpressionAST parent;
  int value;
} IntAST;

typedef struct {
  ExpressionAST parent;
  char* name;
  int num_input;
  ExpressionAST** input;
} CallAST;

static int test() {
  arrayTest();
  return 0;
}

#define MAIN_MEMORY_SIZE 64*1024*1024

static MemArena perm_arena;

static Array /*VariableDeclarationAST* */ variables;
void addVariableDeclaration(VariableDeclarationAST* ast) {
  VariableDeclarationAST** slot = arrayPush(&variables);
  ZERO(slot);
  *slot = ast;
}
VariableDeclarationAST* getVariableDeclaration(char* name) {
  VariableDeclarationAST** it = (VariableDeclarationAST**) arrayBegin(&variables);
  for (int i = 0, len = arrayCount(&variables); i < len; ++i, ++it) {
    if (strcmp(name, (*it)->name) == 0) {
      return *it;
    }
  }
  return 0;
}

static Array /* FunctionDeclarationAST* */ functions;
void addFunctionDeclaration(FunctionDeclarationAST* ast) {
  FunctionDeclarationAST** slot = (FunctionDeclarationAST**) arrayPush(&functions);
  *slot = ast;
}
FunctionDeclarationAST* getFunctionDeclaration(char* name) {
  FunctionDeclarationAST** it = (FunctionDeclarationAST**) arrayBegin(&functions);
  for (int i = 0, len = arrayCount(&functions); i < len; ++i, ++it) {
    if (strcmp(name, (*it)->name) == 0) {
      return *it;
    }
  }
  return 0;
}

static char* pushString(MemArena* arena, char* str) {
  int len = strlen(str) + 1;
  char* result = arenaPush(arena, len);
  memcpy(result, str, len);
  return result;
}

/* (a,b) a+b) */
static FunctionDeclarationAST* parseFunctionDeclaration(bool try) {
  void* start = arenaGetCurrent(perm_arena);
  FunctionDeclarationAST* result = arenaPush(&perm_arena, sizeof(FunctionDeclarationAST));
  if (token != '(') {
    if (!try) {
      logError("token %s not start of a function prototype\n", print_token(token));
    }
    arenaPopTo(&perm_arena, start);
    return 0;
  }

  // arguments
  int num_args = 0;
  result->args = arenaGetCurrent(perm_arena);
  eatToken();
  while (1) {

    if (token == ')') {
      eatToken();
      break;
    }

    ++num_args;

    if (token != TOK_IDENTIFIER) {
      if (!try) {
        logError("Identifier expected in parameter list, found %c\n", token);
      }
      arenaPopTo(&perm_arena, start);
      return 0;
    }

    char* name = pushString(&perm_arena, identifierStr);
    eatToken();
    logDebugInfo("Found parameter %s\n", name);

    if (token != ':') {
      if (!try) {
        logError("No type found after parameter name (did you forget a ':'?\n");
      }
      arenaPopTo(&perm_arena, start);
      return 0;
    }
    eatToken();

    if (token != TOK_IDENTIFIER) {
      if (!try) {
        logError("Typename expected after ':'\n");
      }
      arenaPopTo(&perm_arena, start);
      return 0;
    }
    char* type = pushString(&perm_arena, identifierStr);
    eatToken();
    logDebugInfo(" - with type %s\n", type);

    // TODO: default parameters

    if (token == ',') {
      eatToken();
    }
    else if (token == ')') {
      eatToken();
      break;
    }
    else {
      logDebugInfo("Expected ',' or ')' after parameter\n");
      arenaPopTo(&perm_arena, start);
      return 0;
    }
  }
  result->num_args = num_args;

  // return value
  if (token != TOK_ARROW) {
    logError("No return value specified (did you forget '->'?)");
    logDebugInfo("no return value after parameters");
    arenaPopTo(&perm_arena, start);
    return 0;
  }
  eatToken();
  if (token != TOK_IDENTIFIER) {
    logError("Expected return type, got %s\n", print_token());
    logDebugInfo("no type identifier after arrow");
    arenaPopTo(&perm_arena, start);
    return 0;
  }
  result->return_type_name = pushString(&perm_arena, identifierStr);
  eatToken();

  if (token != '{') {
    logError("No '{' found after function parameter list\n");
    arenaPopTo(&perm_arena, start);
    return 0;
  }
  eatToken();

  int depth = 1;
  while (depth > 0) {
    if (token == '{') ++depth;
    else if (token == '}') --depth;
    else if (token == TOK_EOF) {
      if (!try) {
        logError("'{' never matched with a '}'");
      }
      exit(1);
    }
    eatToken();
  }

  return result;
}

static ExpressionAST* parseExpression() {
  switch (token) {
    case TOK_FLOAT: {
      FloatAST* ast = arenaPush(&perm_arena, sizeof(FloatAST));
      ast->parent.expr_type = FLOAT_AST;
      ast->value = floatVal;
      eatToken();
      logDebugInfo("Found a float literal with value %f\n", ast->value);
      return &ast->parent;
    } break;

    case TOK_INT: {
      IntAST* ast = arenaPush(&perm_arena, sizeof(IntAST));
      ast->parent.expr_type = INT_AST;
      ast->value = intVal;
      eatToken();
      logDebugInfo("Found an int literal with value %i\n", ast->value);
      return &ast->parent;
    } break;

    case TOK_IDENTIFIER: {
      byte* start = arenaGetCurrent(perm_arena);
      char* name = pushString(&perm_arena, identifierStr);
      eatToken();
      if (token == '(') {

        eatToken();
        Array input;
        arrayInit(&input, 0, sizeof(ExpressionAST*));
        ExpressionAST* expr = 0;

        // get input arguments
        while (1) {
          if (token == ')') {
            eatToken();
            break;
          }

          expr = parseExpression();
          if (!expr) {
            logError("Invalid expression inside function call to '%s'\n", name);
            arenaPopTo(&perm_arena, start);
            arrayFree(&input);
            return 0;
          }

          arrayPushVal(&input, &expr);

          if (token == ')') {
            eatToken();
            break;
          }
          if (token == ',') {
            eatToken();
          } else {
            logError("Expected ',' between function input parameters\n");
          }
        }

        // push to permanent storage
        CallAST* call = arenaPush(&perm_arena, sizeof(CallAST));
        call->parent.expr_type = CALL_AST;
        call->name = name;
        call->num_input = arrayCount(&input);
        if (call->num_input > 0) {
          ExpressionAST** dest = arenaPush(&perm_arena, sizeof(ExpressionAST*) * call->num_input);
          memcpy(dest, arrayBegin(&input), sizeof(ExpressionAST*) & call->num_input);
          call->input = dest;
        }
        arrayFree(&input);

        logDebugInfo("Found function call to '%s' with %i inputs\n", call->name, call->num_input);
        return &call->parent;
      }
      else {
        VariableGetAST* ast = arenaPush(&perm_arena, sizeof(VariableGetAST));
        ast->parent.expr_type = VARIABLE_AST;
        ast->name = name;
        logDebugInfo("Found a variable get with name %s\n", name);
        return &ast->parent;
      }
    } break;

    case '(': {
      eatToken();
      ExpressionAST* result = parseExpression();
      if (!result || token != ')') {
        return 0;
      }
      eatToken();
      return result;
    } break;

    default: {
      logError("Expression expected, but found neither a literal, function call or a variable name, instead found %c\n", token);
      return 0;
    } break;
  }
  return 0;
}

#define pushPosition fpos_t _position; int _line = line; int _column = column; fgetpos(file, &_position); logDebugInfo("Pushing %s:%i:%i\n", filename, line+1, column);
#define popPosition fsetpos(file, &_position); curr_char = ' '; line = _line; column = _column; logDebugInfo("Jumping back to %s:%i:%i\n", filename, line+1, column);

TypeAST* evaluateTypeFromName(char* name) {
  logDebugInfo("Trying to evaluate type %s...\n", name);
  TypeAST* result = arenaPush(&perm_arena, sizeof(TypeAST));
  if (strcmp(name, "int") == 0) {
    result->type = INT;
    result->name = "int";
    logDebugInfo("Found int\n");
    return result;
  }
  if (strcmp(name, "float") == 0) {
    result->type = FLOAT;
    result->name = "float";
    logDebugInfo("Found float\n");
    return result;
  }

  arenaPop(&perm_arena, sizeof(TypeAST));
  logError("Unknown type %s\n", name);
  return 0;
}

TypeAST* evaluateTypeOfExpression(ExpressionAST*);
TypeAST* evaluateTypeOfVariable(VariableDeclarationAST* var) {
  if (!var) return 0;
  if (!var->type) {
    TypeAST* declared_type = 0;
    TypeAST* inferred_type = 0;

    if (var->type_name) {
      declared_type = evaluateTypeFromName(var->type_name);
      if (!declared_type) {
        logError("Declared type '%s' for '%s' doesn't exist\n", var->type_name, var->name);
        var->type = 0;
        return 0;
      }
      var->type = declared_type;
    }

    if (var->value) {
      inferred_type = evaluateTypeOfExpression(var->value);
      if (!inferred_type) {
        logError("Couldn't infer type for %s\n", var->name);
        var->type = 0;
        return 0;
      }
      var->type = inferred_type;
    }
    // TODO: if both declared and inferred, garbage collect one of them

    if (inferred_type && declared_type && inferred_type->name != declared_type->name) {
      logError("Inferred type of '%s': '%s' differs from declared type '%s'\n", var->name, inferred_type->name, declared_type->name);
      var->type = 0;
      return 0;
    }

    logDebugInfo("Successfully inferred that '%s' has type '%s'\n", var->name, var->type->name);
    return var->type;
  }
  return var->type;
}

void evaluateTypeOfFunction(FunctionDeclarationAST* fun) {
  if (fun->return_type) {
    return;
  }

  fun->return_type = evaluateTypeFromName(fun->return_type_name);
}

TypeAST* evaluateTypeOfExpression(ExpressionAST* expr) {
  TypeAST* result = 0;
  switch (expr->expr_type) {

    case FLOAT_AST: {
      result = arenaPush(&perm_arena, sizeof(TypeAST));
      result->type = FLOAT;
      result->name = "float";
      return result;
    } break;

    case INT_AST: {
      result = arenaPush(&perm_arena, sizeof(TypeAST));
      result->type = INT;
      result->name = "int";
      return result;
    } break;

    case CALL_AST: {
      CallAST* call = (CallAST*) expr;
      FunctionDeclarationAST* fun = getFunctionDeclaration(call->name);
      if (!fun) {
        return 0;
      }

      if (!fun->return_type && fun->return_type_name) {
        evaluateTypeOfFunction(fun);
        return fun->return_type;
      }

      // TODO: check if the argument types are right
      return fun->return_type;
    } break;

    case VARIABLE_AST: {
      VariableGetAST* var_ref = (VariableGetAST*) expr;
      VariableDeclarationAST* var = getVariableDeclaration(var_ref->name);
      if (!var) {
        logError("Use of undeclared variable '%s'", var_ref->name);
        return 0;
      }

      return evaluateTypeOfVariable(var);
    } break;
  }
  return 0;
}

int main(int argc, char const *argv[]) {
  #ifdef DEBUG
    test();
  #endif
  (void) arenaPop;

  if (argc == 1) {
    logError("Usage: %s <filename>\n", argv[0]);
    return 1;
  }
  else {
    filename = (char*) argv[1];
    file = fopen(argv[1], "r");
    if (!file) {
      logError("Could not open file %s\n", argv[1]);
      return 1;
    }
  }

  arenaInit(&perm_arena);
  arrayInit(&variables, 16, sizeof(VariableDeclarationAST*));
  arrayInit(&functions, 16, sizeof(FunctionDeclarationAST*));

  eatToken();
  while (1) {
    switch (token) {

      case TOK_IDENTIFIER: {
        char* name = pushString(&perm_arena, identifierStr);
        char* type = 0;
        bool is_declaration = false;
        eatToken();

        if (token == ':') {
          is_declaration = true;
          eatToken();
          if (token == TOK_IDENTIFIER) {
            type = pushString(&perm_arena, identifierStr);
            eatToken();
          }
        }

        if (token == '=') {
          pushPosition;
          eatToken();

          // TODO: switch function and expression checks
          // function?
          {
            FunctionDeclarationAST* fun = parseFunctionDeclaration(true);
            if (fun) {
              fun->name = name;
              if (type) {
                logError("Function declaration should not have type");
              }
              else if (!is_declaration) {
                logError("Cannot redefine a function. (did you forget to do ':=' instead of just '='?)");
              }
              // TODO: check if identifier already exists
              else {
                logDebugInfo("Found a function definition with name %s, and %i arguments\n", fun->name, fun->num_args);
                addFunctionDeclaration(fun);
              }
              goto next;
            }
          }

          // expression?
          popPosition;
          eatToken();
          {
            ExpressionAST* value = parseExpression();
            if (value) {
              VariableDeclarationAST* var = arenaPush(&perm_arena, sizeof(VariableDeclarationAST));
              var->name = name;
              var->type_name = type ? type : 0;
              var->value = value;
              var->type = 0;
              logDebugInfo("Found a variable declaration for %s with type %s and a value of type %i\n", var->name, var->type_name, var->value->expr_type);
              addVariableDeclaration(var);
              goto next;
            }
          }

          logError("Neither expression or function prototype found after declaration\n");
          goto next;
        }

        // declaration without value?
        if (is_declaration) {
          VariableDeclarationAST* var = arenaPush(&perm_arena, sizeof(VariableDeclarationAST));
          ZERO(var);
          var->name = name;
          var->type_name = type;
          logDebugInfo("Found a variable declaration for %s with type %s and no value\n", var->name, var->type_name);
          addVariableDeclaration(var);
          // TODO: default initialization here!
        }

        else {
          logError("Unexpected token after ':' (variable declarations are not implemented yet - did you forget the '=' afterwards?)\n");
        }

      } break;

      case ';': {
        eatToken();
      } break;

      case TOK_EOF: {
        goto type_pass;
      } break;

      default: {
        logError("Unexpected token %c\n", token);
        return 0;
      } break;
    }
    next:;
  }
  type_pass:;

  {
    VariableDeclarationAST** it = (VariableDeclarationAST**) arrayBegin(&variables);
    for (int i = 0, len = arrayCount(&variables); i < len; ++i, ++it) {
      TypeAST* type = evaluateTypeOfVariable(*it);
      if (type) {
        logDebugInfo("Evaluated type of '%s' to '%s'\n", (*it)->name, type->name);
      }
    }
  }

  {
    FunctionDeclarationAST** it = (FunctionDeclarationAST**) arrayBegin(&functions);
    logDebugInfo("The found functions are:\n");
    for (int i = 0, len = arrayCount(&functions); i < len; ++i, ++it) {
      logDebugInfo(" - %s\n", (*it)->name);
    }
  }
  return 0;
}
