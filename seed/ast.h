#pragma once
#include "int.h"
#include "grammar.h"

typedef enum NodeType {
    NODE_NUM = 1, NODE_STR, NODE_ADD, NODE_SUB, NODE_MUL, NODE_DIV, NODE_MOD, NODE_POW,
    NODE_NEG, NODE_POS,
    NODE_DECL, NODE_VAR, NODE_ASSIGN,
    NODE_BLOCK, NODE_IF, NODE_WHILE,
    NODE_EQ, NODE_NEQ, NODE_LT, NODE_GT, NODE_LE, NODE_GE,
    NODE_AND, NODE_OR, NODE_NOT,
    NODE_FUNC, NODE_CALL, NODE_RETURN,
    NODE_ARRAY, NODE_INDEX, NODE_ADDR, NODE_DEREF,
    NODE_BREAK, NODE_CONTINUE, NODE_GOTO, NODE_LABEL,
    NODE_ASM,
    NODE_ASM_REG, NODE_ASM_IMM, NODE_ASM_MEM, NODE_ASM_LABEL,
    NODE_STRUCT_DEF, NODE_STRUCT_FIELD, NODE_STRUCT_ACCESS, NODE_STRUCT_PTR_ACCESS, NODE_SIZEOF, NODE_UNION_DEF
} NodeType;

typedef struct Node Node;
struct Node {
    U64 rwullType;
    U64 rwullValue;
    U64 rwullVarType;
    U64 rwullPtrDepth;
    U64 rwullArraySize;
    U64 rwullAsmOp;
    U64 rwullAsmReg;
    U64 rwullAsmScale;
    U64 rwullStructId;
    char* rwszName;
    char* rwszStructName;
    Node* rwpnLeft;
    Node* rwpnRight;
    Node* rwpnNext;
    Node* rwpnThird;
    Node* rwpnArgs;
};

Node* TokenToAst(TokenRoot* rwptrRoot);
void AstFree(Node* rwpnRoot);
void AstPrint(Node* rwpnRoot, int rwullDepth);
