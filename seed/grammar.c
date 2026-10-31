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
    {"Func", TOK_KW_FUNC}, {"Naked", TOK_KW_NAKED}, {"Return", TOK_KW_RETURN},
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

static void SkipSpace(char** rwpszCode) {
    while (**rwpszCode == ' ' || **rwpszCode == '\t' ||
           **rwpszCode == '\n' || **rwpszCode == '\r') {
        (*rwpszCode)++;
    }
}

static void SkipLineComment(char** rwpszCode) {
    while (**rwpszCode && **rwpszCode != '\n') (*rwpszCode)++;
}

static void SkipBlockComment(char** rwpszCode) {
    (*rwpszCode) += 2;
    while (**rwpszCode && !(**rwpszCode == '*' && *(*rwpszCode + 1) == '/'))
        (*rwpszCode)++;
    if (**rwpszCode) (*rwpszCode) += 2;
}

static U64 LexNumber(char** rwpszCode) {
    U64 rwullValue = 0;
    
    if (**rwpszCode == '0' && (*(*rwpszCode + 1) == 'x' || *(*rwpszCode + 1) == 'X')) {
        (*rwpszCode) += 2;
        while (isxdigit((unsigned char)**rwpszCode)) {
            char rwcC = **rwpszCode;
            U64 rwullD = (rwcC >= '0' && rwcC <= '9') ? rwcC - '0' :
                         (rwcC >= 'a' && rwcC <= 'f') ? rwcC - 'a' + 10 : rwcC - 'A' + 10;
            rwullValue = rwullValue * 16 + rwullD;
            (*rwpszCode)++;
        }
        return rwullValue;
    }

    if (**rwpszCode == '0' && (*(*rwpszCode + 1) == 'b' || *(*rwpszCode + 1) == 'B')) {
        (*rwpszCode) += 2;
        while (**rwpszCode == '0' || **rwpszCode == '1') {
            rwullValue = (rwullValue << 1) | (**rwpszCode - '0');
            (*rwpszCode)++;
        }
        return rwullValue;
    }

    if (**rwpszCode == '0' && (*(*rwpszCode + 1) == 't' || *(*rwpszCode + 1) == 'T')) {
        (*rwpszCode) += 2;
        while (**rwpszCode == '0' || **rwpszCode == '1' || **rwpszCode == 't' || **rwpszCode == 'T') {
            char rwcC = **rwpszCode;
            S64 rwsllDigit = 0;
            if (rwcC == '1') rwsllDigit = 1;
            else if (rwcC == 't' || rwcC == 'T') rwsllDigit = -1;
            
            rwullValue = rwullValue * 3 + rwsllDigit;
            (*rwpszCode)++;
        }
        return rwullValue;
    }

    while (**rwpszCode >= '0' && **rwpszCode <= '9') {
        rwullValue = rwullValue * 10 + (**rwpszCode - '0');
        (*rwpszCode)++;
    }
    return rwullValue;
}


static char* LexIdent(char** rwpszCode) {
    char* rwszStart = *rwpszCode;
    while (isalnum((unsigned char)**rwpszCode) || **rwpszCode == '_')
        (*rwpszCode)++;
    U64 rwullLen = (U64)(*rwpszCode - rwszStart);
    char* rwszName = malloc(rwullLen + 1);
    memcpy(rwszName, rwszStart, rwullLen);
    rwszName[rwullLen] = '\0';
    return rwszName;
}

static char* LexString(char** rwpszCode) {
    (*rwpszCode)++;
    char* rwszStart = *rwpszCode;
    while (**rwpszCode && **rwpszCode != '"') {
        if (**rwpszCode == '\\' && *(*rwpszCode + 1)) (*rwpszCode)++;
        (*rwpszCode)++;
    }
    U64 rwullLen = (U64)(*rwpszCode - rwszStart);
    char* rwszStr = malloc(rwullLen + 1);
    memcpy(rwszStr, rwszStart, rwullLen);
    rwszStr[rwullLen] = '\0';
    if (**rwpszCode == '"') (*rwpszCode)++;
    return rwszStr;
}

