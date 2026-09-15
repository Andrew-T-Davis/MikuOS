#include "grammar.h"
#include "asm.h"
#include "int.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char* rwszName;
    U64 rwullType;
} Keyword;

static Keyword g_rwaKeywords[] = {
    {"U8", TOK_KW_U8}, {"S8", TOK_KW_S8},
    {"U16", TOK_KW_U16}, {"S16", TOK_KW_S16},
    {"U32", TOK_KW_U32}, {"S32", TOK_KW_S32},
    {"U64", TOK_KW_U64}, {"S64", TOK_KW_S64},
    {"U0", TOK_KW_U0},
    {"If", TOK_KW_IF}, {"Else", TOK_KW_ELSE}, {"While", TOK_KW_WHILE},
    {"Func", TOK_KW_FUNC}, {"Return", TOK_KW_RETURN},
    {"Break", TOK_KW_BREAK}, {"Continue", TOK_KW_CONTINUE}, {"Goto", TOK_KW_GOTO},
    {"Struct", TOK_KW_STRUCT}, {"SizeOf", TOK_KW_SIZEOF}, {"Union", TOK_KW_UNION},
};

static U64 KeywordLookup(const char* rwszName) {
    for (U64 i = 0; i < sizeof(g_rwaKeywords) / sizeof(Keyword); i++) {
        if (strcmp(g_rwaKeywords[i].rwszName, rwszName) == 0)
            return g_rwaKeywords[i].rwullType;
    }
    return 0;
}

static Token* TokenNew(void) {
    Token* rwptNode = malloc(sizeof(Token));
    memset(rwptNode, 0, sizeof(Token));
    return rwptNode;
}

static void TokenAppend(TokenRoot* rwptrRoot, Token* rwptNode) {
    if (!rwptrRoot->rwptHead) {
        rwptrRoot->rwptHead = rwptNode;
        rwptrRoot->rwptTail = rwptNode;
    } else {
        rwptNode->rwptPre = rwptrRoot->rwptTail;
        rwptrRoot->rwptTail->rwptNext = rwptNode;
        rwptrRoot->rwptTail = rwptNode;
    }
}

static void SkipSpace(char** rwppszCode) {
    while (**rwppszCode == ' ' || **rwppszCode == '\t' ||
           **rwppszCode == '\n' || **rwppszCode == '\r') {
        (*rwppszCode)++;
    }
}

static void SkipLineComment(char** rwppszCode) {
    while (**rwppszCode && **rwppszCode != '\n') (*rwppszCode)++;
}

static void SkipBlockComment(char** rwppszCode) {
    (*rwppszCode) += 2;
    while (**rwppszCode && !(**rwppszCode == '*' && *(*rwppszCode + 1) == '/'))
        (*rwppszCode)++;
    if (**rwppszCode) (*rwppszCode) += 2;
}

static U64 LexNumber(char** rwppszCode) {
    U64 rwullValue = 0;
    if (**rwppszCode == '0' && (*(*rwppszCode + 1) == 'x' || *(*rwppszCode + 1) == 'X')) {
        (*rwppszCode) += 2;
        while (isxdigit((unsigned char)**rwppszCode)) {
            char rwszC = **rwppszCode;
            U64 rwullD = (rwszC >= '0' && rwszC <= '9') ? rwszC - '0' :
                         (rwszC >= 'a' && rwszC <= 'f') ? rwszC - 'a' + 10 : rwszC - 'A' + 10;
            rwullValue = rwullValue * 16 + rwullD;
            (*rwppszCode)++;
        }
        return rwullValue;
    }
    while (**rwppszCode >= '0' && **rwppszCode <= '9') {
        rwullValue = rwullValue * 10 + (**rwppszCode - '0');
        (*rwppszCode)++;
    }
    return rwullValue;
}

static char* LexIdent(char** rwppszCode) {
    char* rwszStart = *rwppszCode;
    while (isalnum((unsigned char)**rwppszCode) || **rwppszCode == '_')
        (*rwppszCode)++;
    U64 rwullLen = (U64)(*rwppszCode - rwszStart);
    char* rwszName = malloc(rwullLen + 1);
    memcpy(rwszName, rwszStart, rwullLen);
    rwszName[rwullLen] = '\0';
    return rwszName;
}

static char* LexString(char** rwppszCode) {
    (*rwppszCode)++;
    char* rwszStart = *rwppszCode;
    while (**rwppszCode && **rwppszCode != '"') {
        if (**rwppszCode == '\\' && *(*rwppszCode + 1)) (*rwppszCode)++;
        (*rwppszCode)++;
    }
    U64 rwullLen = (U64)(*rwppszCode - rwszStart);
    char* rwszStr = malloc(rwullLen + 1);
    memcpy(rwszStr, rwszStart, rwullLen);
    rwszStr[rwullLen] = '\0';
    if (**rwppszCode == '"') (*rwppszCode)++;
    return rwszStr;
}

