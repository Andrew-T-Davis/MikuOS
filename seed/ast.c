#include "ast.h"
#include "asm.h"
#include "int.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Token* rwptCur;

static Node* NodeNew(U64 rwullType) {
    Node* rwpnNode = malloc(sizeof(Node));
    memset(rwpnNode, 0, sizeof(Node));
    rwpnNode->rwullType = rwullType;
    return rwpnNode;
}

static Node* ParseExpr(void);
static Node* ParseStmt(void);
static Node* ParseBlock(void);
static Node* ParseAggregateDef(U64 rwullNodeType);

static Node* ParseFactor(void) {
    if (!rwptCur) return NULL;

    if (rwptCur->rwullType == TOK_NUM) {
        Node* rwpnNode = NodeNew(NODE_NUM);
        rwpnNode->rwullValue = rwptCur->rwullValue;
        rwptCur = rwptCur->rwptNext;
        return rwpnNode;
    }

    if (rwptCur->rwullType == TOK_STR) {
        Node* rwpnNode = NodeNew(NODE_STR);
        rwpnNode->rwszName = strdup(rwptCur->rwszName);
        rwptCur = rwptCur->rwptNext;
        return rwpnNode;
    }

    if (rwptCur->rwullType == TOK_KW_SIZEOF) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnNode = NodeNew(NODE_SIZEOF);
        if (rwptCur && rwptCur->rwullType == TOK_LPAREN) {
            rwptCur = rwptCur->rwptNext;
            if (rwptCur && rwptCur->rwullType == TOK_IDENT) {
                rwpnNode->rwszName = strdup(rwptCur->rwszName);
                rwptCur = rwptCur->rwptNext;
            }
            if (rwptCur && rwptCur->rwullType == TOK_RPAREN) rwptCur = rwptCur->rwptNext;
        }
        return rwpnNode;
    }

    if (rwptCur->rwullType == TOK_AMP) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnNode = NodeNew(NODE_ADDR);
        rwpnNode->rwpnLeft = ParseFactor();
        return rwpnNode;
    }

    if (rwptCur->rwullType == TOK_STAR) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnNode = NodeNew(NODE_DEREF);
        rwpnNode->rwpnLeft = ParseFactor();
        return rwpnNode;
    }

    if (rwptCur->rwullType == TOK_IDENT) {
        char* rwszName = strdup(rwptCur->rwszName);
        rwptCur = rwptCur->rwptNext;

        if (rwptCur && rwptCur->rwullType == TOK_LPAREN) {
            rwptCur = rwptCur->rwptNext;
            Node* rwpnNode = NodeNew(NODE_CALL);
            rwpnNode->rwszName = rwszName;
            Node* rwpnTail = NULL;
            while (rwptCur && rwptCur->rwullType != TOK_RPAREN) {
                Node* rwpnArg = ParseExpr();
                if (!rwpnArg) break;
                if (!rwpnTail) rwpnNode->rwpnArgs = rwpnArg;
                else rwpnTail->rwpnNext = rwpnArg;
                rwpnTail = rwpnArg;
                if (rwptCur && rwptCur->rwullType == TOK_COMMA) rwptCur = rwptCur->rwptNext;
            }
            if (rwptCur && rwptCur->rwullType == TOK_RPAREN) rwptCur = rwptCur->rwptNext;
            return rwpnNode;
        }

        Node* rwpnNode = NodeNew(NODE_VAR);
        rwpnNode->rwszName = rwszName;

        while (rwptCur && rwptCur->rwullType == TOK_LBRACKET) {
            rwptCur = rwptCur->rwptNext;
            Node* rwpnIdx = ParseExpr();
            if (rwptCur && rwptCur->rwullType == TOK_RBRACKET) rwptCur = rwptCur->rwptNext;
            Node* rwpnIn = NodeNew(NODE_INDEX);
            rwpnIn->rwpnLeft = rwpnNode;
            rwpnIn->rwpnRight = rwpnIdx;
            rwpnNode = rwpnIn;
        }

        while (rwptCur && (rwptCur->rwullType == TOK_DOT || rwptCur->rwullType == TOK_ARROW)) {
            U64 rwullIsArrow = rwptCur->rwullType == TOK_ARROW;
            rwptCur = rwptCur->rwptNext;
            if (!rwptCur || rwptCur->rwullType != TOK_IDENT) break;
            char* rwszField = strdup(rwptCur->rwszName);
            rwptCur = rwptCur->rwptNext;
            Node* rwpnAcc = NodeNew(rwullIsArrow ? NODE_STRUCT_PTR_ACCESS : NODE_STRUCT_ACCESS);
            rwpnAcc->rwpnLeft = rwpnNode;
            rwpnAcc->rwszName = rwszField;
            rwpnNode = rwpnAcc;
            while (rwptCur && rwptCur->rwullType == TOK_LBRACKET) {
                rwptCur = rwptCur->rwptNext;
                Node* rwpnIdx = ParseExpr();
                if (rwptCur && rwptCur->rwullType == TOK_RBRACKET) rwptCur = rwptCur->rwptNext;
                Node* rwpnIn = NodeNew(NODE_INDEX);
                rwpnIn->rwpnLeft = rwpnNode;
                rwpnIn->rwpnRight = rwpnIdx;
                rwpnNode = rwpnIn;
            }
        }
        return rwpnNode;
    }

    if (rwptCur->rwullType == TOK_LPAREN) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnNode = ParseExpr();
        if (rwptCur && rwptCur->rwullType == TOK_RPAREN) rwptCur = rwptCur->rwptNext;
        return rwpnNode;
    }

    return NULL;
}

