#pragma once
#include "int.h"
#include "ast.h"

typedef struct CodeBuf CodeBuf;
struct CodeBuf {
    U8* rwpData;
    U64 rwullSize;
    U64 rwullCap;
};

typedef struct Symbol Symbol;
struct Symbol {
    char* rwszName;
    U64 rwullVarType;
    U64 rwullPtrDepth;
    U64 rwullArraySize;
    U64 rwullOffset;
    U64 rwullIsParam;
    U64 rwullIsGlobal;
    U64 rwullGlobalAddr;
    U64 rwullIsFunc;
    U64 rwullFuncId;
    U64 rwullStructId;
    char* rwszStructName;
    Symbol* rwptNext;
};

typedef struct Reloc {
    U64 rwullPos;
    char* rwszName;
    struct Reloc* rwptNext;
} Reloc;

typedef struct StringLit {
    char* rwszText;
    U64 rwullId;
    U64 rwullAddr;
    struct StringLit* rwptNext;
} StringLit;

typedef struct LabelInfo {
    char* rwszName;
    U64 rwullAddr;
    struct LabelInfo* rwptNext;
} LabelInfo;

typedef struct StrReloc {
    U64 rwullPos;
    U64 rwullId;
    struct StrReloc* rwptNext;
} StrReloc;

typedef struct StructField StructField;
struct StructField {
    char* rwszName;
    U64 rwullVarType;
    U64 rwullPtrDepth;
    U64 rwullArraySize;
    U64 rwullOffset;
    U64 rwullStructId;
    char* rwszStructName;
    StructField* rwptNext;
};

typedef struct StructInfo StructInfo;
struct StructInfo {
    char* rwszName;
    U64 rwullSize;
    U64 rwullAlign;
    U64 rwullIsUnion;
    StructField* rwpsfFields;
    StructInfo* rwpsiNext;
};

CodeBuf* AstToCode(Node* rwpnRoot);
void CodeBufFree(CodeBuf* rwpcbCodeBuf);