TokenRoot* CodeToToken(char* rwszCode) {
    if (!rwszCode || !*rwszCode) return NULL;

    TokenRoot* rwptrRoot = malloc(sizeof(TokenRoot));
    rwptrRoot->rwptHead = NULL;
    rwptrRoot->rwptTail = NULL;

    char* rwszCur = rwszCode;

    while (*rwszCur) {
        SkipSpace(&rwszCur);
        if (!*rwszCur) break;

        if (rwszCur[0] == '/' && rwszCur[1] == '/') { SkipLineComment(&rwszCur); continue; }
        if (rwszCur[0] == '/' && rwszCur[1] == '*') { SkipBlockComment(&rwszCur); continue; }

        Token* rwptNode = TokenNew();

        if (*rwszCur >= '0' && *rwszCur <= '9') {
            rwptNode->rwullType = TOK_NUM;
            rwptNode->rwullValue = LexNumber(&rwszCur);
        } else if (*rwszCur == '"') {
            rwptNode->rwullType = TOK_STR;
            rwptNode->rwszName = LexString(&rwszCur);
        } else if (isalpha((unsigned char)*rwszCur) || *rwszCur == '_') {
            rwptNode->rwszName = LexIdent(&rwszCur);
            U64 rwullKw = KeywordLookup(rwptNode->rwszName);
            if (rwullKw) {
                rwptNode->rwullType = rwullKw;
                free(rwptNode->rwszName);
                rwptNode->rwszName = NULL;
            } else {
                U64 rwullAsmOp = AsmOpLookup(rwptNode->rwszName);
                if (rwullAsmOp) {
                    rwptNode->rwullType = TOK_ASM_OP;
                    rwptNode->rwullAsmOp = rwullAsmOp;
                    free(rwptNode->rwszName);
                    rwptNode->rwszName = NULL;
                } else {
                    U64 rwullAsmReg = AsmRegLookup(rwptNode->rwszName);
                    if (rwullAsmReg) {
                        rwptNode->rwullType = TOK_ASM_REG;
                        rwptNode->rwullAsmReg = rwullAsmReg;
                        free(rwptNode->rwszName);
                        rwptNode->rwszName = NULL;
                    } else {
                        rwptNode->rwullType = TOK_IDENT;
                    }
                }
            }
        }
        else if (rwszCur[0] == '=' && rwszCur[1] == '=') { rwptNode->rwullType = TOK_EQ; rwszCur += 2; }
        else if (rwszCur[0] == '!' && rwszCur[1] == '=') { rwptNode->rwullType = TOK_NEQ; rwszCur += 2; }
        else if (rwszCur[0] == '<' && rwszCur[1] == '=') { rwptNode->rwullType = TOK_LE; rwszCur += 2; }
        else if (rwszCur[0] == '>' && rwszCur[1] == '=') { rwptNode->rwullType = TOK_GE; rwszCur += 2; }
        else if (rwszCur[0] == '&' && rwszCur[1] == '&') { rwptNode->rwullType = TOK_AND; rwszCur += 2; }
        else if (rwszCur[0] == '|' && rwszCur[1] == '|') { rwptNode->rwullType = TOK_OR; rwszCur += 2; }
        else if (rwszCur[0] == '-' && rwszCur[1] == '>') { rwptNode->rwullType = TOK_ARROW; rwszCur += 2; }
        else if (*rwszCur == '<') { rwptNode->rwullType = TOK_LT; rwszCur++; }
        else if (*rwszCur == '>') { rwptNode->rwullType = TOK_GT; rwszCur++; }
        else if (*rwszCur == '!') { rwptNode->rwullType = TOK_NOT; rwszCur++; }
        else if (*rwszCur == '&') { rwptNode->rwullType = TOK_AMP; rwszCur++; }
        else if (*rwszCur == '+') { rwptNode->rwullType = TOK_PLUS; rwszCur++; }
        else if (*rwszCur == '-') { rwptNode->rwullType = TOK_MINUS; rwszCur++; }
        else if (*rwszCur == '*') { rwptNode->rwullType = TOK_STAR; rwszCur++; }
        else if (*rwszCur == '/') { rwptNode->rwullType = TOK_DIV; rwszCur++; }
        else if (*rwszCur == '%') { rwptNode->rwullType = TOK_MOD; rwszCur++; }
        else if (*rwszCur == '^') { rwptNode->rwullType = TOK_POW; rwszCur++; }
        else if (*rwszCur == '(') { rwptNode->rwullType = TOK_LPAREN; rwszCur++; }
        else if (*rwszCur == ')') { rwptNode->rwullType = TOK_RPAREN; rwszCur++; }
        else if (*rwszCur == '{') { rwptNode->rwullType = TOK_LBRACE; rwszCur++; }
        else if (*rwszCur == '}') { rwptNode->rwullType = TOK_RBRACE; rwszCur++; }
        else if (*rwszCur == '[') { rwptNode->rwullType = TOK_LBRACKET; rwszCur++; }
        else if (*rwszCur == ']') { rwptNode->rwullType = TOK_RBRACKET; rwszCur++; }
        else if (*rwszCur == ',') { rwptNode->rwullType = TOK_COMMA; rwszCur++; }
        else if (*rwszCur == ';') { rwptNode->rwullType = TOK_SEMI; rwszCur++; }
        else if (*rwszCur == ':') { rwptNode->rwullType = TOK_COLON; rwszCur++; }
        else if (*rwszCur == '.') { rwptNode->rwullType = TOK_DOT; rwszCur++; }
        else if (*rwszCur == '=') { rwptNode->rwullType = TOK_ASSIGN; rwszCur++; }
        else { free(rwptNode); rwszCur++; continue; }

        TokenAppend(rwptrRoot, rwptNode);
    }

    Token* rwptEnd = TokenNew();
    rwptEnd->rwullType = TOK_EOF;
    TokenAppend(rwptrRoot, rwptEnd);
    return rwptrRoot;
}

void TokenRootFree(TokenRoot* rwptrRoot) {
    if (!rwptrRoot) return;
    Token* rwptNode = rwptrRoot->rwptHead;
    while (rwptNode) {
        Token* rwptNext = rwptNode->rwptNext;
        if (rwptNode->rwszName) free(rwptNode->rwszName);
        free(rwptNode);
        rwptNode = rwptNext;
    }
    free(rwptrRoot);
}