static Node* ParseUnary(void) {
    if (!rwptCur) return NULL;
    if (rwptCur->rwullType == TOK_NOT) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnNode = NodeNew(NODE_NOT);
        rwpnNode->rwpnLeft = ParseUnary();
        return rwpnNode;
    }
    if (rwptCur->rwullType == TOK_MINUS) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnNode = NodeNew(NODE_NEG);
        rwpnNode->rwpnLeft = ParseUnary();
        return rwpnNode;
    }
    if (rwptCur->rwullType == TOK_PLUS) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnNode = NodeNew(NODE_POS);
        rwpnNode->rwpnLeft = ParseUnary();
        return rwpnNode;
    }
    return ParseFactor();
}

static Node* ParsePower(void) {
    Node* rwpnLeft = ParseUnary();
    if (!rwpnLeft) return NULL;
    if (rwptCur && rwptCur->rwullType == TOK_POW) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnRight = ParsePower();
        Node* rwpnNode = NodeNew(NODE_POW);
        rwpnNode->rwpnLeft = rwpnLeft; rwpnNode->rwpnRight = rwpnRight;
        return rwpnNode;
    }
    return rwpnLeft;
}

static Node* ParseTerm(void) {
    Node* rwpnLeft = ParsePower();
    if (!rwpnLeft) return NULL;
    while (rwptCur && (rwptCur->rwullType == TOK_STAR ||
                       rwptCur->rwullType == TOK_DIV ||
                       rwptCur->rwullType == TOK_MOD)) {
        U64 rwullT;
        if (rwptCur->rwullType == TOK_STAR) rwullT = NODE_MUL;
        else if (rwptCur->rwullType == TOK_DIV) rwullT = NODE_DIV;
        else rwullT = NODE_MOD;
        rwptCur = rwptCur->rwptNext;
        Node* rwpnRight = ParsePower();
        if (!rwpnRight) break;
        Node* rwpnNode = NodeNew(rwullT);
        rwpnNode->rwpnLeft = rwpnLeft; rwpnNode->rwpnRight = rwpnRight;
        rwpnLeft = rwpnNode;
    }
    return rwpnLeft;
}

static Node* ParseExpr(void) {
    Node* rwpnLeft = ParseTerm();
    if (!rwpnLeft) return NULL;
    while (rwptCur && (rwptCur->rwullType == TOK_PLUS || rwptCur->rwullType == TOK_MINUS)) {
        U64 rwullT = (rwptCur->rwullType == TOK_PLUS) ? NODE_ADD : NODE_SUB;
        rwptCur = rwptCur->rwptNext;
        Node* rwpnRight = ParseTerm();
        if (!rwpnRight) break;
        Node* rwpnNode = NodeNew(rwullT);
        rwpnNode->rwpnLeft = rwpnLeft; rwpnNode->rwpnRight = rwpnRight;
        rwpnLeft = rwpnNode;
    }
    return rwpnLeft;
}

static Node* ParseCmp(void) {
    Node* rwpnLeft = ParseExpr();
    if (!rwpnLeft) return NULL;
    while (rwptCur && (rwptCur->rwullType == TOK_EQ || rwptCur->rwullType == TOK_NEQ ||
                       rwptCur->rwullType == TOK_LT || rwptCur->rwullType == TOK_GT ||
                       rwptCur->rwullType == TOK_LE || rwptCur->rwullType == TOK_GE)) {
        U64 rwullT;
        switch (rwptCur->rwullType) {
            case TOK_EQ: rwullT = NODE_EQ; break;
            case TOK_NEQ: rwullT = NODE_NEQ; break;
            case TOK_LT: rwullT = NODE_LT; break;
            case TOK_GT: rwullT = NODE_GT; break;
            case TOK_LE: rwullT = NODE_LE; break;
            default: rwullT = NODE_GE; break;
        }
        rwptCur = rwptCur->rwptNext;
        Node* rwpnRight = ParseExpr();
        if (!rwpnRight) break;
        Node* rwpnNode = NodeNew(rwullT);
        rwpnNode->rwpnLeft = rwpnLeft; rwpnNode->rwpnRight = rwpnRight;
        rwpnLeft = rwpnNode;
    }
    return rwpnLeft;
}

static Node* ParseAnd(void) {
    Node* rwpnLeft = ParseCmp();
    if (!rwpnLeft) return NULL;
    while (rwptCur && rwptCur->rwullType == TOK_AND) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnRight = ParseCmp();
        if (!rwpnRight) break;
        Node* rwpnNode = NodeNew(NODE_AND);
        rwpnNode->rwpnLeft = rwpnLeft; rwpnNode->rwpnRight = rwpnRight;
        rwpnLeft = rwpnNode;
    }
    return rwpnLeft;
}

static Node* ParseOr(void) {
    Node* rwpnLeft = ParseAnd();
    if (!rwpnLeft) return NULL;
    while (rwptCur && rwptCur->rwullType == TOK_OR) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnRight = ParseAnd();
        if (!rwpnRight) break;
        Node* rwpnNode = NodeNew(NODE_OR);
        rwpnNode->rwpnLeft = rwpnLeft; rwpnNode->rwpnRight = rwpnRight;
        rwpnLeft = rwpnNode;
    }
    return rwpnLeft;
}

static U64 TokenToVarType(U64 rwullTok) {
    switch (rwullTok) {
        case TOK_KW_U8: return 1; case TOK_KW_S8: return 2;
        case TOK_KW_U16: return 3; case TOK_KW_S16: return 4;
        case TOK_KW_U32: return 5; case TOK_KW_S32: return 6;
        case TOK_KW_U64: return 7; case TOK_KW_S64: return 8;
        case TOK_KW_U0: return 9;
    }
    return 0;
}

