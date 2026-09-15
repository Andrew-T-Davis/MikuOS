#pragma once
#include "int.h"

typedef enum TokenType {
    TOK_EOF = 0,
    TOK_NUM,
    TOK_STR,
    TOK_PLUS, TOK_MINUS, TOK_MUL, TOK_DIV, TOK_MOD, TOK_POW,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_SEMI, TOK_COLON,
    TOK_ASSIGN,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_AMP, TOK_STAR,
    TOK_DOT, TOK_ARROW,
    TOK_IDENT,
    TOK_KW_U8, TOK_KW_S8, TOK_KW_U16, TOK_KW_S16,
    TOK_KW_U32, TOK_KW_S32, TOK_KW_U64, TOK_KW_S64, TOK_KW_U0,
    TOK_KW_IF, TOK_KW_ELSE, TOK_KW_WHILE,
    TOK_KW_FUNC, TOK_KW_RETURN,
    TOK_KW_BREAK, TOK_KW_CONTINUE, TOK_KW_GOTO,
    TOK_KW_STRUCT, TOK_KW_SIZEOF, TOK_KW_UNION,
    TOK_ASM_OP,
    TOK_ASM_REG
} TokenType;

typedef struct Token Token;
struct Token {
    U64 rwullType;
    U64 rwullValue;
    U64 rwullAsmOp;
    U64 rwullAsmReg;
    char* rwszName;
    Token* rwptNext;
    Token* rwptPre;
};

typedef struct TokenRoot {
    Token* rwptHead;
    Token* rwptTail;
} TokenRoot;

TokenRoot* CodeToToken(char* rwszCode);
void TokenRootFree(TokenRoot* rwptrRoot);