TokenRoot* CodeToToken(char* rwszCode) {
    if (!rwszCode || !*rwszCode) return NULL;

    TokenRoot* rwptrRoot = malloc(sizeof(TokenRoot));
    rwptrRoot->rwptHead = NULL;
    rwptrRoot->rwptTail = NULL;

    char* rwpcCur = rwszCode;

    while (*rwpcCur) {
        SkipSpace(&rwpcCur);
        if (!*rwpcCur) break;

        if (rwpcCur[0] == '/' && rwpcCur[1] == '/') { SkipLineComment(&rwpcCur); continue; }
        if (rwpcCur[0] == '/' && rwpcCur[1] == '*') { SkipBlockComment(&rwpcCur); continue; }

        Token* rwptNode = TokenNew();

        if (*rwpcCur >= '0' && *rwpcCur <= '9') {
            rwptNode->rwullType = TOK_NUM;
            rwptNode->rwullValue = LexNumber(&rwpcCur);
        } else if (*rwpcCur == '"') {
            rwptNode->rwullType = TOK_STR;
            rwptNode->rwszName = LexString(&rwpcCur);
        } else if (isalpha((unsigned char)*rwpcCur) || *rwpcCur == '_') {
            rwptNode->rwszName = LexIdent(&rwpcCur);
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
        else if (rwpcCur[0] == '=' && rwpcCur[1] == '=') { rwptNode->rwullType = TOK_EQ; rwpcCur += 2; }
        else if (rwpcCur[0] == '!' && rwpcCur[1] == '=') { rwptNode->rwullType = TOK_NEQ; rwpcCur += 2; }
        else if (rwpcCur[0] == '<' && rwpcCur[1] == '=') { rwptNode->rwullType = TOK_LE; rwpcCur += 2; }
        else if (rwpcCur[0] == '>' && rwpcCur[1] == '=') { rwptNode->rwullType = TOK_GE; rwpcCur += 2; }
        else if (rwpcCur[0] == '&' && rwpcCur[1] == '&') { rwptNode->rwullType = TOK_AND; rwpcCur += 2; }
        else if (rwpcCur[0] == '|' && rwpcCur[1] == '|') { rwptNode->rwullType = TOK_OR; rwpcCur += 2; }
        else if (rwpcCur[0] == '-' && rwpcCur[1] == '>') { rwptNode->rwullType = TOK_ARROW; rwpcCur += 2; }
        else if (rwpcCur[0] == '<' && rwpcCur[1] == '<') { rwptNode->rwullType = TOK_SHL; rwpcCur += 2; }
        else if (rwpcCur[0] == '>' && rwpcCur[1] == '>') { rwptNode->rwullType = TOK_SHR; rwpcCur += 2; }
        else if (*rwpcCur == '<') { rwptNode->rwullType = TOK_LT; rwpcCur++; }
        else if (*rwpcCur == '>') { rwptNode->rwullType = TOK_GT; rwpcCur++; }
        else if (*rwpcCur == '!') { rwptNode->rwullType = TOK_NOT; rwpcCur++; }
        else if (*rwpcCur == '&') { rwptNode->rwullType = TOK_AMP; rwpcCur++; }
        else if (*rwpcCur == '+') { rwptNode->rwullType = TOK_PLUS; rwpcCur++; }
        else if (*rwpcCur == '-') { rwptNode->rwullType = TOK_MINUS; rwpcCur++; }
        else if (rwpcCur[0] == '*' && rwpcCur[1] == '*') { rwptNode->rwullType = TOK_POW; rwpcCur += 2; }
        else if (*rwpcCur == '*') { rwptNode->rwullType = TOK_STAR; rwpcCur++; }
        else if (*rwpcCur == '/') { rwptNode->rwullType = TOK_DIV; rwpcCur++; }
        else if (*rwpcCur == '%') { rwptNode->rwullType = TOK_MOD; rwpcCur++; }
        else if (*rwpcCur == '^') { rwptNode->rwullType = TOK_BITXOR; rwpcCur++; }
        else if (*rwpcCur == '(') { rwptNode->rwullType = TOK_LPAREN; rwpcCur++; }
        else if (*rwpcCur == ')') { rwptNode->rwullType = TOK_RPAREN; rwpcCur++; }
        else if (*rwpcCur == '{') { rwptNode->rwullType = TOK_LBRACE; rwpcCur++; }
        else if (*rwpcCur == '}') { rwptNode->rwullType = TOK_RBRACE; rwpcCur++; }
        else if (*rwpcCur == '[') { rwptNode->rwullType = TOK_LBRACKET; rwpcCur++; }
        else if (*rwpcCur == ']') { rwptNode->rwullType = TOK_RBRACKET; rwpcCur++; }
        else if (*rwpcCur == ',') { rwptNode->rwullType = TOK_COMMA; rwpcCur++; }
        else if (*rwpcCur == ';') { rwptNode->rwullType = TOK_SEMI; rwpcCur++; }
        else if (*rwpcCur == ':') { rwptNode->rwullType = TOK_COLON; rwpcCur++; }
        else if (*rwpcCur == '.') { rwptNode->rwullType = TOK_DOT; rwpcCur++; }
        else if (*rwpcCur == '=') { rwptNode->rwullType = TOK_ASSIGN; rwpcCur++; }
        else { free(rwptNode); rwpcCur++; continue; }

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