static int TokenIsType(U64 rwullTok) {
    return rwullTok >= TOK_KW_U8 && rwullTok <= TOK_KW_U0;
}

static Node* ParseTypeAndName(U64* rwpullPtrDepth, char** rwpwszName, U64* rwpullStructId, char** rwpwszStructName, Node** rwpwszInlineDef) {
    if (!rwptCur) return NULL;
    U64 rwullVt = 0;
    U64 rwullStructId = 0;
    char* rwszStructName = NULL;
    Node* rwpnInlineDef = NULL;

    if (TokenIsType(rwptCur->rwullType)) {
        rwullVt = TokenToVarType(rwptCur->rwullType);
        rwptCur = rwptCur->rwptNext;
    } else if (rwptCur->rwullType == TOK_KW_STRUCT) {
        rwpnInlineDef = ParseAggregateDef(NODE_STRUCT_DEF);
        rwullStructId = 1;
        if (rwpnInlineDef && rwpnInlineDef->rwszName) rwszStructName = strdup(rwpnInlineDef->rwszName);
    } else if (rwptCur->rwullType == TOK_KW_UNION) {
        rwpnInlineDef = ParseAggregateDef(NODE_UNION_DEF);
        rwullStructId = 1;
        if (rwpnInlineDef && rwpnInlineDef->rwszName) rwszStructName = strdup(rwpnInlineDef->rwszName);
    } else if (rwptCur->rwullType == TOK_IDENT) {
        rwullStructId = 1;
        rwszStructName = strdup(rwptCur->rwszName);
        rwptCur = rwptCur->rwptNext;
    } else {
        return NULL;
    }

    U64 rwullPtr = 0;
    while (rwptCur && rwptCur->rwullType == TOK_STAR) {
        rwullPtr++;
        rwptCur = rwptCur->rwptNext;
    }

    if (!rwptCur || rwptCur->rwullType != TOK_IDENT) {
        if (rwszStructName) free(rwszStructName);
        if (rwpnInlineDef) AstFree(rwpnInlineDef);
        return NULL;
    }
    *rwpwszName = strdup(rwptCur->rwszName);
    *rwpullPtrDepth = rwullPtr;
    if (rwpullStructId) *rwpullStructId = rwullStructId;
    if (rwpwszStructName) *rwpwszStructName = rwszStructName;
    else if (rwszStructName) free(rwszStructName);
    if (rwpwszInlineDef) *rwpwszInlineDef = rwpnInlineDef;
    rwptCur = rwptCur->rwptNext;
    Node* rwpnNode = NodeNew(NODE_DECL);
    rwpnNode->rwullVarType = rwullVt;
    rwpnNode->rwullPtrDepth = rwullPtr;
    rwpnNode->rwullStructId = rwullStructId;
    return rwpnNode;
}

static Node* ParseDecl(void) {
    U64 rwullPtrDepth = 0;
    U64 rwullStructId = 0;
    char* rwszName = NULL;
    char* rwszStructName = NULL;
    Node* rwpnInlineDef = NULL;
    Node* rwpnNode = ParseTypeAndName(&rwullPtrDepth, &rwszName, &rwullStructId, &rwszStructName, &rwpnInlineDef);
    if (!rwpnNode) return NULL;
    rwpnNode->rwszName = rwszName;
    rwpnNode->rwullPtrDepth = rwullPtrDepth;
    rwpnNode->rwullStructId = rwullStructId;
    rwpnNode->rwszStructName = rwszStructName;
    rwpnNode->rwpnThird = rwpnInlineDef;

    if (rwptCur && rwptCur->rwullType == TOK_LBRACKET) {
        rwptCur = rwptCur->rwptNext;
        if (rwptCur && rwptCur->rwullType == TOK_NUM) {
            rwpnNode->rwullArraySize = rwptCur->rwullValue;
            rwptCur = rwptCur->rwptNext;
        }
        if (rwptCur && rwptCur->rwullType == TOK_RBRACKET) rwptCur = rwptCur->rwptNext;
    }

    if (rwptCur && rwptCur->rwullType == TOK_ASSIGN) {
        rwptCur = rwptCur->rwptNext;
        rwpnNode->rwpnLeft = ParseOr();
    }

    if (rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;
    return rwpnNode;
}

static Node* ParseAggregateDef(U64 rwullNodeType) {
    rwptCur = rwptCur->rwptNext;
    if (!rwptCur || rwptCur->rwullType != TOK_IDENT) return NULL;
    Node* rwpnNode = NodeNew(rwullNodeType);
    rwpnNode->rwszName = strdup(rwptCur->rwszName);
    rwptCur = rwptCur->rwptNext;

    if (!rwptCur || rwptCur->rwullType != TOK_LBRACE) return rwpnNode;
    rwptCur = rwptCur->rwptNext;

    Node* rwpnTail = NULL;
    while (rwptCur && rwptCur->rwullType != TOK_RBRACE && rwptCur->rwullType != TOK_EOF) {
        U64 rwullPtrDepth = 0;
        U64 rwullStructId = 0;
        char* rwszFieldName = NULL;
        char* rwszFieldStructName = NULL;
        Node* rwpnInlineDef = NULL;
        Node* rwpnField = ParseTypeAndName(&rwullPtrDepth, &rwszFieldName, &rwullStructId, &rwszFieldStructName, &rwpnInlineDef);
        if (!rwpnField) break;
        rwpnField->rwszName = rwszFieldName;
        rwpnField->rwullPtrDepth = rwullPtrDepth;
        rwpnField->rwullStructId = rwullStructId;
        rwpnField->rwszStructName = rwszFieldStructName;
        rwpnField->rwpnThird = rwpnInlineDef;
        if (rwptCur && rwptCur->rwullType == TOK_LBRACKET) {
            rwptCur = rwptCur->rwptNext;
            if (rwptCur && rwptCur->rwullType == TOK_NUM) {
                rwpnField->rwullArraySize = rwptCur->rwullValue;
                rwptCur = rwptCur->rwptNext;
            }
            if (rwptCur && rwptCur->rwullType == TOK_RBRACKET) rwptCur = rwptCur->rwptNext;
        }
        if (rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;
        rwpnField->rwullType = NODE_STRUCT_FIELD;
        if (!rwpnTail) rwpnNode->rwpnArgs = rwpnField; else rwpnTail->rwpnNext = rwpnField;
        rwpnTail = rwpnField;
    }
    if (rwptCur && rwptCur->rwullType == TOK_RBRACE) rwptCur = rwptCur->rwptNext;
    if (rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;
    return rwpnNode;
}

static Node* ParseAssign(void) {
    Node* rwpnTarget = NULL;
    char* rwszName = NULL;

    if (rwptCur->rwullType == TOK_STAR) {
        rwptCur = rwptCur->rwptNext;
        rwpnTarget = NodeNew(NODE_DEREF);
        rwpnTarget->rwpnLeft = ParseUnary();
    } else {
        rwszName = strdup(rwptCur->rwszName);
        rwpnTarget = NodeNew(NODE_VAR);
        rwpnTarget->rwszName = strdup(rwszName);
        rwptCur = rwptCur->rwptNext;

        while (rwptCur && rwptCur->rwullType == TOK_LBRACKET) {
            rwptCur = rwptCur->rwptNext;
            Node* rwpnIdx = ParseExpr();
            if (rwptCur && rwptCur->rwullType == TOK_RBRACKET) rwptCur = rwptCur->rwptNext;
            Node* rwpnIn = NodeNew(NODE_INDEX);
            rwpnIn->rwpnLeft = rwpnTarget;
            rwpnIn->rwpnRight = rwpnIdx;
            rwpnTarget = rwpnIn;
        }

        while (rwptCur && (rwptCur->rwullType == TOK_DOT || rwptCur->rwullType == TOK_ARROW)) {
            U64 rwullIsArrow = rwptCur->rwullType == TOK_ARROW;
            rwptCur = rwptCur->rwptNext;
            if (!rwptCur || rwptCur->rwullType != TOK_IDENT) break;
            char* rwszField = strdup(rwptCur->rwszName);
            rwptCur = rwptCur->rwptNext;
            Node* rwpnAcc = NodeNew(rwullIsArrow ? NODE_STRUCT_PTR_ACCESS : NODE_STRUCT_ACCESS);
            rwpnAcc->rwpnLeft = rwpnTarget;
            rwpnAcc->rwszName = rwszField;
            rwpnTarget = rwpnAcc;
            while (rwptCur && rwptCur->rwullType == TOK_LBRACKET) {
                rwptCur = rwptCur->rwptNext;
                Node* rwpnIdx = ParseExpr();
                if (rwptCur && rwptCur->rwullType == TOK_RBRACKET) rwptCur = rwptCur->rwptNext;
                Node* rwpnIn = NodeNew(NODE_INDEX);
                rwpnIn->rwpnLeft = rwpnTarget;
                rwpnIn->rwpnRight = rwpnIdx;
                rwpnTarget = rwpnIn;
            }
        }
    }

    if (!rwptCur || rwptCur->rwullType != TOK_ASSIGN) {
        if (rwszName) free(rwszName);
        AstFree(rwpnTarget);
        return NULL;
    }
    rwptCur = rwptCur->rwptNext;

    Node* rwpnExpr = ParseOr();
    if (rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;

    Node* rwpnNode = NodeNew(NODE_ASSIGN);
    rwpnNode->rwszName = rwszName;
    rwpnNode->rwpnLeft = rwpnTarget;
    rwpnNode->rwpnRight = rwpnExpr;
    return rwpnNode;
}

static Node* ParseReturn(void) {
    rwptCur = rwptCur->rwptNext;
    Node* rwpnNode = NodeNew(NODE_RETURN);
    if (rwptCur && rwptCur->rwullType != TOK_SEMI) rwpnNode->rwpnLeft = ParseOr();
    if (rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;
    return rwpnNode;
}

static Node* ParseFunc(void) {
    rwptCur = rwptCur->rwptNext;
    U64 rwullRetType = 0;
    U64 rwullRetStruct = 0;
    char* rwszRetStructName = NULL;
    if (TokenIsType(rwptCur->rwullType)) {
        rwullRetType = TokenToVarType(rwptCur->rwullType);
        rwptCur = rwptCur->rwptNext;
    } else if (rwptCur->rwullType == TOK_IDENT) {
        rwullRetStruct = 1;
        rwszRetStructName = strdup(rwptCur->rwszName);
        rwptCur = rwptCur->rwptNext;
    }
    U64 rwullPtr = 0;
    while (rwptCur && rwptCur->rwullType == TOK_STAR) { rwullPtr++; rwptCur = rwptCur->rwptNext; }

    if (!rwptCur || rwptCur->rwullType != TOK_IDENT) return NULL;
    char* rwszName = strdup(rwptCur->rwszName);
    rwptCur = rwptCur->rwptNext;

    Node* rwpnNode = NodeNew(NODE_FUNC);
    rwpnNode->rwszName = rwszName;
    rwpnNode->rwullVarType = rwullRetType;
    rwpnNode->rwullPtrDepth = rwullPtr;
    rwpnNode->rwullStructId = rwullRetStruct;
    rwpnNode->rwszStructName = rwszRetStructName;

    if (rwptCur && rwptCur->rwullType == TOK_LPAREN) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnTail = NULL;
        while (rwptCur && rwptCur->rwullType != TOK_RPAREN) {
            U64 rwullPd = 0; char* rwszPn = NULL; U64 rwullSid = 0; char* rwszSname = NULL; Node* rwpnInline = NULL;
            Node* rwpnP = ParseTypeAndName(&rwullPd, &rwszPn, &rwullSid, &rwszSname, &rwpnInline);
            if (rwpnP) {
                rwpnP->rwszName = rwszPn;
                rwpnP->rwullPtrDepth = rwullPd;
                rwpnP->rwullStructId = rwullSid;
                rwpnP->rwszStructName = rwszSname;
                rwpnP->rwpnThird = rwpnInline;
                if (!rwpnTail) rwpnNode->rwpnArgs = rwpnP; else rwpnTail->rwpnNext = rwpnP;
                rwpnTail = rwpnP;
            }
            if (rwptCur && rwptCur->rwullType == TOK_COMMA) rwptCur = rwptCur->rwptNext;
        }
        if (rwptCur && rwptCur->rwullType == TOK_RPAREN) rwptCur = rwptCur->rwptNext;
    }

    rwpnNode->rwpnLeft = ParseBlock();
    return rwpnNode;
}

static Node* ParseIf(void) {
    rwptCur = rwptCur->rwptNext;
    if (rwptCur->rwullType != TOK_LPAREN) return NULL;
    rwptCur = rwptCur->rwptNext;
    Node* rwpnCond = ParseOr();
    if (rwptCur && rwptCur->rwullType == TOK_RPAREN) rwptCur = rwptCur->rwptNext;
    Node* rwpnThen = ParseStmt();
    Node* rwpnElse = NULL;
    if (rwptCur && rwptCur->rwullType == TOK_KW_ELSE) {
        rwptCur = rwptCur->rwptNext;
        rwpnElse = ParseStmt();
    }
    Node* rwpnNode = NodeNew(NODE_IF);
    rwpnNode->rwpnLeft = rwpnCond; rwpnNode->rwpnRight = rwpnThen; rwpnNode->rwpnThird = rwpnElse;
    return rwpnNode;
}

static Node* ParseWhile(void) {
    rwptCur = rwptCur->rwptNext;
    if (rwptCur->rwullType != TOK_LPAREN) return NULL;
    rwptCur = rwptCur->rwptNext;
    Node* rwpnCond = ParseOr();
    if (rwptCur && rwptCur->rwullType == TOK_RPAREN) rwptCur = rwptCur->rwptNext;
    Node* rwpnBody = ParseStmt();
    Node* rwpnNode = NodeNew(NODE_WHILE);
    rwpnNode->rwpnLeft = rwpnCond; rwpnNode->rwpnRight = rwpnBody;
    return rwpnNode;
}

static Node* ParseBlock(void) {
    if (!rwptCur || rwptCur->rwullType != TOK_LBRACE) return NULL;
    rwptCur = rwptCur->rwptNext;
    Node* rwpnBlock = NodeNew(NODE_BLOCK);
    Node* rwpnTail = NULL;
    while (rwptCur && rwptCur->rwullType != TOK_RBRACE && rwptCur->rwullType != TOK_EOF) {
        Node* rwpnStmt = ParseStmt();
        if (!rwpnStmt) break;
        if (!rwpnTail) rwpnBlock->rwpnLeft = rwpnStmt; else rwpnTail->rwpnNext = rwpnStmt;
        rwpnTail = rwpnStmt;
    }
    if (rwptCur && rwptCur->rwullType == TOK_RBRACE) rwptCur = rwptCur->rwptNext;
    return rwpnBlock;
}

static Node* ParseAsmOperand(void) {
    if (!rwptCur) return NULL;

    if (rwptCur->rwullType == TOK_ASM_REG) {
        Node* rwpnNode = NodeNew(NODE_ASM_REG);
        rwpnNode->rwullAsmReg = rwptCur->rwullAsmReg;
        rwptCur = rwptCur->rwptNext;
        return rwpnNode;
    }

    if (rwptCur->rwullType == TOK_NUM) {
        Node* rwpnNode = NodeNew(NODE_ASM_IMM);
        rwpnNode->rwullValue = rwptCur->rwullValue;
        rwptCur = rwptCur->rwptNext;
        return rwpnNode;
    }

    if (rwptCur->rwullType == TOK_MINUS) {
        rwptCur = rwptCur->rwptNext;
        if (rwptCur && rwptCur->rwullType == TOK_NUM) {
            Node* rwpnNode = NodeNew(NODE_ASM_IMM);
            rwpnNode->rwullValue = (U64)(-(S64)rwptCur->rwullValue);
            rwptCur = rwptCur->rwptNext;
            return rwpnNode;
        }
        return NULL;
    }

    if (rwptCur->rwullType == TOK_LBRACKET) {
        rwptCur = rwptCur->rwptNext;
        Node* rwpnMem = NodeNew(NODE_ASM_MEM);

        if (rwptCur && rwptCur->rwullType == TOK_ASM_REG) {
            rwpnMem->rwullAsmReg = rwptCur->rwullAsmReg;
            rwptCur = rwptCur->rwptNext;
        } else if (rwptCur && rwptCur->rwullType == TOK_IDENT) {
            rwpnMem->rwszName = strdup(rwptCur->rwszName);
            rwptCur = rwptCur->rwptNext;
        }

        while (rwptCur && rwptCur->rwullType != TOK_RBRACKET) {
            if (rwptCur->rwullType == TOK_PLUS) {
                rwptCur = rwptCur->rwptNext;
                if (!rwptCur) break;
                if (rwptCur->rwullType == TOK_ASM_REG) {
                    rwpnMem->rwpnLeft = NodeNew(NODE_ASM_REG);
                    rwpnMem->rwpnLeft->rwullAsmReg = rwptCur->rwullAsmReg;
                    rwptCur = rwptCur->rwptNext;
                    if (rwptCur && rwptCur->rwullType == TOK_STAR) {
                        rwptCur = rwptCur->rwptNext;
                        if (rwptCur && rwptCur->rwullType == TOK_NUM) {
                            rwpnMem->rwullAsmScale = rwptCur->rwullValue;
                            rwptCur = rwptCur->rwptNext;
                        }
                    } else {
                        rwpnMem->rwullAsmScale = 1;
                    }
                } else if (rwptCur->rwullType == TOK_IDENT) {
                    rwpnMem->rwpnLeft = NodeNew(NODE_ASM_REG);
                    rwpnMem->rwpnLeft->rwszName = strdup(rwptCur->rwszName);
                    rwptCur = rwptCur->rwptNext;
                    if (rwptCur && rwptCur->rwullType == TOK_STAR) {
                        rwptCur = rwptCur->rwptNext;
                        if (rwptCur && rwptCur->rwullType == TOK_NUM) {
                            rwpnMem->rwullAsmScale = rwptCur->rwullValue;
                            rwptCur = rwptCur->rwptNext;
                        }
                    } else {
                        rwpnMem->rwullAsmScale = 1;
                    }
                } else if (rwptCur->rwullType == TOK_NUM) {
                    rwpnMem->rwpnThird = NodeNew(NODE_ASM_IMM);
                    rwpnMem->rwpnThird->rwullValue = rwptCur->rwullValue;
                    rwptCur = rwptCur->rwptNext;
                }
            } else if (rwptCur->rwullType == TOK_MINUS) {
                rwptCur = rwptCur->rwptNext;
                if (rwptCur && rwptCur->rwullType == TOK_NUM) {
                    rwpnMem->rwpnThird = NodeNew(NODE_ASM_IMM);
                    rwpnMem->rwpnThird->rwullValue = (U64)(-(S64)rwptCur->rwullValue);
                    rwptCur = rwptCur->rwptNext;
                }
            } else {
                break;
            }
        }

        if (rwptCur && rwptCur->rwullType == TOK_RBRACKET) rwptCur = rwptCur->rwptNext;
        return rwpnMem;
    }

    return NULL;
}

static Node* ParseAsm(void) {
    Node* rwpnNode = NodeNew(NODE_ASM);
    rwpnNode->rwullAsmOp = rwptCur->rwullAsmOp;
    rwptCur = rwptCur->rwptNext;

    Node* rwpnTail = NULL;
    while (rwptCur && rwptCur->rwullType != TOK_SEMI && rwptCur->rwullType != TOK_EOF) {
        Node* rwpnOp = ParseAsmOperand();
        if (!rwpnOp) break;
        if (!rwpnTail) rwpnNode->rwpnArgs = rwpnOp;
        else rwpnTail->rwpnNext = rwpnOp;
        rwpnTail = rwpnOp;
        if (rwptCur && rwptCur->rwullType == TOK_COMMA) rwptCur = rwptCur->rwptNext;
    }

    if (rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;
    return rwpnNode;
}

static Node* ParseStmt(void) {
    if (!rwptCur) return NULL;

    if (rwptCur->rwullType == TOK_SEMI) {
        rwptCur = rwptCur->rwptNext;
        return NodeNew(NODE_BLOCK);
    }
    
    if (rwptCur->rwullType == TOK_ASM_OP) return ParseAsm();
    if (rwptCur->rwullType == TOK_KW_STRUCT) return ParseAggregateDef(NODE_STRUCT_DEF);
    if (rwptCur->rwullType == TOK_KW_UNION) return ParseAggregateDef(NODE_UNION_DEF);
    if (rwptCur->rwullType == TOK_KW_FUNC) return ParseFunc();
    if (rwptCur->rwullType == TOK_KW_RETURN) return ParseReturn();
    if (TokenIsType(rwptCur->rwullType)) return ParseDecl();
    if (rwptCur->rwullType == TOK_KW_IF) return ParseIf();
    if (rwptCur->rwullType == TOK_KW_WHILE) return ParseWhile();
    if (rwptCur->rwullType == TOK_LBRACE) return ParseBlock();
    if (rwptCur->rwullType == TOK_KW_BREAK) {
        rwptCur = rwptCur->rwptNext;
        if (rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;
        return NodeNew(NODE_BREAK);
    }
    if (rwptCur->rwullType == TOK_KW_CONTINUE) {
        rwptCur = rwptCur->rwptNext;
        if (rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;
        return NodeNew(NODE_CONTINUE);
    }
    if (rwptCur->rwullType == TOK_KW_GOTO) {
        rwptCur = rwptCur->rwptNext;
        if (!rwptCur || rwptCur->rwullType != TOK_IDENT) return NULL;
        Node* rwpnNode = NodeNew(NODE_GOTO);
        rwpnNode->rwszName = strdup(rwptCur->rwszName);
        rwptCur = rwptCur->rwptNext;
        if (rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;
        return rwpnNode;
    }
    if (rwptCur->rwullType == TOK_STAR) {
        return ParseAssign();
    }

    if (rwptCur->rwullType == TOK_IDENT &&
        rwptCur->rwptNext && rwptCur->rwptNext->rwullType == TOK_ASSIGN) {
        return ParseAssign();
    }
    if (rwptCur->rwullType == TOK_IDENT &&
        rwptCur->rwptNext && rwptCur->rwptNext->rwullType == TOK_IDENT) {
        return ParseDecl();
    }
    if (rwptCur->rwullType == TOK_IDENT &&
        rwptCur->rwptNext && rwptCur->rwptNext->rwullType == TOK_STAR) {
        return ParseDecl();
    }
    if (rwptCur->rwullType == TOK_IDENT &&
        rwptCur->rwptNext && rwptCur->rwptNext->rwullType == TOK_COLON) {
        Node* rwpnNode = NodeNew(NODE_LABEL);
        rwpnNode->rwszName = strdup(rwptCur->rwszName);
        rwptCur = rwptCur->rwptNext->rwptNext;
        return rwpnNode;
    }
    if (rwptCur->rwullType == TOK_IDENT) {
        Token* rwptScan = rwptCur->rwptNext;
        while (rwptScan && (rwptScan->rwullType == TOK_LBRACKET || rwptScan->rwullType == TOK_DOT || rwptScan->rwullType == TOK_ARROW)) {
            if (rwptScan->rwullType == TOK_LBRACKET) {
                rwptScan = rwptScan->rwptNext;
                int rwullDepth = 1;
                while (rwptScan && rwullDepth > 0) {
                    if (rwptScan->rwullType == TOK_LBRACKET) rwullDepth++;
                    else if (rwptScan->rwullType == TOK_RBRACKET) rwullDepth--;
                    rwptScan = rwptScan->rwptNext;
                }
            } else {
                rwptScan = rwptScan->rwptNext;
                if (rwptScan && rwptScan->rwullType == TOK_IDENT) rwptScan = rwptScan->rwptNext;
            }
        }
        if (rwptScan && rwptScan->rwullType == TOK_ASSIGN) {
            return ParseAssign();
        }
    }

    Node* rwpnExpr = ParseOr();
    if (rwpnExpr && rwptCur && rwptCur->rwullType == TOK_SEMI) rwptCur = rwptCur->rwptNext;
    return rwpnExpr;
}

Node* TokenToAst(TokenRoot* rwptrRoot) {
    if (!rwptrRoot || !rwptrRoot->rwptHead) return NULL;
    rwptCur = rwptrRoot->rwptHead;
    Node* rwpnBlock = NodeNew(NODE_BLOCK);
    Node* rwpnTail = NULL;
    while (rwptCur && rwptCur->rwullType != TOK_EOF) {
        Node* rwpnStmt = ParseStmt();
        if (!rwpnStmt) break;
        if (!rwpnTail) rwpnBlock->rwpnLeft = rwpnStmt; else rwpnTail->rwpnNext = rwpnStmt;
        rwpnTail = rwpnStmt;
    }
    return rwpnBlock;
}

void AstFree(Node* rwpnRoot) {
    if (!rwpnRoot) return;
    AstFree(rwpnRoot->rwpnLeft);
    AstFree(rwpnRoot->rwpnRight);
    AstFree(rwpnRoot->rwpnNext);
    AstFree(rwpnRoot->rwpnThird);
    AstFree(rwpnRoot->rwpnArgs);
    if (rwpnRoot->rwszName) free(rwpnRoot->rwszName);
    if (rwpnRoot->rwszStructName) free(rwpnRoot->rwszStructName);
    free(rwpnRoot);
}

void AstPrint(Node* rwpnRoot, int rwullDepth) {
    if (!rwpnRoot) return;
    for (int i = 0; i < rwullDepth; i++) printf("  ");
    switch (rwpnRoot->rwullType) {
        case NODE_NUM: printf("NUM(%llu)\n", (unsigned long long)rwpnRoot->rwullValue); break;
        case NODE_STR: printf("STR(%s)\n", rwpnRoot->rwszName); break;
        case NODE_ADD: printf("ADD\n"); break;
        case NODE_SUB: printf("SUB\n"); break;
        case NODE_MUL: printf("MUL\n"); break;
        case NODE_DIV: printf("DIV\n"); break;
        case NODE_MOD: printf("MOD\n"); break;
        case NODE_POW: printf("POW\n"); break;
        case NODE_NEG: printf("NEG\n"); break;
        case NODE_POS: printf("POS\n"); break;
        case NODE_DECL: printf("DECL(t=%llu,p=%llu,s=%llu,sn=%s,n=%s,sz=%llu)\n",
            (unsigned long long)rwpnRoot->rwullVarType,
            (unsigned long long)rwpnRoot->rwullPtrDepth,
            (unsigned long long)rwpnRoot->rwullStructId,
            rwpnRoot->rwszStructName ? rwpnRoot->rwszStructName : "-",
            rwpnRoot->rwszName,
            (unsigned long long)rwpnRoot->rwullArraySize); break;
        case NODE_VAR: printf("VAR(%s)\n", rwpnRoot->rwszName); break;
        case NODE_ASSIGN:
            if (rwpnRoot->rwszName) printf("ASSIGN(%s)\n", rwpnRoot->rwszName);
            else printf("ASSIGN\n");
            break;
        case NODE_BLOCK: printf("BLOCK\n"); break;
        case NODE_IF: printf("IF\n"); break;
        case NODE_WHILE: printf("WHILE\n"); break;
        case NODE_EQ: printf("EQ\n"); break;
        case NODE_NEQ: printf("NEQ\n"); break;
        case NODE_LT: printf("LT\n"); break;
        case NODE_GT: printf("GT\n"); break;
        case NODE_LE: printf("LE\n"); break;
        case NODE_GE: printf("GE\n"); break;
        case NODE_AND: printf("AND\n"); break;
        case NODE_OR: printf("OR\n"); break;
        case NODE_NOT: printf("NOT\n"); break;
        case NODE_FUNC: printf("FUNC(%s)\n", rwpnRoot->rwszName); break;
        case NODE_CALL: printf("CALL(%s)\n", rwpnRoot->rwszName); break;
        case NODE_RETURN: printf("RETURN\n"); break;
        case NODE_ARRAY: printf("ARRAY\n"); break;
        case NODE_INDEX: printf("INDEX\n"); break;
        case NODE_ADDR: printf("ADDR\n"); break;
        case NODE_DEREF: printf("DEREF\n"); break;
        case NODE_BREAK: printf("BREAK\n"); break;
        case NODE_CONTINUE: printf("CONTINUE\n"); break;
        case NODE_GOTO: printf("GOTO(%s)\n", rwpnRoot->rwszName); break;
        case NODE_LABEL: printf("LABEL(%s)\n", rwpnRoot->rwszName); break;
        case NODE_ASM: printf("ASM(%s)\n", AsmOpName(rwpnRoot->rwullAsmOp)); break;
        case NODE_ASM_REG: printf("ASM_REG(%s)\n", AsmRegName(rwpnRoot->rwullAsmReg)); break;
        case NODE_ASM_IMM: printf("ASM_IMM(%llu)\n", (unsigned long long)rwpnRoot->rwullValue); break;
        case NODE_ASM_MEM: printf("ASM_MEM\n"); break;
        case NODE_ASM_LABEL: printf("ASM_LABEL(%s)\n", rwpnRoot->rwszName); break;
        case NODE_STRUCT_DEF: printf("STRUCT_DEF(%s)\n", rwpnRoot->rwszName); break;
        case NODE_UNION_DEF: printf("UNION_DEF(%s)\n", rwpnRoot->rwszName); break;
        case NODE_STRUCT_FIELD: printf("FIELD(t=%llu,p=%llu,s=%llu,sn=%s,n=%s,sz=%llu)\n",
            (unsigned long long)rwpnRoot->rwullVarType,
            (unsigned long long)rwpnRoot->rwullPtrDepth,
            (unsigned long long)rwpnRoot->rwullStructId,
            rwpnRoot->rwszStructName ? rwpnRoot->rwszStructName : "-",
            rwpnRoot->rwszName,
            (unsigned long long)rwpnRoot->rwullArraySize); break;
        case NODE_STRUCT_ACCESS: printf("STRUCT_ACCESS(.%s)\n", rwpnRoot->rwszName); break;
        case NODE_STRUCT_PTR_ACCESS: printf("STRUCT_PTR_ACCESS(->%s)\n", rwpnRoot->rwszName); break;
        case NODE_SIZEOF: printf("SIZEOF(%s)\n", rwpnRoot->rwszName); break;
        default: printf("?\n"); break;
    }
    if (rwpnRoot->rwullType == NODE_FUNC) {
        Node* rwpnP = rwpnRoot->rwpnArgs;
        while (rwpnP) {
            for (int i = 0; i <= rwullDepth; i++) printf("  ");
            printf("DECL(t=%llu,p=%llu,s=%llu,sn=%s,n=%s,sz=%llu)\n",
                (unsigned long long)rwpnP->rwullVarType,
                (unsigned long long)rwpnP->rwullPtrDepth,
                (unsigned long long)rwpnP->rwullStructId,
                rwpnP->rwszStructName ? rwpnP->rwszStructName : "-",
                rwpnP->rwszName,
                (unsigned long long)rwpnP->rwullArraySize);
            rwpnP = rwpnP->rwpnNext;
        }
        AstPrint(rwpnRoot->rwpnLeft, rwullDepth + 1);
        AstPrint(rwpnRoot->rwpnNext, rwullDepth);
        return;
    }
    if (rwpnRoot->rwullType == NODE_STRUCT_DEF || rwpnRoot->rwullType == NODE_UNION_DEF) {
        Node* rwpnF = rwpnRoot->rwpnArgs;
        while (rwpnF) { AstPrint(rwpnF, rwullDepth + 1); rwpnF = rwpnF->rwpnNext; }
        AstPrint(rwpnRoot->rwpnNext, rwullDepth);
        return;
    }
    AstPrint(rwpnRoot->rwpnLeft, rwullDepth + 1);
    AstPrint(rwpnRoot->rwpnRight, rwullDepth + 1);
    if (rwpnRoot->rwullType == NODE_IF) {
        if (rwpnRoot->rwpnThird) {
            for (int i = 0; i <= rwullDepth; i++) printf("  ");
            printf("ELSE\n");
            AstPrint(rwpnRoot->rwpnThird, rwullDepth + 1);
        }
        AstPrint(rwpnRoot->rwpnNext, rwullDepth);
        return;
    }
    AstPrint(rwpnRoot->rwpnThird, rwullDepth);
    AstPrint(rwpnRoot->rwpnArgs, rwullDepth + 1);
    AstPrint(rwpnRoot->rwpnNext, rwullDepth);
}
