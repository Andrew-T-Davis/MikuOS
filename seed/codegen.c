#include "codegen.h"
#include "asm.h"
#include "int.h"
#include <stdlib.h>
#include <string.h>

static Symbol* rwpsSymbols = NULL;
static Reloc* rwprlRelocs = NULL;
static U64 rwullStackSize = 0;
static U64 rwullNextFuncId = 1;
static U64 rwullGlobalSize = 0;
static StructInfo* rwpsiStructs = NULL;

typedef struct FuncInfo {
    char* rwszName;
    U64 rwullId;
    U64 rwullAddr;
    U64 rwullStackSize;
    U64 rwullRetByValue;
    U64 rwullRetSize;
    char* rwszRetStructName;
    Symbol* rwpsLocals;
    struct FuncInfo* rwpfiNext;
} FuncInfo;

typedef struct LabelReloc {
    U64 rwullPos;
    char* rwszName;
    struct LabelReloc* rwplrNext;
} LabelReloc;

typedef struct BreakFixup {
    U64 rwullPos;
    struct BreakFixup* rwpbfNext;
} BreakFixup;

typedef struct LoopCtx {
    U64 rwullStart;
    U64 rwullEndPlaceholder;
    struct LoopCtx* rwplcNext;
} LoopCtx;

typedef struct AsmReloc {
    U64 rwullPos;
    char* rwszName;
    struct AsmReloc* rwparNext;
} AsmReloc;

static StrReloc* rwpsrStrRelocs = NULL;
static FuncInfo* rwpfiFuncs = NULL;
static FuncInfo* rwpfiCurFunc = NULL;
static StringLit* rwpslStrings = NULL;
static LabelInfo* rwpliLabels = NULL;
static LabelReloc* rwplrLabelRelocs = NULL;
static BreakFixup* rwpbfBreakFixups = NULL;
static LoopCtx* rwplcLoopStack = NULL;
static AsmReloc* rwparAsmRelocs = NULL;
static U64 rwullNextStringId = 1;

static void StrRelocAdd(U64 rwullPos, U64 rwullId) {
    StrReloc* rwpsrStrReloc = malloc(sizeof(StrReloc));
    rwpsrStrReloc->rwullPos = rwullPos;
    rwpsrStrReloc->rwullId = rwullId;
    rwpsrStrReloc->rwptNext = rwpsrStrRelocs;
    rwpsrStrRelocs = rwpsrStrReloc;
}

static void AsmRelocAdd(U64 rwullPos, const char* rwszName) {
    AsmReloc* rwparAsmReloc = malloc(sizeof(AsmReloc));
    rwparAsmReloc->rwullPos = rwullPos;
    rwparAsmReloc->rwszName = strdup(rwszName);
    rwparAsmReloc->rwparNext = rwparAsmRelocs;
    rwparAsmRelocs = rwparAsmReloc;
}

static void BufGrow(CodeBuf* rwpcbCodeBuf, U64 rwullN) {
    if (rwpcbCodeBuf->rwullSize + rwullN <= rwpcbCodeBuf->rwullCap) return;
    U64 rwullC = rwpcbCodeBuf->rwullCap ? rwpcbCodeBuf->rwullCap * 2 : 64;
    while (rwpcbCodeBuf->rwullSize + rwullN > rwullC) rwullC *= 2;
    rwpcbCodeBuf->rwpData = realloc(rwpcbCodeBuf->rwpData, rwullC);
    rwpcbCodeBuf->rwullCap = rwullC;
}
static void E8(CodeBuf* rwpcbCodeBuf, U8 rwullV) { BufGrow(rwpcbCodeBuf, 1); rwpcbCodeBuf->rwpData[rwpcbCodeBuf->rwullSize++] = rwullV; }
static void E16(CodeBuf* rwpcbCodeBuf, U16 rwullV) { BufGrow(rwpcbCodeBuf, 2); memcpy(rwpcbCodeBuf->rwpData + rwpcbCodeBuf->rwullSize, &rwullV, 2); rwpcbCodeBuf->rwullSize += 2; }
static void E32(CodeBuf* rwpcbCodeBuf, U32 rwullV) { BufGrow(rwpcbCodeBuf, 4); memcpy(rwpcbCodeBuf->rwpData + rwpcbCodeBuf->rwullSize, &rwullV, 4); rwpcbCodeBuf->rwullSize += 4; }
static void E64(CodeBuf* rwpcbCodeBuf, U64 rwullV) { BufGrow(rwpcbCodeBuf, 8); memcpy(rwpcbCodeBuf->rwpData + rwpcbCodeBuf->rwullSize, &rwullV, 8); rwpcbCodeBuf->rwullSize += 8; }

static U64 JmpPH(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0xE9); U64 rwullP = rwpcbCodeBuf->rwullSize; E32(rwpcbCodeBuf, 0); return rwullP; }
static U64 JzPH(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x84); U64 rwullP = rwpcbCodeBuf->rwullSize; E32(rwpcbCodeBuf, 0); return rwullP; }
static U64 JnzPH(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x85); U64 rwullP = rwpcbCodeBuf->rwullSize; E32(rwpcbCodeBuf, 0); return rwullP; }
static void Patch32(CodeBuf* rwpcbCodeBuf, U64 rwullPos, U64 rwullTarget) {
    S64 rwullD = (S64)rwullTarget - (S64)(rwullPos + 4);
    S32 rwullDd = (S32)rwullD;
    memcpy(rwpcbCodeBuf->rwpData + rwullPos, &rwullDd, 4);
}
static void Setcc(CodeBuf* rwpcbCodeBuf, U8 rwullOp) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, rwullOp); E8(rwpcbCodeBuf, 0xC0); }
static void MovzxEaxAl(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xB6); E8(rwpcbCodeBuf, 0xC0); }
static void TestRax(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x85); E8(rwpcbCodeBuf, 0xC0); }
static void XorRax(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x31); E8(rwpcbCodeBuf, 0xC0); }
static void MovRaxImm(CodeBuf* rwpcbCodeBuf, U64 rwullV) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0xB8); E64(rwpcbCodeBuf, rwullV); }
static void PushRax(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x50); }
static void PopRcx(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x59); }
static void AddRaxRcx(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x01); E8(rwpcbCodeBuf, 0xC8); }
static void SubRaxRcx(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x29); E8(rwpcbCodeBuf, 0xC8); }
static void ImulRaxRcx(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xAF); E8(rwpcbCodeBuf, 0xC1); }
static void IdivRcx(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x99); E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0xF7); E8(rwpcbCodeBuf, 0xF9); }
static void CmpRaxRcx(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x39); E8(rwpcbCodeBuf, 0xC1); }
static void NegRax(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0xF7); E8(rwpcbCodeBuf, 0xD8); }
static void MovRspRbp(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xEC); }
static void PopRbp(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x5D); }
static void Ret(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0xC3); }
static void PushRbp(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x55); }
static void MovRbpRsp(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xE5); }
static void AddRspImm32(CodeBuf* rwpcbCodeBuf, U32 rwullV) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x81); E8(rwpcbCodeBuf, 0xC4); E32(rwpcbCodeBuf, rwullV); }
static void ShlRaxImm(CodeBuf* rwpcbCodeBuf, U8 rwullV) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0xC1); E8(rwpcbCodeBuf, 0xE0); E8(rwpcbCodeBuf, rwullV); }
static void PopRax(CodeBuf* rwpcbCodeBuf) { E8(rwpcbCodeBuf, 0x58); }

static StructInfo* StructFind(const char* rwszName) {
    StructInfo* rwpsiStruct = rwpsiStructs;
    while (rwpsiStruct) {
        if (strcmp(rwpsiStruct->rwszName, rwszName) == 0) return rwpsiStruct;
        rwpsiStruct = rwpsiStruct->rwpsiNext;
    }
    return NULL;
}

static StructField* StructFieldFind(StructInfo* rwpsiStruct, const char* rwszField) {
    if (!rwpsiStruct) return NULL;
    StructField* rwpsfField = rwpsiStruct->rwpsfFields;
    while (rwpsfField) {
        if (strcmp(rwpsfField->rwszName, rwszField) == 0) return rwpsfField;
        rwpsfField = rwpsfField->rwptNext;
    }
    return NULL;
}

static U64 TypeSize(U64 rwullT) {
    switch (rwullT) {
        case 1: case 2: return 1;
        case 3: case 4: return 2;
        case 5: case 6: return 4;
        case 7: case 8: return 8;
    }
    return 0;
}
static int TypeSigned(U64 rwullT) { return rwullT == 2 || rwullT == 4 || rwullT == 6 || rwullT == 8; }
static U64 Log2Ceil(U64 rwullV) { U64 rwullR = 0, rwullX = 1; while (rwullX < rwullV) { rwullX <<= 1; rwullR++; } return rwullR; }
static U64 Align8(U64 rwullV) { return (rwullV + 7) & ~7ULL; }

static U64 StructSize(const char* rwszName) {
    StructInfo* rwpsiStruct = StructFind(rwszName);
    return rwpsiStruct ? rwpsiStruct->rwullSize : 8;
}

static U64 FieldSize(U64 rwullT, U64 rwullPtr, U64 rwullArraySize, U64 rwullStructId, const char* rwszStructName) {
    U64 rwullSz;
    if (rwullPtr) rwullSz = 8;
    else if (rwullStructId) {
        rwullSz = StructSize(rwszStructName);
    } else {
        rwullSz = TypeSize(rwullT);
        if (rwullSz == 0) rwullSz = 8;
    }
    if (rwullArraySize) rwullSz *= rwullArraySize;
    return rwullSz;
}

static U64 SymbolSize(Symbol* rwpsSymbol) {
    return FieldSize(rwpsSymbol->rwullVarType, rwpsSymbol->rwullPtrDepth,
                     rwpsSymbol->rwullArraySize, rwpsSymbol->rwullStructId,
                     rwpsSymbol->rwszStructName);
}

static U64 SymbolElemSize(Symbol* rwpsSymbol) {
    if (rwpsSymbol->rwullPtrDepth) return 8;
    if (rwpsSymbol->rwullStructId) {
        return StructSize(rwpsSymbol->rwszStructName);
    }
    U64 rwullSz = TypeSize(rwpsSymbol->rwullVarType);
    return rwullSz ? rwullSz : 8;
}

static void StructDefAdd(Node* rwpnDef, U64 rwullIsUnion) {
    StructInfo* rwpsiStruct = malloc(sizeof(StructInfo));
    memset(rwpsiStruct, 0, sizeof(StructInfo));
    rwpsiStruct->rwszName = strdup(rwpnDef->rwszName);
    rwpsiStruct->rwullAlign = 1;
    rwpsiStruct->rwullIsUnion = rwullIsUnion;

    U64 rwullOffset = 0;
    U64 rwullMaxSize = 0;
    Node* rwpnF = rwpnDef->rwpnArgs;
    StructField* rwpsfTail = NULL;
    while (rwpnF) {
        if (rwpnF->rwpnThird) {
            if (rwpnF->rwpnThird->rwullType == NODE_UNION_DEF)
                StructDefAdd(rwpnF->rwpnThird, 1);
            else
                StructDefAdd(rwpnF->rwpnThird, 0);
        }

        U64 rwullFsz;
        if (rwpnF->rwullPtrDepth) rwullFsz = 8;
        else if (rwpnF->rwullStructId) {
            rwullFsz = StructSize(rwpnF->rwszStructName);
        } else {
            rwullFsz = TypeSize(rwpnF->rwullVarType);
            if (rwullFsz == 0) rwullFsz = 8;
        }
        if (rwpnF->rwullArraySize) rwullFsz *= rwpnF->rwullArraySize;
        if (rwullFsz == 0) rwullFsz = 1;

        U64 rwullAlign = rwullFsz > 8 ? 8 : rwullFsz;
        if (rwullAlign == 0) rwullAlign = 1;
        if (rwpsiStruct->rwullAlign < rwullAlign) rwpsiStruct->rwullAlign = rwullAlign;

        U64 rwullFieldOffset;
        if (rwullIsUnion) {
            rwullFieldOffset = 0;
            if (rwullFsz > rwullMaxSize) rwullMaxSize = rwullFsz;
        } else {
            rwullOffset = (rwullOffset + rwullAlign - 1) & ~(rwullAlign - 1);
            rwullFieldOffset = rwullOffset;
            rwullOffset += rwullFsz;
        }

        StructField* rwpsfField = malloc(sizeof(StructField));
        memset(rwpsfField, 0, sizeof(StructField));
        rwpsfField->rwszName = strdup(rwpnF->rwszName);
        rwpsfField->rwullVarType = rwpnF->rwullVarType;
        rwpsfField->rwullPtrDepth = rwpnF->rwullPtrDepth;
        rwpsfField->rwullArraySize = rwpnF->rwullArraySize;
        rwpsfField->rwullOffset = rwullFieldOffset;
        rwpsfField->rwullStructId = rwpnF->rwullStructId;
        if (rwpnF->rwszStructName) rwpsfField->rwszStructName = strdup(rwpnF->rwszStructName);
        if (!rwpsfTail) rwpsiStruct->rwpsfFields = rwpsfField;
        else rwpsfTail->rwptNext = rwpsfField;
        rwpsfTail = rwpsfField;

        rwpnF = rwpnF->rwpnNext;
    }

    if (rwullIsUnion) rwullOffset = rwullMaxSize;
    rwpsiStruct->rwullSize = (rwullOffset + rwpsiStruct->rwullAlign - 1) & ~(rwpsiStruct->rwullAlign - 1);
    if (rwpsiStruct->rwullSize == 0) rwpsiStruct->rwullSize = 1;
    rwpsiStruct->rwpsiNext = rwpsiStructs;
    rwpsiStructs = rwpsiStruct;
}

static Symbol* SymFindLocal(const char* rwszN) {
    if (!rwpfiCurFunc) return NULL;
    Symbol* rwpsSymbol = rwpfiCurFunc->rwpsLocals;
    while (rwpsSymbol) { if (strcmp(rwpsSymbol->rwszName, rwszN) == 0) return rwpsSymbol; rwpsSymbol = rwpsSymbol->rwptNext; }
    return NULL;
}

static Symbol* SymFindGlobal(const char* rwszN) {
    Symbol* rwpsSymbol = rwpsSymbols;
    while (rwpsSymbol) { if (strcmp(rwpsSymbol->rwszName, rwszN) == 0) return rwpsSymbol; rwpsSymbol = rwpsSymbol->rwptNext; }
    return NULL;
}

static Symbol* SymAddLocal(const char* rwszN, U64 rwullT, U64 rwullPtr, U64 rwullArr, U64 rwullSid, const char* rwszSname) {
    U64 rwullSz = FieldSize(rwullT, rwullPtr, rwullArr, rwullSid, rwszSname);
    if (rwullSz == 0) rwullSz = 8;
    rwullSz = Align8(rwullSz);
    rwullStackSize += rwullSz;

    Symbol* rwpsSymbol = malloc(sizeof(Symbol));
    memset(rwpsSymbol, 0, sizeof(Symbol));
    rwpsSymbol->rwszName = strdup(rwszN);
    rwpsSymbol->rwullVarType = rwullT;
    rwpsSymbol->rwullPtrDepth = rwullPtr;
    rwpsSymbol->rwullArraySize = rwullArr;
    rwpsSymbol->rwullStructId = rwullSid;
    if (rwszSname) rwpsSymbol->rwszStructName = strdup(rwszSname);
    rwpsSymbol->rwullOffset = rwullStackSize;
    rwpsSymbol->rwptNext = rwpfiCurFunc->rwpsLocals;
    rwpfiCurFunc->rwpsLocals = rwpsSymbol;
    return rwpsSymbol;
}

static void SymAddGlobal(const char* rwszN, U64 rwullT, U64 rwullPtr, U64 rwullArr, U64 rwullSid, const char* rwszSname) {
    U64 rwullSz = FieldSize(rwullT, rwullPtr, rwullArr, rwullSid, rwszSname);
    if (rwullSz == 0) rwullSz = 8;
    rwullSz = Align8(rwullSz);

    Symbol* rwpsSymbol = malloc(sizeof(Symbol));
    memset(rwpsSymbol, 0, sizeof(Symbol));
    rwpsSymbol->rwszName = strdup(rwszN);
    rwpsSymbol->rwullVarType = rwullT;
    rwpsSymbol->rwullPtrDepth = rwullPtr;
    rwpsSymbol->rwullArraySize = rwullArr;
    rwpsSymbol->rwullStructId = rwullSid;
    if (rwszSname) rwpsSymbol->rwszStructName = strdup(rwszSname);
    rwpsSymbol->rwullIsGlobal = 1;
    rwpsSymbol->rwullGlobalAddr = rwullGlobalSize;
    rwpsSymbol->rwptNext = rwpsSymbols;
    rwpsSymbols = rwpsSymbol;

    rwullGlobalSize += rwullSz;
}

static void SymAddFunc(const char* rwszN, U64 rwullId) {
    Symbol* rwpsSymbol = malloc(sizeof(Symbol));
    memset(rwpsSymbol, 0, sizeof(Symbol));
    rwpsSymbol->rwszName = strdup(rwszN);
    rwpsSymbol->rwullIsFunc = 1;
    rwpsSymbol->rwullFuncId = rwullId;
    rwpsSymbol->rwptNext = rwpsSymbols;
    rwpsSymbols = rwpsSymbol;
}

static FuncInfo* FuncFind(const char* rwszN) {
    FuncInfo* rwpfiFunc = rwpfiFuncs;
    while (rwpfiFunc) { if (strcmp(rwpfiFunc->rwszName, rwszN) == 0) return rwpfiFunc; rwpfiFunc = rwpfiFunc->rwpfiNext; }
    return NULL;
}

static void RelocAdd(U64 rwullPos, const char* rwszName) {
    Reloc* rwprlReloc = malloc(sizeof(Reloc));
    rwprlReloc->rwullPos = rwullPos;
    rwprlReloc->rwszName = strdup(rwszName);
    rwprlReloc->rwptNext = rwprlRelocs;
    rwprlRelocs = rwprlReloc;
}

static U64 StrAdd(const char* rwszS) {
    StringLit* rwpslString = malloc(sizeof(StringLit));
    rwpslString->rwszText = strdup(rwszS);
    rwpslString->rwullId = rwullNextStringId++;
    rwpslString->rwullAddr = 0;
    rwpslString->rwptNext = rwpslStrings;
    rwpslStrings = rwpslString;
    return rwpslString->rwullId;
}

static StringLit* StrFind(U64 rwullId) {
    StringLit* rwpslString = rwpslStrings;
    while (rwpslString) { if (rwpslString->rwullId == rwullId) return rwpslString; rwpslString = rwpslString->rwptNext; }
    return NULL;
}

static void LabelRelocAdd(U64 rwullPos, const char* rwszName) {
    LabelReloc* rwplrLabelReloc = malloc(sizeof(LabelReloc));
    rwplrLabelReloc->rwullPos = rwullPos;
    rwplrLabelReloc->rwszName = strdup(rwszName);
    rwplrLabelReloc->rwplrNext = rwplrLabelRelocs;
    rwplrLabelRelocs = rwplrLabelReloc;
}

static void BreakFixupAdd(U64 rwullPos) {
    BreakFixup* rwpbfBreak = malloc(sizeof(BreakFixup));
    rwpbfBreak->rwullPos = rwullPos;
    rwpbfBreak->rwpbfNext = rwpbfBreakFixups;
    rwpbfBreakFixups = rwpbfBreak;
}

static void GenNode(CodeBuf* rwpcbCodeBuf, Node* rwpnNode);

static void GenVarAddr(CodeBuf* rwpcbCodeBuf, Symbol* rwpsSymbol) {
    if (rwpsSymbol->rwullIsGlobal) {
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x05);
        U64 rwullPos = rwpcbCodeBuf->rwullSize;
        E32(rwpcbCodeBuf, 0);
        RelocAdd(rwullPos, rwpsSymbol->rwszName);
    } else if (rwpsSymbol->rwullIsParam) {
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x85);
        E32(rwpcbCodeBuf, (U32)rwpsSymbol->rwullOffset);
    } else {
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x85);
        E32(rwpcbCodeBuf, (U32)(-(S32)rwpsSymbol->rwullOffset));
    }
}

static StructField* StructFieldOf(Node* rwpnNode) {
    if (!rwpnNode) return NULL;
    if (rwpnNode->rwullType != NODE_STRUCT_ACCESS && rwpnNode->rwullType != NODE_STRUCT_PTR_ACCESS) return NULL;
    Node* rwpnBase = rwpnNode->rwpnLeft;
    while (rwpnBase && rwpnBase->rwullType == NODE_INDEX) rwpnBase = rwpnBase->rwpnLeft;
    if (!rwpnBase) return NULL;
    if (rwpnBase->rwullType == NODE_VAR) {
        Symbol* rwpsSymbol = SymFindLocal(rwpnBase->rwszName);
        if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnBase->rwszName);
        if (rwpsSymbol && rwpsSymbol->rwszStructName) {
            StructInfo* rwpsiStruct = StructFind(rwpsSymbol->rwszStructName);
            return StructFieldFind(rwpsiStruct, rwpnNode->rwszName);
        }
    }
    if (rwpnBase->rwullType == NODE_STRUCT_ACCESS || rwpnBase->rwullType == NODE_STRUCT_PTR_ACCESS) {
        StructField* rwpsfBase = StructFieldOf(rwpnBase);
        if (rwpsfBase && rwpsfBase->rwszStructName) {
            StructInfo* rwpsiStruct = StructFind(rwpsfBase->rwszStructName);
            return StructFieldFind(rwpsiStruct, rwpnNode->rwszName);
        }
    }
    return NULL;
}

static U64 StructFieldOffsetRecursive(Node* rwpnNode) {
    if (!rwpnNode) return 0;
    if (rwpnNode->rwullType != NODE_STRUCT_ACCESS && rwpnNode->rwullType != NODE_STRUCT_PTR_ACCESS) return 0;
    U64 rwullOffset = 0;
    if (rwpnNode->rwpnLeft && (rwpnNode->rwpnLeft->rwullType == NODE_STRUCT_ACCESS ||
                               rwpnNode->rwpnLeft->rwullType == NODE_STRUCT_PTR_ACCESS)) {
        rwullOffset += StructFieldOffsetRecursive(rwpnNode->rwpnLeft);
        StructField* rwpsfBase = StructFieldOf(rwpnNode->rwpnLeft);
        if (rwpsfBase && rwpsfBase->rwszStructName) {
            StructInfo* rwpsiStruct = StructFind(rwpsfBase->rwszStructName);
            StructField* rwpsfField = StructFieldFind(rwpsiStruct, rwpnNode->rwszName);
            if (rwpsfField) rwullOffset += rwpsfField->rwullOffset;
        }
    } else {
        Node* rwpnBase = rwpnNode->rwpnLeft;
        while (rwpnBase && rwpnBase->rwullType == NODE_INDEX) rwpnBase = rwpnBase->rwpnLeft;
        if (rwpnBase && rwpnBase->rwullType == NODE_VAR) {
            Symbol* rwpsSymbol = SymFindLocal(rwpnBase->rwszName);
            if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnBase->rwszName);
            if (rwpsSymbol && rwpsSymbol->rwszStructName) {
                StructInfo* rwpsiStruct = StructFind(rwpsSymbol->rwszStructName);
                StructField* rwpsfField = StructFieldFind(rwpsiStruct, rwpnNode->rwszName);
                if (rwpsfField) rwullOffset = rwpsfField->rwullOffset;
            }
        }
    }
    return rwullOffset;
}

static void GenLoad(CodeBuf* rwpcbCodeBuf, U64 rwullSize, int rwullIsSigned) {
    if (rwullSize == 1) {
        if (rwullIsSigned) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xBE); }
        else { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xB6); }
        E8(rwpcbCodeBuf, 0x00);
    } else if (rwullSize == 2) {
        if (rwullIsSigned) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xBF); }
        else { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xB7); }
        E8(rwpcbCodeBuf, 0x00);
    } else if (rwullSize == 4) {
        if (rwullIsSigned) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x63); }
        else { E8(rwpcbCodeBuf, 0x8B); }
        E8(rwpcbCodeBuf, 0x00);
    } else {
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8B); E8(rwpcbCodeBuf, 0x00);
    }
}

static void GenStore(CodeBuf* rwpcbCodeBuf, U64 rwullSize) {
    if (rwullSize == 1) { E8(rwpcbCodeBuf, 0x88); E8(rwpcbCodeBuf, 0x01); }
    else if (rwullSize == 2) { E8(rwpcbCodeBuf, 0x66); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0x01); }
    else if (rwullSize == 4) { E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0x01); }
    else { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0x01); }
}

static void GenLValue(CodeBuf* rwpcbCodeBuf, Node* rwpnNode) {
    if (rwpnNode->rwullType == NODE_VAR) {
        Symbol* rwpsSymbol = SymFindLocal(rwpnNode->rwszName);
        if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnNode->rwszName);
        if (!rwpsSymbol) return;
        GenVarAddr(rwpcbCodeBuf, rwpsSymbol);
    } else if (rwpnNode->rwullType == NODE_INDEX) {
        Node* rwpnBase = rwpnNode->rwpnLeft;
        while (rwpnBase && rwpnBase->rwullType == NODE_INDEX) rwpnBase = rwpnBase->rwpnLeft;
        Symbol* rwpsBase = NULL;
        if (rwpnBase && rwpnBase->rwullType == NODE_VAR) {
            rwpsBase = SymFindLocal(rwpnBase->rwszName);
            if (!rwpsBase) rwpsBase = SymFindGlobal(rwpnBase->rwszName);
        }

        int rwullIsPtr = rwpsBase && rwpsBase->rwullPtrDepth > 0 && rwpsBase->rwullArraySize == 0;

        if (rwullIsPtr) {
            GenVarAddr(rwpcbCodeBuf, rwpsBase);
            GenLoad(rwpcbCodeBuf, 8, 0);
        } else {
            GenLValue(rwpcbCodeBuf, rwpnNode->rwpnLeft);
        }
        PushRax(rwpcbCodeBuf);
        GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight);
        PopRcx(rwpcbCodeBuf);

        U64 rwullElemSize = 8;
        if (rwpsBase) {
            if (rwpsBase->rwullPtrDepth > 0) {
                if (rwpsBase->rwullStructId && rwpsBase->rwszStructName)
                    rwullElemSize = StructSize(rwpsBase->rwszStructName);
                else {
                    rwullElemSize = TypeSize(rwpsBase->rwullVarType);
                    if (rwullElemSize == 0) rwullElemSize = 8;
                }
            } else {
                rwullElemSize = SymbolElemSize(rwpsBase);
            }
        }
        U64 rwullShift = Log2Ceil(rwullElemSize);
        if (rwullShift) ShlRaxImm(rwpcbCodeBuf, (U8)rwullShift);
        AddRaxRcx(rwpcbCodeBuf);
    } else if (rwpnNode->rwullType == NODE_DEREF) {
        GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft);
    } else if (rwpnNode->rwullType == NODE_STRUCT_ACCESS || rwpnNode->rwullType == NODE_STRUCT_PTR_ACCESS) {
        if (rwpnNode->rwullType == NODE_STRUCT_PTR_ACCESS) {
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft);
        } else {
            GenLValue(rwpcbCodeBuf, rwpnNode->rwpnLeft);
        }
        U64 rwullOffset = StructFieldOffsetRecursive(rwpnNode);
        if (rwullOffset) {
            PushRax(rwpcbCodeBuf);
            MovRaxImm(rwpcbCodeBuf, rwullOffset);
            PopRcx(rwpcbCodeBuf);
            AddRaxRcx(rwpcbCodeBuf);
        }
    }
}

static void GenCopyRaxRcx(CodeBuf* rwpcbCodeBuf, U64 rwullBytes) {
    U64 rwull8 = rwullBytes / 8;
    U64 rwull1 = rwullBytes % 8;
    for (U64 i = 0; i < rwull8; i++) {
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8B); E8(rwpcbCodeBuf, 0x01);
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0x00);
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x83); E8(rwpcbCodeBuf, 0xC1); E8(rwpcbCodeBuf, 0x08);
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x83); E8(rwpcbCodeBuf, 0xC0); E8(rwpcbCodeBuf, 0x08);
    }
    for (U64 i = 0; i < rwull1; i++) {
        E8(rwpcbCodeBuf, 0x8A); E8(rwpcbCodeBuf, 0x01);
        E8(rwpcbCodeBuf, 0x88); E8(rwpcbCodeBuf, 0x00);
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0xFF); E8(rwpcbCodeBuf, 0xC1);
        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0xFF); E8(rwpcbCodeBuf, 0xC0);
    }
}

static U64 LValueTypeSize(Node* rwpnNode) {
    if (rwpnNode->rwullType == NODE_VAR) {
        Symbol* rwpsSymbol = SymFindLocal(rwpnNode->rwszName);
        if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnNode->rwszName);
        if (rwpsSymbol) {
            if (rwpsSymbol->rwullPtrDepth > 0) return 8;
            if (rwpsSymbol->rwullStructId && rwpsSymbol->rwszStructName)
                return StructSize(rwpsSymbol->rwszStructName);
            return SymbolElemSize(rwpsSymbol);
        }
    } else if (rwpnNode->rwullType == NODE_INDEX) {
        if (rwpnNode->rwpnLeft->rwullType == NODE_VAR) {
            Symbol* rwpsSymbol = SymFindLocal(rwpnNode->rwpnLeft->rwszName);
            if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnNode->rwpnLeft->rwszName);
            if (rwpsSymbol) {
                if (rwpsSymbol->rwullPtrDepth > 0) {
                    if (rwpsSymbol->rwullStructId && rwpsSymbol->rwszStructName)
                        return StructSize(rwpsSymbol->rwszStructName);
                    U64 rwullSz = TypeSize(rwpsSymbol->rwullVarType);
                    return rwullSz ? rwullSz : 8;
                }
                return SymbolElemSize(rwpsSymbol);
            }
        }
        if (rwpnNode->rwpnLeft->rwullType == NODE_STRUCT_ACCESS ||
            rwpnNode->rwpnLeft->rwullType == NODE_STRUCT_PTR_ACCESS) {
            StructField* rwpsfField = StructFieldOf(rwpnNode->rwpnLeft);
            if (rwpsfField) {
                if (rwpsfField->rwullPtrDepth) return 8;
                if (rwpsfField->rwullStructId && rwpsfField->rwszStructName)
                    return StructSize(rwpsfField->rwszStructName);
                U64 rwullSz = TypeSize(rwpsfField->rwullVarType);
                return rwullSz ? rwullSz : 8;
            }
        }
    } else if (rwpnNode->rwullType == NODE_DEREF) {
        Node* rwpnInner = rwpnNode->rwpnLeft;
        if (rwpnInner && rwpnInner->rwullType == NODE_VAR) {
            Symbol* rwpsSymbol = SymFindLocal(rwpnInner->rwszName);
            if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnInner->rwszName);
            if (rwpsSymbol && rwpsSymbol->rwullPtrDepth > 0) {
                if (rwpsSymbol->rwullStructId && rwpsSymbol->rwszStructName)
                    return StructSize(rwpsSymbol->rwszStructName);
                U64 rwullSz = TypeSize(rwpsSymbol->rwullVarType);
                return rwullSz ? rwullSz : 8;
            }
        }
        return 8;
    } else if (rwpnNode->rwullType == NODE_STRUCT_ACCESS || rwpnNode->rwullType == NODE_STRUCT_PTR_ACCESS) {
        StructField* rwpsfField = StructFieldOf(rwpnNode);
        if (rwpsfField) {
            if (rwpsfField->rwullPtrDepth) return 8;
            if (rwpsfField->rwullStructId && rwpsfField->rwszStructName)
                return StructSize(rwpsfField->rwszStructName);
            U64 rwullSz = TypeSize(rwpsfField->rwullVarType);
            return rwullSz ? rwullSz : 8;
        }
        return 8;
    }
    return 8;
}

static int LValueTypeSigned(Node* rwpnNode) {
    if (rwpnNode->rwullType == NODE_VAR) {
        Symbol* rwpsSymbol = SymFindLocal(rwpnNode->rwszName);
        if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnNode->rwszName);
        if (rwpsSymbol) return rwpsSymbol->rwullPtrDepth ? 0 : TypeSigned(rwpsSymbol->rwullVarType);
    } else if (rwpnNode->rwullType == NODE_INDEX) {
        if (rwpnNode->rwpnLeft->rwullType == NODE_VAR) {
            Symbol* rwpsSymbol = SymFindLocal(rwpnNode->rwpnLeft->rwszName);
            if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnNode->rwpnLeft->rwszName);
            if (rwpsSymbol) {
                if (rwpsSymbol->rwullPtrDepth > 0) return TypeSigned(rwpsSymbol->rwullVarType);
                return TypeSigned(rwpsSymbol->rwullVarType);
            }
        }
        if (rwpnNode->rwpnLeft->rwullType == NODE_STRUCT_ACCESS ||
            rwpnNode->rwpnLeft->rwullType == NODE_STRUCT_PTR_ACCESS) {
            StructField* rwpsfField = StructFieldOf(rwpnNode->rwpnLeft);
            if (rwpsfField) return TypeSigned(rwpsfField->rwullVarType);
        }
    } else if (rwpnNode->rwullType == NODE_DEREF) {
        Node* rwpnInner = rwpnNode->rwpnLeft;
        if (rwpnInner && rwpnInner->rwullType == NODE_VAR) {
            Symbol* rwpsSymbol = SymFindLocal(rwpnInner->rwszName);
            if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnInner->rwszName);
            if (rwpsSymbol && rwpsSymbol->rwullPtrDepth > 0)
                return TypeSigned(rwpsSymbol->rwullVarType);
        }
        return 0;
    } else if (rwpnNode->rwullType == NODE_STRUCT_ACCESS || rwpnNode->rwullType == NODE_STRUCT_PTR_ACCESS) {
        StructField* rwpsfField = StructFieldOf(rwpnNode);
        if (rwpsfField) return TypeSigned(rwpsfField->rwullVarType);
    }
    return 0;
}

static void ShiftRelocs(U64 rwullFrom, U64 rwullDelta) {
    Reloc* rwprlReloc = rwprlRelocs;
    while (rwprlReloc) { if (rwprlReloc->rwullPos >= rwullFrom) rwprlReloc->rwullPos += rwullDelta; rwprlReloc = rwprlReloc->rwptNext; }
    StrReloc* rwpsrStrReloc = rwpsrStrRelocs;
    while (rwpsrStrReloc) { if (rwpsrStrReloc->rwullPos >= rwullFrom) rwpsrStrReloc->rwullPos += rwullDelta; rwpsrStrReloc = rwpsrStrReloc->rwptNext; }
    LabelReloc* rwplrLabelReloc = rwplrLabelRelocs;
    while (rwplrLabelReloc) { if (rwplrLabelReloc->rwullPos >= rwullFrom) rwplrLabelReloc->rwullPos += rwullDelta; rwplrLabelReloc = rwplrLabelReloc->rwplrNext; }
    BreakFixup* rwpbfBreak = rwpbfBreakFixups;
    while (rwpbfBreak) { if (rwpbfBreak->rwullPos >= rwullFrom) rwpbfBreak->rwullPos += rwullDelta; rwpbfBreak = rwpbfBreak->rwpbfNext; }
    LabelInfo* rwpliLabel = rwpliLabels;
    while (rwpliLabel) { if (rwpliLabel->rwullAddr >= rwullFrom) rwpliLabel->rwullAddr += rwullDelta; rwpliLabel = rwpliLabel->rwptNext; }
    AsmReloc* rwparAsmReloc = rwparAsmRelocs;
    while (rwparAsmReloc) { if (rwparAsmReloc->rwullPos >= rwullFrom) rwparAsmReloc->rwullPos += rwullDelta; rwparAsmReloc = rwparAsmReloc->rwparNext; }
}

static int AsmOpWidth(U64 rwullOp) {
    if (rwullOp >= ASM_OP_MOVQ && rwullOp <= ASM_OP_MOVB) return 8 >> (rwullOp - ASM_OP_MOVQ);
    if (rwullOp >= ASM_OP_LEAQ && rwullOp <= ASM_OP_LEAB) return 8 >> (rwullOp - ASM_OP_LEAQ);
    if (rwullOp >= ASM_OP_PUSHQ && rwullOp <= ASM_OP_PUSHB) return 8 >> (rwullOp - ASM_OP_PUSHQ);
    if (rwullOp >= ASM_OP_POPQ && rwullOp <= ASM_OP_POPB) return 8 >> (rwullOp - ASM_OP_POPQ);
    if (rwullOp >= ASM_OP_XCHGQ && rwullOp <= ASM_OP_XCHGB) return 8 >> (rwullOp - ASM_OP_XCHGQ);
    if (rwullOp >= ASM_OP_ADDQ && rwullOp <= ASM_OP_ADDB) return 8 >> (rwullOp - ASM_OP_ADDQ);
    if (rwullOp >= ASM_OP_SUBQ && rwullOp <= ASM_OP_SUBB) return 8 >> (rwullOp - ASM_OP_SUBQ);
    if (rwullOp >= ASM_OP_IMULQ && rwullOp <= ASM_OP_IMULB) return 8 >> (rwullOp - ASM_OP_IMULQ);
    if (rwullOp >= ASM_OP_MULQ && rwullOp <= ASM_OP_MULB) return 8 >> (rwullOp - ASM_OP_MULQ);
    if (rwullOp >= ASM_OP_IDIVQ && rwullOp <= ASM_OP_IDIVB) return 8 >> (rwullOp - ASM_OP_IDIVQ);
    if (rwullOp >= ASM_OP_DIVQ && rwullOp <= ASM_OP_DIVB) return 8 >> (rwullOp - ASM_OP_DIVQ);
    if (rwullOp >= ASM_OP_INCQ && rwullOp <= ASM_OP_INCB) return 8 >> (rwullOp - ASM_OP_INCQ);
    if (rwullOp >= ASM_OP_DECQ && rwullOp <= ASM_OP_DECB) return 8 >> (rwullOp - ASM_OP_DECQ);
    if (rwullOp >= ASM_OP_NEGQ && rwullOp <= ASM_OP_NEGB) return 8 >> (rwullOp - ASM_OP_NEGQ);
    if (rwullOp >= ASM_OP_CMPQ && rwullOp <= ASM_OP_CMPB) return 8 >> (rwullOp - ASM_OP_CMPQ);
    if (rwullOp >= ASM_OP_TESTQ && rwullOp <= ASM_OP_TESTB) return 8 >> (rwullOp - ASM_OP_TESTQ);
    if (rwullOp >= ASM_OP_ANDQ && rwullOp <= ASM_OP_ANDB) return 8 >> (rwullOp - ASM_OP_ANDQ);
    if (rwullOp >= ASM_OP_ORQ && rwullOp <= ASM_OP_ORB) return 8 >> (rwullOp - ASM_OP_ORQ);
    if (rwullOp >= ASM_OP_XORQ && rwullOp <= ASM_OP_XORB) return 8 >> (rwullOp - ASM_OP_XORQ);
    if (rwullOp >= ASM_OP_NOTQ && rwullOp <= ASM_OP_NOTB) return 8 >> (rwullOp - ASM_OP_NOTQ);
    if (rwullOp >= ASM_OP_SHLQ && rwullOp <= ASM_OP_SHLB) return 8 >> (rwullOp - ASM_OP_SHLQ);
    if (rwullOp >= ASM_OP_SHRQ && rwullOp <= ASM_OP_SHRB) return 8 >> (rwullOp - ASM_OP_SHRQ);
    if (rwullOp >= ASM_OP_SARQ && rwullOp <= ASM_OP_SARB) return 8 >> (rwullOp - ASM_OP_SARQ);
    if (rwullOp >= ASM_OP_SALQ && rwullOp <= ASM_OP_SALB) return 8 >> (rwullOp - ASM_OP_SALQ);
    if (rwullOp >= ASM_OP_BTQ && rwullOp <= ASM_OP_BTB) return 8 >> (rwullOp - ASM_OP_BTQ);
    if (rwullOp >= ASM_OP_BTSQ && rwullOp <= ASM_OP_BTSB) return 8 >> (rwullOp - ASM_OP_BTSQ);
    if (rwullOp >= ASM_OP_BTRQ && rwullOp <= ASM_OP_BTRB) return 8 >> (rwullOp - ASM_OP_BTRQ);
    if (rwullOp >= ASM_OP_BTCQ && rwullOp <= ASM_OP_BTCB) return 8 >> (rwullOp - ASM_OP_BTCQ);
    if (rwullOp >= ASM_OP_BSFQ && rwullOp <= ASM_OP_BSFB) return 8 >> (rwullOp - ASM_OP_BSFQ);
    if (rwullOp >= ASM_OP_BSRQ && rwullOp <= ASM_OP_BSRB) return 8 >> (rwullOp - ASM_OP_BSRQ);
    if (rwullOp >= ASM_OP_POPCNTQ && rwullOp <= ASM_OP_POPCNTB) return 8 >> (rwullOp - ASM_OP_POPCNTQ);
    if (rwullOp >= ASM_OP_LZCNTQ && rwullOp <= ASM_OP_LZCNTB) return 8 >> (rwullOp - ASM_OP_LZCNTQ);
    if (rwullOp >= ASM_OP_TZCNTQ && rwullOp <= ASM_OP_TZCNTB) return 8 >> (rwullOp - ASM_OP_TZCNTQ);
    if (rwullOp >= ASM_OP_XADDQ && rwullOp <= ASM_OP_XADDB) return 8 >> (rwullOp - ASM_OP_XADDQ);
    if (rwullOp >= ASM_OP_CMPXCHGQ && rwullOp <= ASM_OP_CMPXCHGB) return 8 >> (rwullOp - ASM_OP_CMPXCHGQ);
    return 8;
}

static int AsmOpIsShift(U64 rwullOp) {
    return (rwullOp >= ASM_OP_SHLQ && rwullOp <= ASM_OP_SALB);
}

static int AsmOpIsUnary(U64 rwullOp) {
    if (rwullOp >= ASM_OP_INCQ && rwullOp <= ASM_OP_DECB) return 1;
    if (rwullOp >= ASM_OP_NEGQ && rwullOp <= ASM_OP_NOTB) return 1;
    if (rwullOp >= ASM_OP_MULQ && rwullOp <= ASM_OP_DIVB) return 1;
    if (rwullOp >= ASM_OP_IDIVQ && rwullOp <= ASM_OP_IDIVB) return 1;
    return 0;
}

static int AsmOpIsMov(U64 rwullOp) {
    return rwullOp >= ASM_OP_MOVQ && rwullOp <= ASM_OP_MOVB;
}

static int AsmOpIsLea(U64 rwullOp) {
    return rwullOp >= ASM_OP_LEAQ && rwullOp <= ASM_OP_LEAB;
}

static int AsmOpIsPush(U64 rwullOp) {
    return rwullOp >= ASM_OP_PUSHQ && rwullOp <= ASM_OP_PUSHB;
}

static int AsmOpIsPop(U64 rwullOp) {
    return rwullOp >= ASM_OP_POPQ && rwullOp <= ASM_OP_POPB;
}

static int AsmOpIsAlu(U64 rwullOp) {
    if (rwullOp >= ASM_OP_ADDQ && rwullOp <= ASM_OP_ADDB) return 1;
    if (rwullOp >= ASM_OP_SUBQ && rwullOp <= ASM_OP_SUBB) return 1;
    if (rwullOp >= ASM_OP_CMPQ && rwullOp <= ASM_OP_CMPB) return 1;
    if (rwullOp >= ASM_OP_ANDQ && rwullOp <= ASM_OP_ANDB) return 1;
    if (rwullOp >= ASM_OP_ORQ && rwullOp <= ASM_OP_ORB) return 1;
    if (rwullOp >= ASM_OP_XORQ && rwullOp <= ASM_OP_XORB) return 1;
    if (rwullOp >= ASM_OP_TESTQ && rwullOp <= ASM_OP_TESTB) return 1;
    return 0;
}

static U64 AsmAluOpcode(U64 rwullOp) {
    if (rwullOp >= ASM_OP_ADDQ && rwullOp <= ASM_OP_ADDB) return 0;
    if (rwullOp >= ASM_OP_ORQ && rwullOp <= ASM_OP_ORB) return 1;
    if (rwullOp >= ASM_OP_ANDQ && rwullOp <= ASM_OP_ANDB) return 4;
    if (rwullOp >= ASM_OP_SUBQ && rwullOp <= ASM_OP_SUBB) return 5;
    if (rwullOp >= ASM_OP_XORQ && rwullOp <= ASM_OP_XORB) return 6;
    if (rwullOp >= ASM_OP_CMPQ && rwullOp <= ASM_OP_CMPB) return 7;
    return 0;
}

static U64 AsmShiftOpcode(U64 rwullOp) {
    if (rwullOp >= ASM_OP_SHLQ && rwullOp <= ASM_OP_SHLB) return 4;
    if (rwullOp >= ASM_OP_SHRQ && rwullOp <= ASM_OP_SHRB) return 5;
    if (rwullOp >= ASM_OP_SARQ && rwullOp <= ASM_OP_SARB) return 7;
    if (rwullOp >= ASM_OP_SALQ && rwullOp <= ASM_OP_SALB) return 4;
    return 4;
}

static int AsmOpIsJcc(U64 rwullOp) {
    return rwullOp >= ASM_OP_JMP && rwullOp <= ASM_OP_JRCXZ;
}

static U8 AsmJccOpcode(U64 rwullOp) {
    switch (rwullOp) {
        case ASM_OP_JZ: case ASM_OP_JE: return 0x84;
        case ASM_OP_JNZ: case ASM_OP_JNE: return 0x85;
        case ASM_OP_JL: return 0x8C;
        case ASM_OP_JG: return 0x8F;
        case ASM_OP_JLE: return 0x8E;
        case ASM_OP_JGE: return 0x8D;
        case ASM_OP_JB: return 0x82;
        case ASM_OP_JA: return 0x87;
        case ASM_OP_JBE: return 0x86;
        case ASM_OP_JAE: return 0x83;
        case ASM_OP_JC: return 0x82;
        case ASM_OP_JNC: return 0x83;
        case ASM_OP_JO: return 0x80;
        case ASM_OP_JNO: return 0x81;
        case ASM_OP_JS: return 0x88;
        case ASM_OP_JNS: return 0x89;
        case ASM_OP_JP: return 0x8A;
        case ASM_OP_JNP: return 0x8B;
    }
    return 0x84;
}

typedef struct {
    U64 rwullBaseReg;
    char* rwszBaseName;
    U64 rwullIdxReg;
    U64 rwullScale;
    U64 rwullHasDisp;
    S64 rwullDisp;
    char* rwszDispName;
} AsmMemInfo;

static int AsmMemNeedsSib(AsmMemInfo* rwpmemMem) {
    if (rwpmemMem->rwullIdxReg != ASM_REG_NONE) return 1;
    U64 rwullBase = rwpmemMem->rwullBaseReg;
    if (rwullBase == ASM_REG_NONE) return 1;
    if (rwullBase == ASM_REG_QSP || rwullBase == ASM_REG_Q12) return 1;
    if (rwullBase == ASM_REG_DSP || rwullBase == ASM_REG_Q12D) return 1;
    if (rwullBase == ASM_REG_WSP || rwullBase == ASM_REG_Q12W) return 1;
    if (rwullBase == ASM_REG_BSPL || rwullBase == ASM_REG_Q12B) return 1;
    return 0;
}

static int AsmMemNeedsDisp32(AsmMemInfo* rwpmemMem) {
    if (rwpmemMem->rwullBaseReg == ASM_REG_NONE) return 1;
    if (rwpmemMem->rwullBaseReg == ASM_REG_QBP || rwpmemMem->rwullBaseReg == ASM_REG_Q13) return 1;
    if (rwpmemMem->rwullBaseReg == ASM_REG_DBP || rwpmemMem->rwullBaseReg == ASM_REG_Q13D) return 1;
    if (rwpmemMem->rwullBaseReg == ASM_REG_WBP || rwpmemMem->rwullBaseReg == ASM_REG_Q13W) return 1;
    if (rwpmemMem->rwullBaseReg == ASM_REG_BBPL || rwpmemMem->rwullBaseReg == ASM_REG_Q13B) return 1;
    return 0;
}

static void AsmEncodeModRM(CodeBuf* rwpcbCodeBuf, U8 rwullMod, U8 rwullReg, U8 rwullRm) {
    E8(rwpcbCodeBuf, (U8)((rwullMod << 6) | ((rwullReg & 7) << 3) | (rwullRm & 7)));
}

static void AsmEmitRex(CodeBuf* rwpcbCodeBuf, int rwullW, int rwullR, int rwullX, int rwullB) {
    U8 rwullRex = 0x40;
    if (rwullW) rwullRex |= 0x08;
    if (rwullR) rwullRex |= 0x04;
    if (rwullX) rwullRex |= 0x02;
    if (rwullB) rwullRex |= 0x01;
    if (rwullRex != 0x40) E8(rwpcbCodeBuf, rwullRex);
}

static void AsmEmitMem(CodeBuf* rwpcbCodeBuf, int rwullRegField, AsmMemInfo* rwpmemMem, int rwullIs64) {
    int rwullRexR = (rwullRegField >> 3) & 1;
    int rwullRexX = 0;
    int rwullRexB = 0;

    if (rwpmemMem->rwullIdxReg != ASM_REG_NONE && AsmRegNeedsRex(rwpmemMem->rwullIdxReg)) rwullRexX = 1;
    if (rwpmemMem->rwullBaseReg != ASM_REG_NONE && AsmRegNeedsRex(rwpmemMem->rwullBaseReg)) rwullRexB = 1;

    AsmEmitRex(rwpcbCodeBuf, rwullIs64, rwullRexR, rwullRexX, rwullRexB);

    int rwullNeedSib = AsmMemNeedsSib(rwpmemMem);
    int rwullNeedDisp32 = AsmMemNeedsDisp32(rwpmemMem);

    U8 rwullMod;
    if (rwpmemMem->rwullBaseReg == ASM_REG_NONE) {
        rwullMod = 0;
    } else if (rwpmemMem->rwullHasDisp && rwpmemMem->rwullDisp != 0) {
        if (rwpmemMem->rwullDisp >= -128 && rwpmemMem->rwullDisp <= 127 && !rwullNeedDisp32)
            rwullMod = 1;
        else
            rwullMod = 2;
    } else if (rwpmemMem->rwullBaseReg == ASM_REG_QBP || rwpmemMem->rwullBaseReg == ASM_REG_Q13 ||
               rwpmemMem->rwullBaseReg == ASM_REG_DBP || rwpmemMem->rwullBaseReg == ASM_REG_Q13D ||
               rwpmemMem->rwullBaseReg == ASM_REG_WBP || rwpmemMem->rwullBaseReg == ASM_REG_Q13W ||
               rwpmemMem->rwullBaseReg == ASM_REG_BBPL || rwpmemMem->rwullBaseReg == ASM_REG_Q13B) {
        rwullMod = 1;
    } else {
        rwullMod = 0;
    }

    if (rwullNeedSib) {
        AsmEncodeModRM(rwpcbCodeBuf, rwullMod, (U8)rwullRegField, 4);
        U8 rwullSs = 0;
        U64 rwullScale = rwpmemMem->rwullScale;
        if (rwullScale == 2) rwullSs = 1;
        else if (rwullScale == 4) rwullSs = 2;
        else if (rwullScale == 8) rwullSs = 3;
        U8 rwullIdx = rwpmemMem->rwullIdxReg != ASM_REG_NONE ? (U8)AsmRegLow3(rwpmemMem->rwullIdxReg) : 4;
        U8 rwullBase = rwpmemMem->rwullBaseReg != ASM_REG_NONE ? (U8)AsmRegLow3(rwpmemMem->rwullBaseReg) : 5;
        E8(rwpcbCodeBuf, (U8)((rwullSs << 6) | ((rwullIdx & 7) << 3) | (rwullBase & 7)));
    } else {
        U8 rwullRm = rwpmemMem->rwullBaseReg != ASM_REG_NONE ? (U8)AsmRegLow3(rwpmemMem->rwullBaseReg) : 5;
        AsmEncodeModRM(rwpcbCodeBuf, rwullMod, (U8)rwullRegField, rwullRm);
    }

    if (rwullMod == 0 && rwpmemMem->rwullBaseReg == ASM_REG_NONE) {
        if (rwpmemMem->rwszDispName) {
            U64 rwullPos = rwpcbCodeBuf->rwullSize;
            E32(rwpcbCodeBuf, 0);
            AsmRelocAdd(rwullPos, rwpmemMem->rwszDispName);
        } else {
            E32(rwpcbCodeBuf, (U32)rwpmemMem->rwullDisp);
        }
    } else if (rwullMod == 1) {
        E8(rwpcbCodeBuf, (U8)(S8)rwpmemMem->rwullDisp);
    } else if (rwullMod == 2) {
        E32(rwpcbCodeBuf, (U32)(S32)rwpmemMem->rwullDisp);
    }
}

static void AsmNodeToMem(AsmMemInfo* rwpmemMem, Node* rwpnNode) {
    memset(rwpmemMem, 0, sizeof(AsmMemInfo));
    rwpmemMem->rwullBaseReg = ASM_REG_NONE;
    rwpmemMem->rwullIdxReg = ASM_REG_NONE;
    rwpmemMem->rwullScale = 1;

    if (rwpnNode->rwullAsmReg != ASM_REG_NONE) {
        rwpmemMem->rwullBaseReg = rwpnNode->rwullAsmReg;
    } else if (rwpnNode->rwszName) {
        Symbol* rwpsSymbol = SymFindLocal(rwpnNode->rwszName);
        if (rwpsSymbol) {
            rwpmemMem->rwullBaseReg = ASM_REG_QBP;
            rwpmemMem->rwullHasDisp = 1;
            if (rwpsSymbol->rwullIsParam)
                rwpmemMem->rwullDisp = (S64)rwpsSymbol->rwullOffset;
            else
                rwpmemMem->rwullDisp = -(S64)rwpsSymbol->rwullOffset;
        } else {
            rwpsSymbol = SymFindGlobal(rwpnNode->rwszName);
            if (rwpsSymbol) {
                rwpmemMem->rwullBaseReg = ASM_REG_NONE;
                rwpmemMem->rwullHasDisp = 1;
                rwpmemMem->rwszDispName = strdup(rwpsSymbol->rwszName);
            }
        }
    }

    if (rwpnNode->rwpnLeft) {
        if (rwpnNode->rwpnLeft->rwullAsmReg != ASM_REG_NONE) {
            rwpmemMem->rwullIdxReg = rwpnNode->rwpnLeft->rwullAsmReg;
        } else if (rwpnNode->rwpnLeft->rwszName) {
            Symbol* rwpsSymbol = SymFindLocal(rwpnNode->rwpnLeft->rwszName);
            if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnNode->rwpnLeft->rwszName);
            if (rwpsSymbol) {
                rwpmemMem->rwullBaseReg = ASM_REG_QBP;
                rwpmemMem->rwullHasDisp = 1;
                if (rwpsSymbol->rwullIsParam)
                    rwpmemMem->rwullDisp = (S64)rwpsSymbol->rwullOffset;
                else
                    rwpmemMem->rwullDisp = -(S64)rwpsSymbol->rwullOffset;
            }
        }
        rwpmemMem->rwullScale = rwpnNode->rwullAsmScale ? rwpnNode->rwullAsmScale : 1;
    }

    if (rwpnNode->rwpnThird) {
        rwpmemMem->rwullHasDisp = 1;
        rwpmemMem->rwullDisp += (S64)rwpnNode->rwpnThird->rwullValue;
    }
}

static void AsmEncodeMov(CodeBuf* rwpcbCodeBuf, U64 rwullOp, Node* rwpnDst, Node* rwpnSrc) {
    U64 rwullWidth = AsmOpWidth(rwullOp);

    if (rwpnDst->rwullType == NODE_ASM_REG && AsmRegIsCR(rwpnDst->rwullAsmReg)) {
        if (rwpnSrc->rwullType == NODE_ASM_REG) {
            U64 rwullSrc = rwpnSrc->rwullAsmReg;
            AsmEmitRex(rwpcbCodeBuf, 0, 1, 0, AsmRegNeedsRex(rwullSrc));
            E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x22);
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwpnDst->rwullAsmReg), (U8)AsmRegLow3(rwullSrc));
        }
        return;
    }
    if (rwpnDst->rwullType == NODE_ASM_REG && AsmRegIsDR(rwpnDst->rwullAsmReg)) {
        if (rwpnSrc->rwullType == NODE_ASM_REG) {
            U64 rwullSrc = rwpnSrc->rwullAsmReg;
            AsmEmitRex(rwpcbCodeBuf, 0, 1, 0, AsmRegNeedsRex(rwullSrc));
            E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x23);
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwpnDst->rwullAsmReg), (U8)AsmRegLow3(rwullSrc));
        }
        return;
    }
    if (rwpnSrc->rwullType == NODE_ASM_REG && AsmRegIsCR(rwpnSrc->rwullAsmReg)) {
        if (rwpnDst->rwullType == NODE_ASM_REG) {
            U64 rwullDst = rwpnDst->rwullAsmReg;
            AsmEmitRex(rwpcbCodeBuf, 0, 1, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x20);
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwpnSrc->rwullAsmReg), (U8)AsmRegLow3(rwullDst));
        }
        return;
    }
    if (rwpnSrc->rwullType == NODE_ASM_REG && AsmRegIsDR(rwpnSrc->rwullAsmReg)) {
        if (rwpnDst->rwullType == NODE_ASM_REG) {
            U64 rwullDst = rwpnDst->rwullAsmReg;
            AsmEmitRex(rwpcbCodeBuf, 0, 1, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x21);
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwpnSrc->rwullAsmReg), (U8)AsmRegLow3(rwullDst));
        }
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_REG && rwpnSrc->rwullType == NODE_ASM_IMM) {
        U64 rwullReg = rwpnDst->rwullAsmReg;
        U64 rwullImm = rwpnSrc->rwullValue;
        if (rwullWidth == 8) {
            AsmEmitRex(rwpcbCodeBuf, 1, 0, 0, AsmRegNeedsRex(rwullReg));
            E8(rwpcbCodeBuf, 0xB8 + (U8)AsmRegLow3(rwullReg));
            E64(rwpcbCodeBuf, rwullImm);
        } else if (rwullWidth == 4) {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullReg));
            E8(rwpcbCodeBuf, 0xB8 + (U8)AsmRegLow3(rwullReg));
            E32(rwpcbCodeBuf, (U32)rwullImm);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullReg));
            E8(rwpcbCodeBuf, 0xB8 + (U8)AsmRegLow3(rwullReg));
            E16(rwpcbCodeBuf, (U16)rwullImm);
        } else {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullReg));
            E8(rwpcbCodeBuf, 0xB0 + (U8)AsmRegLow3(rwullReg));
            E8(rwpcbCodeBuf, (U8)rwullImm);
        }
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_REG && rwpnSrc->rwullType == NODE_ASM_REG) {
        U64 rwullDst = rwpnDst->rwullAsmReg;
        U64 rwullSrc = rwpnSrc->rwullAsmReg;
        if (rwullWidth == 8) {
            AsmEmitRex(rwpcbCodeBuf, 1, AsmRegNeedsRex(rwullSrc), 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x89);
        } else if (rwullWidth == 4) {
            AsmEmitRex(rwpcbCodeBuf, 0, AsmRegNeedsRex(rwullSrc), 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x89);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            AsmEmitRex(rwpcbCodeBuf, 0, AsmRegNeedsRex(rwullSrc), 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x89);
        } else {
            AsmEmitRex(rwpcbCodeBuf, 0, AsmRegNeedsRex(rwullSrc), 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x88);
        }
        AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwullSrc), (U8)AsmRegLow3(rwullDst));
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_REG && rwpnSrc->rwullType == NODE_ASM_MEM) {
        U64 rwullDst = rwpnDst->rwullAsmReg;
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnSrc);
        if (rwullWidth == 8) {
            E8(rwpcbCodeBuf, 0x8B);
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 1);
        } else if (rwullWidth == 4) {
            E8(rwpcbCodeBuf, 0x8B);
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 0);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            E8(rwpcbCodeBuf, 0x8B);
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 0);
        } else {
            E8(rwpcbCodeBuf, 0x8A);
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 0);
        }
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_MEM && rwpnSrc->rwullType == NODE_ASM_REG) {
        U64 rwullSrc = rwpnSrc->rwullAsmReg;
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnDst);
        if (rwullWidth == 8) {
            E8(rwpcbCodeBuf, 0x89);
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullSrc), &rwpmemMem, 1);
        } else if (rwullWidth == 4) {
            E8(rwpcbCodeBuf, 0x89);
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullSrc), &rwpmemMem, 0);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            E8(rwpcbCodeBuf, 0x89);
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullSrc), &rwpmemMem, 0);
        } else {
            E8(rwpcbCodeBuf, 0x88);
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullSrc), &rwpmemMem, 0);
        }
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_MEM && rwpnSrc->rwullType == NODE_ASM_IMM) {
        U64 rwullImm = rwpnSrc->rwullValue;
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnDst);
        if (rwullWidth == 8) {
            E8(rwpcbCodeBuf, 0xC7);
            AsmEmitMem(rwpcbCodeBuf, 0, &rwpmemMem, 1);
            E32(rwpcbCodeBuf, (U32)(S32)rwullImm);
        } else if (rwullWidth == 4) {
            E8(rwpcbCodeBuf, 0xC7);
            AsmEmitMem(rwpcbCodeBuf, 0, &rwpmemMem, 0);
            E32(rwpcbCodeBuf, (U32)rwullImm);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            E8(rwpcbCodeBuf, 0xC7);
            AsmEmitMem(rwpcbCodeBuf, 0, &rwpmemMem, 0);
            E16(rwpcbCodeBuf, (U16)rwullImm);
        } else {
            E8(rwpcbCodeBuf, 0xC6);
            AsmEmitMem(rwpcbCodeBuf, 0, &rwpmemMem, 0);
            E8(rwpcbCodeBuf, (U8)rwullImm);
        }
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
        return;
    }
}

static void AsmEncodeAlu(CodeBuf* rwpcbCodeBuf, U64 rwullOp, Node* rwpnDst, Node* rwpnSrc) {
    U64 rwullWidth = AsmOpWidth(rwullOp);
    U64 rwullAluOp = AsmAluOpcode(rwullOp);

    if (rwpnDst->rwullType == NODE_ASM_REG && rwpnSrc->rwullType == NODE_ASM_REG) {
        U64 rwullDst = rwpnDst->rwullAsmReg;
        U64 rwullSrc = rwpnSrc->rwullAsmReg;
        if (rwullWidth == 8) {
            AsmEmitRex(rwpcbCodeBuf, 1, AsmRegNeedsRex(rwullSrc), 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, (U8)(0x01 | (rwullAluOp << 3)));
        } else if (rwullWidth == 4) {
            AsmEmitRex(rwpcbCodeBuf, 0, AsmRegNeedsRex(rwullSrc), 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, (U8)(0x01 | (rwullAluOp << 3)));
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            AsmEmitRex(rwpcbCodeBuf, 0, AsmRegNeedsRex(rwullSrc), 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, (U8)(0x01 | (rwullAluOp << 3)));
        } else {
            AsmEmitRex(rwpcbCodeBuf, 0, AsmRegNeedsRex(rwullSrc), 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, (U8)(0x00 | (rwullAluOp << 3)));
        }
        AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwullSrc), (U8)AsmRegLow3(rwullDst));
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_REG && rwpnSrc->rwullType == NODE_ASM_IMM) {
        U64 rwullDst = rwpnDst->rwullAsmReg;
        U64 rwullImm = rwpnSrc->rwullValue;
        if (rwullWidth == 8) {
            AsmEmitRex(rwpcbCodeBuf, 1, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x81);
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)rwullAluOp, (U8)AsmRegLow3(rwullDst));
            E32(rwpcbCodeBuf, (U32)(S32)rwullImm);
        } else if (rwullWidth == 4) {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x81);
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)rwullAluOp, (U8)AsmRegLow3(rwullDst));
            E32(rwpcbCodeBuf, (U32)rwullImm);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x81);
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)rwullAluOp, (U8)AsmRegLow3(rwullDst));
            E16(rwpcbCodeBuf, (U16)rwullImm);
        } else {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0x80);
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)rwullAluOp, (U8)AsmRegLow3(rwullDst));
            E8(rwpcbCodeBuf, (U8)rwullImm);
        }
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_REG && rwpnSrc->rwullType == NODE_ASM_MEM) {
        U64 rwullDst = rwpnDst->rwullAsmReg;
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnSrc);
        if (rwullWidth == 8) {
            E8(rwpcbCodeBuf, (U8)(0x03 | (rwullAluOp << 3)));
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 1);
        } else if (rwullWidth == 4) {
            E8(rwpcbCodeBuf, (U8)(0x03 | (rwullAluOp << 3)));
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 0);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            E8(rwpcbCodeBuf, (U8)(0x03 | (rwullAluOp << 3)));
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 0);
        } else {
            E8(rwpcbCodeBuf, (U8)(0x02 | (rwullAluOp << 3)));
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 0);
        }
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_MEM && rwpnSrc->rwullType == NODE_ASM_REG) {
        U64 rwullSrc = rwpnSrc->rwullAsmReg;
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnDst);
        if (rwullWidth == 8) {
            E8(rwpcbCodeBuf, (U8)(0x01 | (rwullAluOp << 3)));
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullSrc), &rwpmemMem, 1);
        } else if (rwullWidth == 4) {
            E8(rwpcbCodeBuf, (U8)(0x01 | (rwullAluOp << 3)));
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullSrc), &rwpmemMem, 0);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            E8(rwpcbCodeBuf, (U8)(0x01 | (rwullAluOp << 3)));
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullSrc), &rwpmemMem, 0);
        } else {
            E8(rwpcbCodeBuf, (U8)(0x00 | (rwullAluOp << 3)));
            AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullSrc), &rwpmemMem, 0);
        }
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
        return;
    }
}

static void AsmEncodeShift(CodeBuf* rwpcbCodeBuf, U64 rwullOp, Node* rwpnDst, Node* rwpnSrc) {
    U64 rwullWidth = AsmOpWidth(rwullOp);
    U64 rwullShiftOp = AsmShiftOpcode(rwullOp);

    if (rwpnDst->rwullType == NODE_ASM_REG && rwpnSrc->rwullType == NODE_ASM_IMM) {
        U64 rwullDst = rwpnDst->rwullAsmReg;
        U64 rwullImm = rwpnSrc->rwullValue;
        if (rwullWidth == 8) {
            AsmEmitRex(rwpcbCodeBuf, 1, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xC1);
        } else if (rwullWidth == 4) {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xC1);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xC1);
        } else {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xC0);
        }
        AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)rwullShiftOp, (U8)AsmRegLow3(rwullDst));
        E8(rwpcbCodeBuf, (U8)rwullImm);
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_REG && rwpnSrc->rwullType == NODE_ASM_REG) {
        U64 rwullDst = rwpnDst->rwullAsmReg;
        if (rwullWidth == 8) {
            AsmEmitRex(rwpcbCodeBuf, 1, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xD3);
        } else if (rwullWidth == 4) {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xD3);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xD3);
        } else {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xD2);
        }
        AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)rwullShiftOp, (U8)AsmRegLow3(rwullDst));
        return;
    }
}

static void AsmEncodeUnary(CodeBuf* rwpcbCodeBuf, U64 rwullOp, Node* rwpnDst) {
    U64 rwullWidth = AsmOpWidth(rwullOp);
    U64 rwullExt;

    if (rwullOp >= ASM_OP_INCQ && rwullOp <= ASM_OP_INCB) rwullExt = 0;
    else if (rwullOp >= ASM_OP_DECQ && rwullOp <= ASM_OP_DECB) rwullExt = 1;
    else if (rwullOp >= ASM_OP_NOTQ && rwullOp <= ASM_OP_NOTB) rwullExt = 2;
    else if (rwullOp >= ASM_OP_NEGQ && rwullOp <= ASM_OP_NEGB) rwullExt = 3;
    else if (rwullOp >= ASM_OP_MULQ && rwullOp <= ASM_OP_MULB) rwullExt = 4;
    else if (rwullOp >= ASM_OP_IMULQ && rwullOp <= ASM_OP_IMULB) rwullExt = 5;
    else if (rwullOp >= ASM_OP_DIVQ && rwullOp <= ASM_OP_DIVB) rwullExt = 6;
    else if (rwullOp >= ASM_OP_IDIVQ && rwullOp <= ASM_OP_IDIVB) rwullExt = 7;
    else return;

    if (rwpnDst->rwullType == NODE_ASM_REG) {
        U64 rwullDst = rwpnDst->rwullAsmReg;
        if (rwullWidth == 8) {
            AsmEmitRex(rwpcbCodeBuf, 1, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xF7);
        } else if (rwullWidth == 4) {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xF7);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xF7);
        } else {
            AsmEmitRex(rwpcbCodeBuf, 0, 0, 0, AsmRegNeedsRex(rwullDst));
            E8(rwpcbCodeBuf, 0xF6);
        }
        AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)rwullExt, (U8)AsmRegLow3(rwullDst));
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_MEM) {
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnDst);
        if (rwullWidth == 8) {
            E8(rwpcbCodeBuf, 0xF7);
            AsmEmitMem(rwpcbCodeBuf, (int)rwullExt, &rwpmemMem, 1);
        } else if (rwullWidth == 4) {
            E8(rwpcbCodeBuf, 0xF7);
            AsmEmitMem(rwpcbCodeBuf, (int)rwullExt, &rwpmemMem, 0);
        } else if (rwullWidth == 2) {
            E8(rwpcbCodeBuf, 0x66);
            E8(rwpcbCodeBuf, 0xF7);
            AsmEmitMem(rwpcbCodeBuf, (int)rwullExt, &rwpmemMem, 0);
        } else {
            E8(rwpcbCodeBuf, 0xF6);
            AsmEmitMem(rwpcbCodeBuf, (int)rwullExt, &rwpmemMem, 0);
        }
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
    }
}

static void AsmEncodePushPop(CodeBuf* rwpcbCodeBuf, U64 rwullOp, Node* rwpnDst) {
    int rwullIsPush = AsmOpIsPush(rwullOp);
    if (rwpnDst->rwullType == NODE_ASM_REG) {
        U64 rwullReg = rwpnDst->rwullAsmReg;
        if (AsmRegNeedsRex(rwullReg)) E8(rwpcbCodeBuf, 0x41);
        E8(rwpcbCodeBuf, (U8)((rwullIsPush ? 0x50 : 0x58) + (U8)AsmRegLow3(rwullReg)));
        return;
    }
    if (rwpnDst->rwullType == NODE_ASM_MEM) {
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnDst);
        E8(rwpcbCodeBuf, (U8)(rwullIsPush ? 0xFF : 0x8F));
        AsmEmitMem(rwpcbCodeBuf, rwullIsPush ? 6 : 0, &rwpmemMem, 0);
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
    }
}

static void AsmEncodeLea(CodeBuf* rwpcbCodeBuf, Node* rwpnDst, Node* rwpnSrc) {
    if (rwpnDst->rwullType != NODE_ASM_REG || rwpnSrc->rwullType != NODE_ASM_MEM) return;
    U64 rwullDst = rwpnDst->rwullAsmReg;
    AsmMemInfo rwpmemMem;
    AsmNodeToMem(&rwpmemMem, rwpnSrc);
    E8(rwpcbCodeBuf, 0x8D);
    AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 1);
    if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
}

static void AsmEncodeJmp(CodeBuf* rwpcbCodeBuf, U64 rwullOp, Node* rwpnDst) {
    if (rwullOp == ASM_OP_JECXZ || rwullOp == ASM_OP_JRCXZ) {
        if (rwullOp == ASM_OP_JRCXZ) E8(rwpcbCodeBuf, 0x48);
        E8(rwpcbCodeBuf, 0xE3);
        if (rwpnDst->rwullType == NODE_ASM_LABEL || (rwpnDst->rwullType == NODE_ASM_MEM && rwpnDst->rwszName)) {
            U64 rwullPos = rwpcbCodeBuf->rwullSize;
            E8(rwpcbCodeBuf, 0);
            LabelRelocAdd(rwullPos, rwpnDst->rwszName);
        } else if (rwpnDst->rwullType == NODE_ASM_IMM) {
            E8(rwpcbCodeBuf, (U8)rwpnDst->rwullValue);
        }
        return;
    }

    if (rwpnDst->rwullType == NODE_ASM_LABEL || (rwpnDst->rwullType == NODE_ASM_MEM && rwpnDst->rwszName)) {
        const char* rwszLabel = rwpnDst->rwszName;
        if (rwullOp == ASM_OP_JMP) {
            E8(rwpcbCodeBuf, 0xE9);
            U64 rwullPos = rwpcbCodeBuf->rwullSize;
            E32(rwpcbCodeBuf, 0);
            LabelRelocAdd(rwullPos, rwszLabel);
        } else {
            E8(rwpcbCodeBuf, 0x0F);
            E8(rwpcbCodeBuf, AsmJccOpcode(rwullOp));
            U64 rwullPos = rwpcbCodeBuf->rwullSize;
            E32(rwpcbCodeBuf, 0);
            LabelRelocAdd(rwullPos, rwszLabel);
        }
        return;
    }
    if (rwpnDst->rwullType == NODE_ASM_REG) {
        U64 rwullReg = rwpnDst->rwullAsmReg;
        if (AsmRegNeedsRex(rwullReg)) E8(rwpcbCodeBuf, 0x41);
        E8(rwpcbCodeBuf, 0xFF);
        AsmEncodeModRM(rwpcbCodeBuf, 3, 4, (U8)AsmRegLow3(rwullReg));
        return;
    }
    if (rwpnDst->rwullType == NODE_ASM_MEM) {
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnDst);
        E8(rwpcbCodeBuf, 0xFF);
        AsmEmitMem(rwpcbCodeBuf, 4, &rwpmemMem, 0);
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
    }
}

static void AsmEncodeCall(CodeBuf* rwpcbCodeBuf, Node* rwpnDst) {
    if (rwpnDst->rwullType == NODE_ASM_LABEL) {
        E8(rwpcbCodeBuf, 0xE8);
        U64 rwullPos = rwpcbCodeBuf->rwullSize;
        E32(rwpcbCodeBuf, 0);
        Symbol* rwpsSymbol = SymFindGlobal(rwpnDst->rwszName);
        if (rwpsSymbol && rwpsSymbol->rwullIsFunc) {
            RelocAdd(rwullPos, rwpnDst->rwszName);
        } else {
            LabelRelocAdd(rwullPos, rwpnDst->rwszName);
        }
        return;
    }
    if (rwpnDst->rwullType == NODE_ASM_REG) {
        U64 rwullReg = rwpnDst->rwullAsmReg;
        if (AsmRegNeedsRex(rwullReg)) E8(rwpcbCodeBuf, 0x41);
        E8(rwpcbCodeBuf, 0xFF);
        AsmEncodeModRM(rwpcbCodeBuf, 3, 2, (U8)AsmRegLow3(rwullReg));
        return;
    }
    if (rwpnDst->rwullType == NODE_ASM_MEM) {
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnDst);
        E8(rwpcbCodeBuf, 0xFF);
        AsmEmitMem(rwpcbCodeBuf, 2, &rwpmemMem, 0);
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
    }
}

static void AsmEncodeRep(CodeBuf* rwpcbCodeBuf, U64 rwullOp) {
    U8 rwullPrefix;
    U64 rwullWhich;
    U64 rwullSize;

    if (rwullOp >= ASM_OP_REP_MOVSB && rwullOp <= ASM_OP_REP_SCASQ) {
        rwullPrefix = 0xF3;
        rwullWhich = (rwullOp - ASM_OP_REP_MOVSB) / 4;
        rwullSize = (rwullOp - ASM_OP_REP_MOVSB) % 4;
    } else if (rwullOp >= ASM_OP_REPNE_MOVSB && rwullOp <= ASM_OP_REPNE_SCASQ) {
        rwullPrefix = 0xF2;
        rwullWhich = (rwullOp - ASM_OP_REPNE_MOVSB) / 4;
        rwullSize = (rwullOp - ASM_OP_REPNE_MOVSB) % 4;
    } else {
        rwullPrefix = 0xF3;
        rwullWhich = (rwullOp - ASM_OP_REPZ_MOVSB) / 4;
        rwullSize = (rwullOp - ASM_OP_REPZ_MOVSB) % 4;
    }

    E8(rwpcbCodeBuf, rwullPrefix);
    if (rwullSize == 3) E8(rwpcbCodeBuf, 0x48);
    if (rwullSize == 1) E8(rwpcbCodeBuf, 0x66);

    U8 rwullOpcode;
    if (rwullWhich == 0) rwullOpcode = (rwullSize == 0) ? 0xA4 : 0xA5;
    else if (rwullWhich == 1) rwullOpcode = (rwullSize == 0) ? 0xAA : 0xAB;
    else if (rwullWhich == 2) rwullOpcode = (rwullSize == 0) ? 0xAC : 0xAD;
    else if (rwullWhich == 3) rwullOpcode = (rwullSize == 0) ? 0xA6 : 0xA7;
    else rwullOpcode = (rwullSize == 0) ? 0xAE : 0xAF;

    E8(rwpcbCodeBuf, rwullOpcode);
}

static void AsmEncodeSse(CodeBuf* rwpcbCodeBuf, U64 rwullOp, Node* rwpnArg0, Node* rwpnArg1) {
    U8 rwullPrefix = 0;
    U8 rwullOpcode = 0;

    switch (rwullOp) {
        case ASM_OP_SSE_MOVAPS: rwullOpcode = 0x28; break;
        case ASM_OP_SSE_MOVUPS: rwullOpcode = 0x10; break;
        case ASM_OP_SSE_MOVSS: rwullPrefix = 0xF3; rwullOpcode = 0x10; break;
        case ASM_OP_SSE_MOVSD: rwullPrefix = 0xF2; rwullOpcode = 0x10; break;
        case ASM_OP_SSE_MOVDQA: rwullPrefix = 0x66; rwullOpcode = 0x6F; break;
        case ASM_OP_SSE_MOVDQU: rwullPrefix = 0xF3; rwullOpcode = 0x6F; break;
        case ASM_OP_SSE_ADDPS: rwullOpcode = 0x58; break;
        case ASM_OP_SSE_ADDPD: rwullPrefix = 0x66; rwullOpcode = 0x58; break;
        case ASM_OP_SSE_ADDSS: rwullPrefix = 0xF3; rwullOpcode = 0x58; break;
        case ASM_OP_SSE_ADDSD: rwullPrefix = 0xF2; rwullOpcode = 0x58; break;
        case ASM_OP_SSE_SUBPS: rwullOpcode = 0x5C; break;
        case ASM_OP_SSE_SUBPD: rwullPrefix = 0x66; rwullOpcode = 0x5C; break;
        case ASM_OP_SSE_SUBSS: rwullPrefix = 0xF3; rwullOpcode = 0x5C; break;
        case ASM_OP_SSE_SUBSD: rwullPrefix = 0xF2; rwullOpcode = 0x5C; break;
        case ASM_OP_SSE_MULPS: rwullOpcode = 0x59; break;
        case ASM_OP_SSE_MULPD: rwullPrefix = 0x66; rwullOpcode = 0x59; break;
        case ASM_OP_SSE_MULSS: rwullPrefix = 0xF3; rwullOpcode = 0x59; break;
        case ASM_OP_SSE_MULSD: rwullPrefix = 0xF2; rwullOpcode = 0x59; break;
        case ASM_OP_SSE_DIVPS: rwullOpcode = 0x5E; break;
        case ASM_OP_SSE_DIVPD: rwullPrefix = 0x66; rwullOpcode = 0x5E; break;
        case ASM_OP_SSE_DIVSS: rwullPrefix = 0xF3; rwullOpcode = 0x5E; break;
        case ASM_OP_SSE_DIVSD: rwullPrefix = 0xF2; rwullOpcode = 0x5E; break;
        case ASM_OP_SSE_SQRTPS: rwullOpcode = 0x51; break;
        case ASM_OP_SSE_SQRTPD: rwullPrefix = 0x66; rwullOpcode = 0x51; break;
        case ASM_OP_SSE_SQRTSS: rwullPrefix = 0xF3; rwullOpcode = 0x51; break;
        case ASM_OP_SSE_SQRTSD: rwullPrefix = 0xF2; rwullOpcode = 0x51; break;
        case ASM_OP_SSE_MINPS: rwullOpcode = 0x5D; break;
        case ASM_OP_SSE_MINPD: rwullPrefix = 0x66; rwullOpcode = 0x5D; break;
        case ASM_OP_SSE_MAXPS: rwullOpcode = 0x5F; break;
        case ASM_OP_SSE_MAXPD: rwullPrefix = 0x66; rwullOpcode = 0x5F; break;
        case ASM_OP_SSE_ANDPS: rwullOpcode = 0x54; break;
        case ASM_OP_SSE_ANDPD: rwullPrefix = 0x66; rwullOpcode = 0x54; break;
        case ASM_OP_SSE_ORPS: rwullOpcode = 0x56; break;
        case ASM_OP_SSE_ORPD: rwullPrefix = 0x66; rwullOpcode = 0x56; break;
        case ASM_OP_SSE_XORPS: rwullOpcode = 0x57; break;
        case ASM_OP_SSE_XORPD: rwullPrefix = 0x66; rwullOpcode = 0x57; break;
        case ASM_OP_SSE_PXOR: rwullPrefix = 0x66; rwullOpcode = 0xEF; break;
        case ASM_OP_SSE_PAND: rwullPrefix = 0x66; rwullOpcode = 0xDB; break;
        case ASM_OP_SSE_POR: rwullPrefix = 0x66; rwullOpcode = 0xEB; break;
        case ASM_OP_SSE_PANDN: rwullPrefix = 0x66; rwullOpcode = 0xDF; break;
        case ASM_OP_SSE_CMPPS: rwullOpcode = 0xC2; break;
        case ASM_OP_SSE_CMPPD: rwullPrefix = 0x66; rwullOpcode = 0xC2; break;
        case ASM_OP_SSE_CMPSS: rwullPrefix = 0xF3; rwullOpcode = 0xC2; break;
        case ASM_OP_SSE_CMPSD: rwullPrefix = 0xF2; rwullOpcode = 0xC2; break;
        case ASM_OP_SSE_COMISS: rwullOpcode = 0x2F; break;
        case ASM_OP_SSE_COMISD: rwullPrefix = 0x66; rwullOpcode = 0x2F; break;
        case ASM_OP_SSE_UCOMISS: rwullOpcode = 0x2E; break;
        case ASM_OP_SSE_UCOMISD: rwullPrefix = 0x66; rwullOpcode = 0x2E; break;
        case ASM_OP_SSE_CVTSI2SS: rwullPrefix = 0xF3; rwullOpcode = 0x2A; break;
        case ASM_OP_SSE_CVTSI2SD: rwullPrefix = 0xF2; rwullOpcode = 0x2A; break;
        case ASM_OP_SSE_CVTSS2SI: rwullPrefix = 0xF3; rwullOpcode = 0x2D; break;
        case ASM_OP_SSE_CVTSD2SI: rwullPrefix = 0xF2; rwullOpcode = 0x2D; break;
        case ASM_OP_SSE_CVTPS2PD: rwullOpcode = 0x5A; break;
        case ASM_OP_SSE_CVTPD2PS: rwullPrefix = 0x66; rwullOpcode = 0x5A; break;
        case ASM_OP_SSE_CVTSS2SD: rwullPrefix = 0xF3; rwullOpcode = 0x5A; break;
        case ASM_OP_SSE_CVTSD2SS: rwullPrefix = 0xF2; rwullOpcode = 0x5A; break;
        default: return;
    }

    if (rwullPrefix) E8(rwpcbCodeBuf, rwullPrefix);

    if (rwpnArg0->rwullType == NODE_ASM_REG && rwpnArg1->rwullType == NODE_ASM_REG) {
        U64 rwullDst = rwpnArg0->rwullAsmReg;
        U64 rwullSrc = rwpnArg1->rwullAsmReg;
        AsmEmitRex(rwpcbCodeBuf, 0, AsmRegNeedsRex(rwullDst), 0, AsmRegNeedsRex(rwullSrc));
        E8(rwpcbCodeBuf, 0x0F);
        E8(rwpcbCodeBuf, rwullOpcode);
        AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwullDst), (U8)AsmRegLow3(rwullSrc));
    } else if (rwpnArg0->rwullType == NODE_ASM_REG && rwpnArg1->rwullType == NODE_ASM_MEM) {
        U64 rwullDst = rwpnArg0->rwullAsmReg;
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnArg1);
        E8(rwpcbCodeBuf, 0x0F);
        E8(rwpcbCodeBuf, rwullOpcode);
        AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwullDst), &rwpmemMem, 0);
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
    }
}

static void AsmEncodeAvx(CodeBuf* rwpcbCodeBuf, U64 rwullOp, Node* rwpnArg0, Node* rwpnArg1, Node* rwpnArg2) {
    U8 rwullOpcode = 0;
    U8 rwullPp = 0;
    U8 rwullMap = 0x0F;
    U8 rwullW = 0;

    switch (rwullOp) {
        case ASM_OP_AVX_VMOVAPS: rwullOpcode = 0x28; break;
        case ASM_OP_AVX_VMOVUPS: rwullOpcode = 0x10; break;
        case ASM_OP_AVX_VMOVSS: rwullOpcode = 0x10; rwullPp = 2; break;
        case ASM_OP_AVX_VMOVSD: rwullOpcode = 0x10; rwullPp = 3; break;
        case ASM_OP_AVX_VMOVDQA: rwullOpcode = 0x6F; rwullPp = 1; break;
        case ASM_OP_AVX_VMOVDQU: rwullOpcode = 0x6F; rwullPp = 2; break;
        case ASM_OP_AVX_VADDPS: rwullOpcode = 0x58; break;
        case ASM_OP_AVX_VADDPD: rwullOpcode = 0x58; rwullPp = 1; break;
        case ASM_OP_AVX_VADDSS: rwullOpcode = 0x58; rwullPp = 2; break;
        case ASM_OP_AVX_VADDSD: rwullOpcode = 0x58; rwullPp = 3; break;
        case ASM_OP_AVX_VSUBPS: rwullOpcode = 0x5C; break;
        case ASM_OP_AVX_VSUBPD: rwullOpcode = 0x5C; rwullPp = 1; break;
        case ASM_OP_AVX_VSUBSS: rwullOpcode = 0x5C; rwullPp = 2; break;
        case ASM_OP_AVX_VSUBSD: rwullOpcode = 0x5C; rwullPp = 3; break;
        case ASM_OP_AVX_VMULPS: rwullOpcode = 0x59; break;
        case ASM_OP_AVX_VMULPD: rwullOpcode = 0x59; rwullPp = 1; break;
        case ASM_OP_AVX_VMULSS: rwullOpcode = 0x59; rwullPp = 2; break;
        case ASM_OP_AVX_VMULSD: rwullOpcode = 0x59; rwullPp = 3; break;
        case ASM_OP_AVX_VDIVPS: rwullOpcode = 0x5E; break;
        case ASM_OP_AVX_VDIVPD: rwullOpcode = 0x5E; rwullPp = 1; break;
        case ASM_OP_AVX_VDIVSS: rwullOpcode = 0x5E; rwullPp = 2; break;
        case ASM_OP_AVX_VDIVSD: rwullOpcode = 0x5E; rwullPp = 3; break;
        case ASM_OP_AVX_VSQRTPS: rwullOpcode = 0x51; break;
        case ASM_OP_AVX_VSQRTPD: rwullOpcode = 0x51; rwullPp = 1; break;
        case ASM_OP_AVX_VANDPS: rwullOpcode = 0x54; break;
        case ASM_OP_AVX_VANDPD: rwullOpcode = 0x54; rwullPp = 1; break;
        case ASM_OP_AVX_VORPS: rwullOpcode = 0x56; break;
        case ASM_OP_AVX_VORPD: rwullOpcode = 0x56; rwullPp = 1; break;
        case ASM_OP_AVX_VXORPS: rwullOpcode = 0x57; break;
        case ASM_OP_AVX_VXORPD: rwullOpcode = 0x57; rwullPp = 1; break;
        case ASM_OP_AVX_VPXOR: rwullOpcode = 0xEF; rwullPp = 1; break;
        case ASM_OP_AVX_VPAND: rwullOpcode = 0xDB; rwullPp = 1; break;
        case ASM_OP_AVX_VPOR: rwullOpcode = 0xEB; rwullPp = 1; break;
        case ASM_OP_AVX_VZEROUPPER: E8(rwpcbCodeBuf, 0xC5); E8(rwpcbCodeBuf, 0xF8); E8(rwpcbCodeBuf, 0x77); return;
        case ASM_OP_AVX_VZEROALL: E8(rwpcbCodeBuf, 0xC5); E8(rwpcbCodeBuf, 0xFC); E8(rwpcbCodeBuf, 0x77); return;
        default: return;
    }

    E8(rwpcbCodeBuf, 0xC4);

    int rwullR = 0, rwullX = 0, rwullB = 0;
    U8 rwullVVVV = 0;

    if (rwpnArg0->rwullType == NODE_ASM_REG) {
        rwullR = AsmRegNeedsRex(rwpnArg0->rwullAsmReg);
    }
    if (rwpnArg1->rwullType == NODE_ASM_REG) {
        rwullVVVV = (U8)(15 - AsmRegLow3(rwpnArg1->rwullAsmReg));
    }
    if (rwpnArg1->rwullType == NODE_ASM_MEM) {
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnArg1);
        if (rwpmemMem.rwullBaseReg != ASM_REG_NONE) rwullB = AsmRegNeedsRex(rwpmemMem.rwullBaseReg);
        if (rwpmemMem.rwullIdxReg != ASM_REG_NONE) rwullX = AsmRegNeedsRex(rwpmemMem.rwullIdxReg);
    }

    U8 rwullByte2 = (U8)(((~rwullR & 1) << 7) | ((~rwullX & 1) << 6) | ((~rwullB & 1) << 5) | (rwullMap & 0x1F));
    U8 rwullByte3 = (U8)((rwullW << 7) | ((rwullVVVV & 0xF) << 3) | (rwullPp & 0x3));
    E8(rwpcbCodeBuf, rwullByte2);
    E8(rwpcbCodeBuf, rwullByte3);
    E8(rwpcbCodeBuf, rwullOpcode);

    if (rwpnArg2 && rwpnArg2->rwullType == NODE_ASM_REG) {
        U64 rwullSrc2 = rwpnArg2->rwullAsmReg;
        AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwpnArg1->rwullAsmReg), (U8)AsmRegLow3(rwullSrc2));
    } else if (rwpnArg2 && rwpnArg2->rwullType == NODE_ASM_MEM) {
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnArg2);
        AsmEmitMem(rwpcbCodeBuf, (int)AsmRegLow3(rwpnArg1->rwullAsmReg), &rwpmemMem, 0);
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
    } else if (rwpnArg1 && rwpnArg1->rwullType == NODE_ASM_REG) {
        AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwpnArg0->rwullAsmReg), (U8)AsmRegLow3(rwpnArg1->rwullAsmReg));
    }
}

static void AsmEncode(CodeBuf* rwpcbCodeBuf, Node* rwpnAsm) {
    U64 rwullOp = rwpnAsm->rwullAsmOp;
    Node* rwpnArg0 = rwpnAsm->rwpnArgs;
    Node* rwpnArg1 = rwpnArg0 ? rwpnArg0->rwpnNext : NULL;
    Node* rwpnArg2 = rwpnArg1 ? rwpnArg1->rwpnNext : NULL;

    if (rwullOp >= ASM_OP_REP_MOVSB && rwullOp <= ASM_OP_REPZ_SCASQ) {
        AsmEncodeRep(rwpcbCodeBuf, rwullOp);
        return;
    }

    if (rwullOp == ASM_OP_NOP) { E8(rwpcbCodeBuf, 0x90); return; }
    if (rwullOp == ASM_OP_HLT) { E8(rwpcbCodeBuf, 0xF4); return; }
    if (rwullOp == ASM_OP_RET) { E8(rwpcbCodeBuf, 0xC3); return; }
    if (rwullOp == ASM_OP_CPUID) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xA2); return; }
    if (rwullOp == ASM_OP_CLI) { E8(rwpcbCodeBuf, 0xFA); return; }
    if (rwullOp == ASM_OP_STI) { E8(rwpcbCodeBuf, 0xFB); return; }
    if (rwullOp == ASM_OP_IRETQ) { E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0xCF); return; }
    if (rwullOp == ASM_OP_WBINVD) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x09); return; }
    if (rwullOp == ASM_OP_RDMSR) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x32); return; }
    if (rwullOp == ASM_OP_WRMSR) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x30); return; }
    if (rwullOp == ASM_OP_RDTSC) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x31); return; }
    if (rwullOp == ASM_OP_RDTSCP) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x01); E8(rwpcbCodeBuf, 0xF9); return; }
    if (rwullOp == ASM_OP_SWAPGS) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x01); E8(rwpcbCodeBuf, 0xF8); return; }
    if (rwullOp == ASM_OP_PAUSE) { E8(rwpcbCodeBuf, 0xF3); E8(rwpcbCodeBuf, 0x90); return; }
    if (rwullOp == ASM_OP_MFENCE) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xAE); E8(rwpcbCodeBuf, 0xF0); return; }
    if (rwullOp == ASM_OP_LFENCE) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xAE); E8(rwpcbCodeBuf, 0xE8); return; }
    if (rwullOp == ASM_OP_SFENCE) { E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0xAE); E8(rwpcbCodeBuf, 0xF8); return; }
    if (rwullOp == ASM_OP_INT) {
        E8(rwpcbCodeBuf, 0xCD);
        E8(rwpcbCodeBuf, (U8)rwpnArg0->rwullValue);
        return;
    }

    if (rwullOp == ASM_OP_INB) {
        if (rwpnArg1->rwullType == NODE_ASM_REG) { E8(rwpcbCodeBuf, 0xEC); }
        else { E8(rwpcbCodeBuf, 0xE4); E8(rwpcbCodeBuf, (U8)rwpnArg1->rwullValue); }
        return;
    }
    if (rwullOp == ASM_OP_INW) {
        E8(rwpcbCodeBuf, 0x66);
        if (rwpnArg1->rwullType == NODE_ASM_REG) { E8(rwpcbCodeBuf, 0xED); }
        else { E8(rwpcbCodeBuf, 0xE5); E8(rwpcbCodeBuf, (U8)rwpnArg1->rwullValue); }
        return;
    }
    if (rwullOp == ASM_OP_IND) {
        if (rwpnArg1->rwullType == NODE_ASM_REG) { E8(rwpcbCodeBuf, 0xED); }
        else { E8(rwpcbCodeBuf, 0xE5); E8(rwpcbCodeBuf, (U8)rwpnArg1->rwullValue); }
        return;
    }
    if (rwullOp == ASM_OP_OUTB) {
        if (rwpnArg0->rwullType == NODE_ASM_REG) { E8(rwpcbCodeBuf, 0xEE); }
        else { E8(rwpcbCodeBuf, 0xE6); E8(rwpcbCodeBuf, (U8)rwpnArg0->rwullValue); }
        return;
    }
    if (rwullOp == ASM_OP_OUTW) {
        E8(rwpcbCodeBuf, 0x66);
        if (rwpnArg0->rwullType == NODE_ASM_REG) { E8(rwpcbCodeBuf, 0xEF); }
        else { E8(rwpcbCodeBuf, 0xE7); E8(rwpcbCodeBuf, (U8)rwpnArg0->rwullValue); }
        return;
    }
    if (rwullOp == ASM_OP_OUTD) {
        if (rwpnArg0->rwullType == NODE_ASM_REG) { E8(rwpcbCodeBuf, 0xEF); }
        else { E8(rwpcbCodeBuf, 0xE7); E8(rwpcbCodeBuf, (U8)rwpnArg0->rwullValue); }
        return;
    }

    if (rwullOp == ASM_OP_LGDT || rwullOp == ASM_OP_LIDT || rwullOp == ASM_OP_SGDT || rwullOp == ASM_OP_SIDT) {
        E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x01);
        U8 rwullExt;
        if (rwullOp == ASM_OP_SGDT) rwullExt = 0;
        else if (rwullOp == ASM_OP_SIDT) rwullExt = 1;
        else if (rwullOp == ASM_OP_LGDT) rwullExt = 2;
        else rwullExt = 3;
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnArg0);
        AsmEmitMem(rwpcbCodeBuf, rwullExt, &rwpmemMem, 0);
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
        return;
    }
    if (rwullOp == ASM_OP_LLDT || rwullOp == ASM_OP_LTR || rwullOp == ASM_OP_SLDT || rwullOp == ASM_OP_STR) {
        E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x00);
        U8 rwullExt;
        if (rwullOp == ASM_OP_SLDT) rwullExt = 0;
        else if (rwullOp == ASM_OP_STR) rwullExt = 1;
        else if (rwullOp == ASM_OP_LLDT) rwullExt = 2;
        else rwullExt = 3;
        if (rwpnArg0->rwullType == NODE_ASM_REG) {
            E8(rwpcbCodeBuf, (U8)((3 << 6) | (rwullExt << 3) | (AsmRegLow3(rwpnArg0->rwullAsmReg) & 7)));
        } else if (rwpnArg0->rwullType == NODE_ASM_MEM) {
            AsmMemInfo rwpmemMem;
            AsmNodeToMem(&rwpmemMem, rwpnArg0);
            AsmEmitMem(rwpcbCodeBuf, rwullExt, &rwpmemMem, 0);
            if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
        }
        return;
    }
    if (rwullOp == ASM_OP_INVLPG) {
        E8(rwpcbCodeBuf, 0x0F); E8(rwpcbCodeBuf, 0x01);
        AsmMemInfo rwpmemMem;
        AsmNodeToMem(&rwpmemMem, rwpnArg0);
        AsmEmitMem(rwpcbCodeBuf, 7, &rwpmemMem, 0);
        if (rwpmemMem.rwszDispName) free(rwpmemMem.rwszDispName);
        return;
    }

    if (rwullOp >= ASM_OP_XADDQ && rwullOp <= ASM_OP_XADDB) {
        U64 rwullWidth = AsmOpWidth(rwullOp);
        E8(rwpcbCodeBuf, 0x0F);
        E8(rwpcbCodeBuf, (rwullWidth == 1) ? 0xC0 : 0xC1);
        if (rwpnArg0->rwullType == NODE_ASM_REG && rwpnArg1->rwullType == NODE_ASM_REG) {
            AsmEmitRex(rwpcbCodeBuf, rwullWidth == 8, AsmRegNeedsRex(rwpnArg1->rwullAsmReg), 0, AsmRegNeedsRex(rwpnArg0->rwullAsmReg));
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwpnArg1->rwullAsmReg), (U8)AsmRegLow3(rwpnArg0->rwullAsmReg));
        }
        return;
    }
    if (rwullOp >= ASM_OP_CMPXCHGQ && rwullOp <= ASM_OP_CMPXCHGB) {
        U64 rwullWidth = AsmOpWidth(rwullOp);
        E8(rwpcbCodeBuf, 0x0F);
        E8(rwpcbCodeBuf, (rwullWidth == 1) ? 0xB0 : 0xB1);
        if (rwpnArg0->rwullType == NODE_ASM_REG && rwpnArg1->rwullType == NODE_ASM_REG) {
            AsmEmitRex(rwpcbCodeBuf, rwullWidth == 8, AsmRegNeedsRex(rwpnArg1->rwullAsmReg), 0, AsmRegNeedsRex(rwpnArg0->rwullAsmReg));
            AsmEncodeModRM(rwpcbCodeBuf, 3, (U8)AsmRegLow3(rwpnArg1->rwullAsmReg), (U8)AsmRegLow3(rwpnArg0->rwullAsmReg));
        }
        return;
    }

    if (rwullOp >= ASM_OP_SSE_MOVAPS && rwullOp <= ASM_OP_SSE_CVTSD2SS) {
        AsmEncodeSse(rwpcbCodeBuf, rwullOp, rwpnArg0, rwpnArg1);
        return;
    }
    if (rwullOp >= ASM_OP_AVX_VMOVAPS && rwullOp <= ASM_OP_AVX_VZEROALL) {
        AsmEncodeAvx(rwpcbCodeBuf, rwullOp, rwpnArg0, rwpnArg1, rwpnArg2);
        return;
    }

    if (AsmOpIsJcc(rwullOp)) { AsmEncodeJmp(rwpcbCodeBuf, rwullOp, rwpnArg0); return; }
    if (rwullOp == ASM_OP_CALL) { AsmEncodeCall(rwpcbCodeBuf, rwpnArg0); return; }

    if (AsmOpIsMov(rwullOp)) { AsmEncodeMov(rwpcbCodeBuf, rwullOp, rwpnArg0, rwpnArg1); return; }
    if (AsmOpIsLea(rwullOp)) { AsmEncodeLea(rwpcbCodeBuf, rwpnArg0, rwpnArg1); return; }
    if (AsmOpIsAlu(rwullOp)) { AsmEncodeAlu(rwpcbCodeBuf, rwullOp, rwpnArg0, rwpnArg1); return; }
    if (AsmOpIsShift(rwullOp)) { AsmEncodeShift(rwpcbCodeBuf, rwullOp, rwpnArg0, rwpnArg1); return; }
    if (AsmOpIsUnary(rwullOp)) { AsmEncodeUnary(rwpcbCodeBuf, rwullOp, rwpnArg0); return; }
    if (AsmOpIsPush(rwullOp) || AsmOpIsPop(rwullOp)) { AsmEncodePushPop(rwpcbCodeBuf, rwullOp, rwpnArg0); return; }
}

static U64 ArgSlotSize(Node* rwpnParam) {
    if (rwpnParam->rwullPtrDepth) return 8;
    if (rwpnParam->rwullStructId && rwpnParam->rwszStructName) {
        return Align8(StructSize(rwpnParam->rwszStructName));
    }
    return 8;
}

static int ArgIsStructByValue(Node* rwpnParam) {
    return rwpnParam->rwullStructId && !rwpnParam->rwullPtrDepth && rwpnParam->rwszStructName;
}

static int NodeIsStructByValue(Node* rwpnNode) {
    return rwpnNode->rwullStructId && !rwpnNode->rwullPtrDepth && rwpnNode->rwszStructName;
}

static U64 NodeStructSize(Node* rwpnNode) {
    return StructSize(rwpnNode->rwszStructName);
}

static void GenNode(CodeBuf* rwpcbCodeBuf, Node* rwpnNode) {
    if (!rwpnNode) return;
    switch (rwpnNode->rwullType) {
        case NODE_NUM: MovRaxImm(rwpcbCodeBuf, rwpnNode->rwullValue); break;

        case NODE_STR: {
            U64 rwullId = StrAdd(rwpnNode->rwszName);
            E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x05);
            U64 rwullPos = rwpcbCodeBuf->rwullSize;
            E32(rwpcbCodeBuf, 0);
            StrRelocAdd(rwullPos, rwullId);
            break;
        }

        case NODE_NEG: GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); NegRax(rwpcbCodeBuf); break;
        case NODE_POS: GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); break;
        case NODE_NOT:
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); TestRax(rwpcbCodeBuf); Setcc(rwpcbCodeBuf, 0x94); MovzxEaxAl(rwpcbCodeBuf);
            break;

        case NODE_ADD: case NODE_MUL:
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); PushRax(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight); PopRcx(rwpcbCodeBuf);
            if (rwpnNode->rwullType == NODE_ADD) AddRaxRcx(rwpcbCodeBuf);
            else ImulRaxRcx(rwpcbCodeBuf);
            break;

        case NODE_SUB:
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); PushRax(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight); PopRcx(rwpcbCodeBuf);
            E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x87); E8(rwpcbCodeBuf, 0xC8);
            SubRaxRcx(rwpcbCodeBuf);
            break;

        case NODE_DIV:
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); PushRax(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight); PopRcx(rwpcbCodeBuf);
            E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x87); E8(rwpcbCodeBuf, 0xC8);
            IdivRcx(rwpcbCodeBuf);
            break;

        case NODE_MOD:
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); PushRax(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight); PopRcx(rwpcbCodeBuf);
            E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x87); E8(rwpcbCodeBuf, 0xC8);
            IdivRcx(rwpcbCodeBuf);
            E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xD0);
            break;

        case NODE_POW: {
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); PushRax(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight); PopRcx(rwpcbCodeBuf);
            E8(rwpcbCodeBuf, 0x49); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xC0);
            MovRaxImm(rwpcbCodeBuf, 1);
            E8(rwpcbCodeBuf, 0x4D); E8(rwpcbCodeBuf, 0x85); E8(rwpcbCodeBuf, 0xC0);
            E8(rwpcbCodeBuf, 0x74); E8(rwpcbCodeBuf, 0x08);
            ImulRaxRcx(rwpcbCodeBuf);
            E8(rwpcbCodeBuf, 0x49); E8(rwpcbCodeBuf, 0xFF); E8(rwpcbCodeBuf, 0xC8);
            E8(rwpcbCodeBuf, 0xEB); E8(rwpcbCodeBuf, 0xF2);
            break;
        }

        case NODE_DECL: {
            if (rwpnNode->rwpnThird) {
                if (rwpnNode->rwpnThird->rwullType == NODE_UNION_DEF)
                    StructDefAdd(rwpnNode->rwpnThird, 1);
                else
                    StructDefAdd(rwpnNode->rwpnThird, 0);
            }
            if (rwpfiCurFunc) SymAddLocal(rwpnNode->rwszName, rwpnNode->rwullVarType, rwpnNode->rwullPtrDepth, rwpnNode->rwullArraySize, rwpnNode->rwullStructId, rwpnNode->rwszStructName);
            else SymAddGlobal(rwpnNode->rwszName, rwpnNode->rwullVarType, rwpnNode->rwullPtrDepth, rwpnNode->rwullArraySize, rwpnNode->rwullStructId, rwpnNode->rwszStructName);
        if (rwpnNode->rwpnLeft) {
            Symbol* rwpsSymbol = rwpfiCurFunc ? SymFindLocal(rwpnNode->rwszName) : SymFindGlobal(rwpnNode->rwszName);
            if (rwpsSymbol) {
                GenVarAddr(rwpcbCodeBuf, rwpsSymbol);
                PushRax(rwpcbCodeBuf);
                GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft);
                PopRcx(rwpcbCodeBuf);
                E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0x01);
            }
        }
            break;
        }

        case NODE_VAR: {
            Symbol* rwpsSymbol = SymFindLocal(rwpnNode->rwszName);
            if (!rwpsSymbol) rwpsSymbol = SymFindGlobal(rwpnNode->rwszName);
            if (!rwpsSymbol) { MovRaxImm(rwpcbCodeBuf, 0); break; }
            if (rwpsSymbol->rwullArraySize || rwpsSymbol->rwullStructId) {
                GenVarAddr(rwpcbCodeBuf, rwpsSymbol);
            } else {
                GenVarAddr(rwpcbCodeBuf, rwpsSymbol);
                U64 rwullSz = rwpsSymbol->rwullPtrDepth ? 8 : SymbolElemSize(rwpsSymbol);
                GenLoad(rwpcbCodeBuf, rwullSz, rwpsSymbol->rwullPtrDepth ? 0 : TypeSigned(rwpsSymbol->rwullVarType));
            }
            break;
        }

        case NODE_ASSIGN: {
            if (NodeIsStructByValue(rwpnNode->rwpnLeft) && rwpnNode->rwpnRight) {
                GenLValue(rwpcbCodeBuf, rwpnNode->rwpnLeft);
                E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xC0);
                PushRax(rwpcbCodeBuf);
                GenLValue(rwpcbCodeBuf, rwpnNode->rwpnRight);
                E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xC1);
                PopRax(rwpcbCodeBuf);
                U64 rwullSz = StructSize(rwpnNode->rwpnLeft->rwszStructName);
                GenCopyRaxRcx(rwpcbCodeBuf, rwullSz);
                break;
            }
            GenLValue(rwpcbCodeBuf, rwpnNode->rwpnLeft);
            PushRax(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight);
            PopRcx(rwpcbCodeBuf);
            GenStore(rwpcbCodeBuf, LValueTypeSize(rwpnNode->rwpnLeft));
            break;
        }

        case NODE_ADDR:
            GenLValue(rwpcbCodeBuf, rwpnNode->rwpnLeft);
            break;

        case NODE_DEREF:
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft);
            GenLoad(rwpcbCodeBuf, LValueTypeSize(rwpnNode), LValueTypeSigned(rwpnNode));
            break;

        case NODE_INDEX:
            GenLValue(rwpcbCodeBuf, rwpnNode);
            GenLoad(rwpcbCodeBuf, LValueTypeSize(rwpnNode), LValueTypeSigned(rwpnNode));
            break;

        case NODE_STRUCT_ACCESS:
        case NODE_STRUCT_PTR_ACCESS:
            GenLValue(rwpcbCodeBuf, rwpnNode);
            if (rwpnNode->rwpnNext) {
                GenLoad(rwpcbCodeBuf, LValueTypeSize(rwpnNode), LValueTypeSigned(rwpnNode));
            }
            break;

        case NODE_SIZEOF: {
            StructInfo* rwpsiStruct = StructFind(rwpnNode->rwszName);
            U64 rwullSz = rwpsiStruct ? rwpsiStruct->rwullSize : 8;
            MovRaxImm(rwpcbCodeBuf, rwullSz);
            break;
        }

        case NODE_STRUCT_DEF:
            StructDefAdd(rwpnNode, 0);
            break;

        case NODE_UNION_DEF:
            StructDefAdd(rwpnNode, 1);
            break;

        case NODE_BLOCK: {
            Node* rwpnS = rwpnNode->rwpnLeft;
            while (rwpnS) { GenNode(rwpcbCodeBuf, rwpnS); rwpnS = rwpnS->rwpnNext; }
            break;
        }

        case NODE_IF: {
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft);
            TestRax(rwpcbCodeBuf);
            U64 rwullJz = JzPH(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight);
            if (rwpnNode->rwpnThird) {
                U64 rwullJmp = JmpPH(rwpcbCodeBuf);
                Patch32(rwpcbCodeBuf, rwullJz, rwpcbCodeBuf->rwullSize);
                GenNode(rwpcbCodeBuf, rwpnNode->rwpnThird);
                Patch32(rwpcbCodeBuf, rwullJmp, rwpcbCodeBuf->rwullSize);
            } else {
                Patch32(rwpcbCodeBuf, rwullJz, rwpcbCodeBuf->rwullSize);
            }
            break;
        }

        case NODE_WHILE: {
            U64 rwullStart = rwpcbCodeBuf->rwullSize;
            LoopCtx* rwplcLoop = malloc(sizeof(LoopCtx));
            rwplcLoop->rwullStart = rwullStart;
            rwplcLoop->rwullEndPlaceholder = 0;
            rwplcLoop->rwplcNext = rwplcLoopStack;
            rwplcLoopStack = rwplcLoop;

            BreakFixup* rwpbfSaved = rwpbfBreakFixups;
            rwpbfBreakFixups = NULL;

            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft);
            TestRax(rwpcbCodeBuf);
            U64 rwullJz = JzPH(rwpcbCodeBuf);
            rwplcLoop->rwullEndPlaceholder = rwullJz;

            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight);
            E8(rwpcbCodeBuf, 0xE9);
            S32 rwullD = (S32)((S64)rwullStart - (S64)(rwpcbCodeBuf->rwullSize + 4));
            E32(rwpcbCodeBuf, (U32)rwullD);
            Patch32(rwpcbCodeBuf, rwullJz, rwpcbCodeBuf->rwullSize);

            BreakFixup* rwpbfBreak = rwpbfBreakFixups;
            while (rwpbfBreak) {
                Patch32(rwpcbCodeBuf, rwpbfBreak->rwullPos, rwpcbCodeBuf->rwullSize);
                BreakFixup* rwpbfNext = rwpbfBreak->rwpbfNext;
                free(rwpbfBreak);
                rwpbfBreak = rwpbfNext;
            }
            rwpbfBreakFixups = rwpbfSaved;

            rwplcLoopStack = rwplcLoop->rwplcNext;
            free(rwplcLoop);
            break;
        }

        case NODE_BREAK: {
            if (rwplcLoopStack) {
                U64 rwullJmp = JmpPH(rwpcbCodeBuf);
                BreakFixupAdd(rwullJmp);
            }
            break;
        }

        case NODE_CONTINUE: {
            if (rwplcLoopStack) {
                U64 rwullStart = rwplcLoopStack->rwullStart;
                E8(rwpcbCodeBuf, 0xE9);
                S32 rwullD = (S32)((S64)rwullStart - (S64)(rwpcbCodeBuf->rwullSize + 4));
                E32(rwpcbCodeBuf, (U32)rwullD);
            }
            break;
        }

        case NODE_GOTO: {
            E8(rwpcbCodeBuf, 0xE9);
            U64 rwullPos = rwpcbCodeBuf->rwullSize;
            E32(rwpcbCodeBuf, 0);
            LabelRelocAdd(rwullPos, rwpnNode->rwszName);
            break;
        }

        case NODE_LABEL: {
            LabelInfo* rwpliLabel = malloc(sizeof(LabelInfo));
            rwpliLabel->rwszName = strdup(rwpnNode->rwszName);
            rwpliLabel->rwullAddr = rwpcbCodeBuf->rwullSize;
            rwpliLabel->rwptNext = rwpliLabels;
            rwpliLabels = rwpliLabel;
            break;
        }

        case NODE_EQ: case NODE_NEQ: case NODE_LT:
        case NODE_GT: case NODE_LE: case NODE_GE: {
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); PushRax(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight); PopRcx(rwpcbCodeBuf);
            CmpRaxRcx(rwpcbCodeBuf);
            switch (rwpnNode->rwullType) {
                case NODE_EQ: Setcc(rwpcbCodeBuf, 0x94); break;
                case NODE_NEQ: Setcc(rwpcbCodeBuf, 0x95); break;
                case NODE_LT: Setcc(rwpcbCodeBuf, 0x9C); break;
                case NODE_GT: Setcc(rwpcbCodeBuf, 0x9F); break;
                case NODE_LE: Setcc(rwpcbCodeBuf, 0x9E); break;
                case NODE_GE: Setcc(rwpcbCodeBuf, 0x9D); break;
            }
            MovzxEaxAl(rwpcbCodeBuf);
            break;
        }

        case NODE_AND: {
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); TestRax(rwpcbCodeBuf);
            U64 rwullJz = JzPH(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight); TestRax(rwpcbCodeBuf);
            Setcc(rwpcbCodeBuf, 0x95); MovzxEaxAl(rwpcbCodeBuf);
            U64 rwullJmp = JmpPH(rwpcbCodeBuf);
            Patch32(rwpcbCodeBuf, rwullJz, rwpcbCodeBuf->rwullSize);
            XorRax(rwpcbCodeBuf);
            Patch32(rwpcbCodeBuf, rwullJmp, rwpcbCodeBuf->rwullSize);
            break;
        }

        case NODE_OR: {
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft); TestRax(rwpcbCodeBuf);
            U64 rwullJnz = JnzPH(rwpcbCodeBuf);
            GenNode(rwpcbCodeBuf, rwpnNode->rwpnRight); TestRax(rwpcbCodeBuf);
            Setcc(rwpcbCodeBuf, 0x95); MovzxEaxAl(rwpcbCodeBuf);
            U64 rwullJmp = JmpPH(rwpcbCodeBuf);
            Patch32(rwpcbCodeBuf, rwullJnz, rwpcbCodeBuf->rwullSize);
            MovRaxImm(rwpcbCodeBuf, 1);
            Patch32(rwpcbCodeBuf, rwullJmp, rwpcbCodeBuf->rwullSize);
            break;
        }

        case NODE_FUNC: {
            FuncInfo* rwpfiFunc = malloc(sizeof(FuncInfo));
            memset(rwpfiFunc, 0, sizeof(FuncInfo));
            rwpfiFunc->rwszName = strdup(rwpnNode->rwszName);
            rwpfiFunc->rwullId = rwullNextFuncId++;
            rwpfiFunc->rwullAddr = rwpcbCodeBuf->rwullSize;
            rwpfiFunc->rwullRetByValue = NodeIsStructByValue(rwpnNode) ? 1 : 0;
            rwpfiFunc->rwullRetSize = rwpfiFunc->rwullRetByValue ? NodeStructSize(rwpnNode) : 0;
            if (rwpnNode->rwszStructName) rwpfiFunc->rwszRetStructName = strdup(rwpnNode->rwszStructName);
            rwpfiFunc->rwpfiNext = rwpfiFuncs;
            rwpfiFuncs = rwpfiFunc;
            SymAddFunc(rwpnNode->rwszName, rwpfiFunc->rwullId);

            FuncInfo* rwpfiSaved = rwpfiCurFunc;
            rwpfiCurFunc = rwpfiFunc;
            U64 rwullSavedStack = rwullStackSize;
            rwullStackSize = 0;

            PushRbp(rwpcbCodeBuf);
            MovRbpRsp(rwpcbCodeBuf);

            U64 rwullParamOffset = 16;
            if (rwpfiFunc->rwullRetByValue) rwullParamOffset += 8;

            Node* rwpnP = rwpnNode->rwpnArgs;
            while (rwpnP) {
                if (rwpnP->rwpnThird) {
                    if (rwpnP->rwpnThird->rwullType == NODE_UNION_DEF)
                        StructDefAdd(rwpnP->rwpnThird, 1);
                    else
                        StructDefAdd(rwpnP->rwpnThird, 0);
                }
                Symbol* rwpsSymbol = malloc(sizeof(Symbol));
                memset(rwpsSymbol, 0, sizeof(Symbol));
                rwpsSymbol->rwszName = strdup(rwpnP->rwszName);
                rwpsSymbol->rwullVarType = rwpnP->rwullVarType;
                rwpsSymbol->rwullPtrDepth = rwpnP->rwullPtrDepth;
                rwpsSymbol->rwullStructId = rwpnP->rwullStructId;
                if (rwpnP->rwszStructName) rwpsSymbol->rwszStructName = strdup(rwpnP->rwszStructName);
                rwpsSymbol->rwullOffset = rwullParamOffset;
                rwpsSymbol->rwullIsParam = 1;
                rwpsSymbol->rwptNext = rwpfiCurFunc->rwpsLocals;
                rwpfiCurFunc->rwpsLocals = rwpsSymbol;
                rwullParamOffset += ArgSlotSize(rwpnP);
                rwpnP = rwpnP->rwpnNext;
            }

            if (rwpfiFunc->rwullRetByValue) {
                Symbol* rwpsRet = malloc(sizeof(Symbol));
                memset(rwpsRet, 0, sizeof(Symbol));
                rwpsRet->rwszName = strdup("__ret");
                rwpsRet->rwullStructId = 1;
                if (rwpnNode->rwszStructName) rwpsRet->rwszStructName = strdup(rwpnNode->rwszStructName);
                rwpsRet->rwullOffset = 16;
                rwpsRet->rwullIsParam = 1;
                rwpsRet->rwptNext = rwpfiCurFunc->rwpsLocals;
                rwpfiCurFunc->rwpsLocals = rwpsRet;
            }

            U64 rwullPrologueSize = rwpcbCodeBuf->rwullSize;

            GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft);

            int rwullHasRet = 0;
            Node* rwpnScan = rwpnNode->rwpnLeft;
            if (rwpnScan && rwpnScan->rwullType == NODE_BLOCK) {
                Node* rwpnSt = rwpnScan->rwpnLeft;
                while (rwpnSt) {
                    if (rwpnSt->rwullType == NODE_RETURN) { rwullHasRet = 1; break; }
                    rwpnSt = rwpnSt->rwpnNext;
                }
            }

            if (!rwullHasRet) {
                if (rwpfiFunc->rwullRetByValue) {
                    E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x85);
                    E32(rwpcbCodeBuf, 0x10);
                } else {
                    MovRaxImm(rwpcbCodeBuf, 0);
                }
                MovRspRbp(rwpcbCodeBuf);
                PopRbp(rwpcbCodeBuf);
                Ret(rwpcbCodeBuf);
            }

            rwpfiFunc->rwullStackSize = rwullStackSize;
            U64 rwullAligned = (rwullStackSize + 15) & ~15ULL;
            if (rwullAligned > 0) {
                U64 rwullOldSize = rwpcbCodeBuf->rwullSize;
                BufGrow(rwpcbCodeBuf, 7);
                memmove(rwpcbCodeBuf->rwpData + rwullPrologueSize + 7,
                        rwpcbCodeBuf->rwpData + rwullPrologueSize,
                        rwullOldSize - rwullPrologueSize);
                U8 rwullSub[7] = {0x48, 0x81, 0xEC, 0, 0, 0, 0};
                U32 rwullSz = (U32)rwullAligned;
                memcpy(rwullSub + 3, &rwullSz, 4);
                memcpy(rwpcbCodeBuf->rwpData + rwullPrologueSize, rwullSub, 7);
                rwpcbCodeBuf->rwullSize += 7;

                ShiftRelocs(rwullPrologueSize, 7);

                FuncInfo* rwpfiIter = rwpfiFuncs;
                while (rwpfiIter) {
                    if (rwpfiIter->rwullAddr >= rwullPrologueSize) rwpfiIter->rwullAddr += 7;
                    rwpfiIter = rwpfiIter->rwpfiNext;
                }
            }

            rwullStackSize = rwullSavedStack;
            rwpfiCurFunc = rwpfiSaved;
            break;
        }

        case NODE_CALL: {
            FuncInfo* rwpfiCallee = FuncFind(rwpnNode->rwszName);
            int rwullRetByValue = rwpfiCallee && rwpfiCallee->rwullRetByValue;

            U64 rwullRetTempOffset = 0;
            if (rwullRetByValue) {
                U64 rwullRetSize = Align8(rwpfiCallee->rwullRetSize);
                rwullStackSize += rwullRetSize;
                rwullRetTempOffset = rwullStackSize;
            }

            U64 rwullArgc = 0;
            Node* rwpnA = rwpnNode->rwpnArgs;
            while (rwpnA) { rwullArgc++; rwpnA = rwpnA->rwpnNext; }

            Node* rwpArgs[32];
            U64 rwullCnt = 0;
            rwpnA = rwpnNode->rwpnArgs;
            while (rwpnA && rwullCnt < 32) { rwpArgs[rwullCnt++] = rwpnA; rwpnA = rwpnA->rwpnNext; }

            if (rwullRetByValue) {
                E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x85);
                E32(rwpcbCodeBuf, (U32)(-(S32)rwullRetTempOffset));
                PushRax(rwpcbCodeBuf);
            }

            for (S64 rwullI = (S64)rwullCnt - 1; rwullI >= 0; rwullI--) {
                Node* rwpnArg = rwpArgs[rwullI];
                if (NodeIsStructByValue(rwpnArg)) {
                    U64 rwullSz = Align8(StructSize(rwpnArg->rwszStructName));
                    U64 rwullSlots = rwullSz / 8;
                    if (rwullSlots == 0) rwullSlots = 1;
                    GenLValue(rwpcbCodeBuf, rwpnArg);
                    E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xC1);
                    for (S64 rwullJ = (S64)rwullSlots - 1; rwullJ >= 0; rwullJ--) {
                        E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8B); E8(rwpcbCodeBuf, 0x81);
                        E32(rwpcbCodeBuf, (U32)(rwullJ * 8));
                        PushRax(rwpcbCodeBuf);
                    }
                } else {
                    GenNode(rwpcbCodeBuf, rwpnArg);
                    PushRax(rwpcbCodeBuf);
                }
            }

            E8(rwpcbCodeBuf, 0xE8);
            U64 rwullPos = rwpcbCodeBuf->rwullSize;
            E32(rwpcbCodeBuf, 0);
            RelocAdd(rwullPos, rwpnNode->rwszName);

            U64 rwullArgBytes = 0;
            for (U64 rwullI = 0; rwullI < rwullCnt; rwullI++) {
                if (NodeIsStructByValue(rwpArgs[rwullI]))
                    rwullArgBytes += Align8(StructSize(rwpArgs[rwullI]->rwszStructName));
                else
                    rwullArgBytes += 8;
            }
            if (rwullRetByValue) rwullArgBytes += 8;

            if (rwullArgBytes > 0) AddRspImm32(rwpcbCodeBuf, (U32)rwullArgBytes);

            if (rwullRetByValue) {
                E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x85);
                E32(rwpcbCodeBuf, (U32)(-(S32)rwullRetTempOffset));
            }
            break;
        }

        case NODE_RETURN: {
            int rwullRetByValue = rwpfiCurFunc && rwpfiCurFunc->rwullRetByValue;
            if (rwullRetByValue && rwpnNode->rwpnLeft) {
                if (NodeIsStructByValue(rwpnNode->rwpnLeft)) {
                    E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x85);
                    E32(rwpcbCodeBuf, 0x10);
                    E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xC0);
                    PushRax(rwpcbCodeBuf);
                    GenLValue(rwpcbCodeBuf, rwpnNode->rwpnLeft);
                    E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xC1);
                    PopRax(rwpcbCodeBuf);
                    U64 rwullSz = rwpfiCurFunc->rwullRetSize;
                    GenCopyRaxRcx(rwpcbCodeBuf, rwullSz);
                    E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x85);
                    E32(rwpcbCodeBuf, 0x10);
                } else {
                    GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft);
                    E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x8D); E8(rwpcbCodeBuf, 0x85);
                    E32(rwpcbCodeBuf, 0x10);
                    E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0xC0);
                    E8(rwpcbCodeBuf, 0x48); E8(rwpcbCodeBuf, 0x89); E8(rwpcbCodeBuf, 0x01);
                }
            } else if (rwpnNode->rwpnLeft) {
                GenNode(rwpcbCodeBuf, rwpnNode->rwpnLeft);
            } else {
                MovRaxImm(rwpcbCodeBuf, 0);
            }
            MovRspRbp(rwpcbCodeBuf);
            PopRbp(rwpcbCodeBuf);
            Ret(rwpcbCodeBuf);
            break;
        }

        case NODE_ASM:
            AsmEncode(rwpcbCodeBuf, rwpnNode);
            break;

        default: break;
    }
}

CodeBuf* AstToCode(Node* rwpnRoot) {
    CodeBuf* rwpcbCodeBuf = malloc(sizeof(CodeBuf));
    memset(rwpcbCodeBuf, 0, sizeof(CodeBuf));

    GenNode(rwpcbCodeBuf, rwpnRoot);

    U64 rwullGlobalStart = rwpcbCodeBuf->rwullSize;
    for (U64 i = 0; i < rwullGlobalSize; i++) E8(rwpcbCodeBuf, 0);

    StringLit* rwpslString = rwpslStrings;
    while (rwpslString) {
        rwpslString->rwullAddr = rwpcbCodeBuf->rwullSize;
        for (const char* rwszP = rwpslString->rwszText; *rwszP; rwszP++) E8(rwpcbCodeBuf, (U8)*rwszP);
        E8(rwpcbCodeBuf, 0);
        rwpslString = rwpslString->rwptNext;
    }

    E8(rwpcbCodeBuf, 0xC3);

    Reloc* rwprlReloc = rwprlRelocs;
    while (rwprlReloc) {
        FuncInfo* rwpfiFunc = FuncFind(rwprlReloc->rwszName);
        if (rwpfiFunc) {
            S32 rwullD = (S32)((S64)rwpfiFunc->rwullAddr - (S64)(rwprlReloc->rwullPos + 4));
            memcpy(rwpcbCodeBuf->rwpData + rwprlReloc->rwullPos, &rwullD, 4);
        } else {
            Symbol* rwpsSymbol = SymFindGlobal(rwprlReloc->rwszName);
            if (rwpsSymbol && rwpsSymbol->rwullIsGlobal) {
                S32 rwullD = (S32)((S64)(rwullGlobalStart + rwpsSymbol->rwullGlobalAddr) - (S64)(rwprlReloc->rwullPos + 4));
                memcpy(rwpcbCodeBuf->rwpData + rwprlReloc->rwullPos, &rwullD, 4);
            }
        }
        rwprlReloc = rwprlReloc->rwptNext;
    }

    AsmReloc* rwparAsmReloc = rwparAsmRelocs;
    while (rwparAsmReloc) {
        Symbol* rwpsSymbol = SymFindGlobal(rwparAsmReloc->rwszName);
        if (rwpsSymbol && rwpsSymbol->rwullIsGlobal) {
            S32 rwullD = (S32)((S64)(rwullGlobalStart + rwpsSymbol->rwullGlobalAddr) - (S64)(rwparAsmReloc->rwullPos + 4));
            memcpy(rwpcbCodeBuf->rwpData + rwparAsmReloc->rwullPos, &rwullD, 4);
        }
        rwparAsmReloc = rwparAsmReloc->rwparNext;
    }

    LabelReloc* rwplrLabelReloc = rwplrLabelRelocs;
    while (rwplrLabelReloc) {
        LabelInfo* rwpliLabel = rwpliLabels;
        while (rwpliLabel) {
            if (strcmp(rwpliLabel->rwszName, rwplrLabelReloc->rwszName) == 0) {
                S32 rwullD = (S32)((S64)rwpliLabel->rwullAddr - (S64)(rwplrLabelReloc->rwullPos + 4));
                memcpy(rwpcbCodeBuf->rwpData + rwplrLabelReloc->rwullPos, &rwullD, 4);
                break;
            }
            rwpliLabel = rwpliLabel->rwptNext;
        }
        rwplrLabelReloc = rwplrLabelReloc->rwplrNext;
    }

    StrReloc* rwpsrStrReloc = rwpsrStrRelocs;
    while (rwpsrStrReloc) {
        StringLit* rwpslString2 = StrFind(rwpsrStrReloc->rwullId);
        if (rwpslString2) {
            S32 rwullD = (S32)((S64)rwpslString2->rwullAddr - (S64)(rwpsrStrReloc->rwullPos + 4));
            memcpy(rwpcbCodeBuf->rwpData + rwpsrStrReloc->rwullPos, &rwullD, 4);
        }
        rwpsrStrReloc = rwpsrStrReloc->rwptNext;
    }

    Symbol* rwpsSymbol = rwpsSymbols;
    while (rwpsSymbol) { Symbol* rwpsNext = rwpsSymbol->rwptNext; free(rwpsSymbol->rwszName); if (rwpsSymbol->rwszStructName) free(rwpsSymbol->rwszStructName); free(rwpsSymbol); rwpsSymbol = rwpsNext; }
    rwpsSymbols = NULL;

    FuncInfo* rwpfiFunc = rwpfiFuncs;
    while (rwpfiFunc) {
        FuncInfo* rwpfiNext = rwpfiFunc->rwpfiNext;
        free(rwpfiFunc->rwszName);
        if (rwpfiFunc->rwszRetStructName) free(rwpfiFunc->rwszRetStructName);
        free(rwpfiFunc);
        rwpfiFunc = rwpfiNext;
    }
    rwpfiFuncs = NULL;

    Reloc* rwprlRelocFree = rwprlRelocs;
    while (rwprlRelocFree) { Reloc* rwprlNext = rwprlRelocFree->rwptNext; free(rwprlRelocFree->rwszName); free(rwprlRelocFree); rwprlRelocFree = rwprlNext; }
    rwprlRelocs = NULL;

    StringLit* rwpslStringFree = rwpslStrings;
    while (rwpslStringFree) { StringLit* rwpslNext = rwpslStringFree->rwptNext; free(rwpslStringFree->rwszText); free(rwpslStringFree); rwpslStringFree = rwpslNext; }
    rwpslStrings = NULL;

    LabelInfo* rwpliLabelFree = rwpliLabels;
    while (rwpliLabelFree) { LabelInfo* rwpliNext = rwpliLabelFree->rwptNext; free(rwpliLabelFree->rwszName); free(rwpliLabelFree); rwpliLabelFree = rwpliNext; }
    rwpliLabels = NULL;

    LabelReloc* rwplrLabelRelocFree = rwplrLabelRelocs;
    while (rwplrLabelRelocFree) { LabelReloc* rwplrNext = rwplrLabelRelocFree->rwplrNext; free(rwplrLabelRelocFree->rwszName); free(rwplrLabelRelocFree); rwplrLabelRelocFree = rwplrNext; }
    rwplrLabelRelocs = NULL;

    BreakFixup* rwpbfBreakFree = rwpbfBreakFixups;
    while (rwpbfBreakFree) { BreakFixup* rwpbfNext = rwpbfBreakFree->rwpbfNext; free(rwpbfBreakFree); rwpbfBreakFree = rwpbfNext; }
    rwpbfBreakFixups = NULL;

    StrReloc* rwpsrStrRelocFree = rwpsrStrRelocs;
    while (rwpsrStrRelocFree) { StrReloc* rwpsrNext = rwpsrStrRelocFree->rwptNext; free(rwpsrStrRelocFree); rwpsrStrRelocFree = rwpsrNext; }
    rwpsrStrRelocs = NULL;

    LoopCtx* rwplcLoopFree = rwplcLoopStack;
    while (rwplcLoopFree) { LoopCtx* rwplcNext = rwplcLoopFree->rwplcNext; free(rwplcLoopFree); rwplcLoopFree = rwplcNext; }
    rwplcLoopStack = NULL;

    AsmReloc* rwparAsmRelocFree = rwparAsmRelocs;
    while (rwparAsmRelocFree) { AsmReloc* rwparNext = rwparAsmRelocFree->rwparNext; free(rwparAsmRelocFree->rwszName); free(rwparAsmRelocFree); rwparAsmRelocFree = rwparNext; }
    rwparAsmRelocs = NULL;

    StructInfo* rwpsiStruct = rwpsiStructs;
    while (rwpsiStruct) {
        StructField* rwpsfField = rwpsiStruct->rwpsfFields;
        while (rwpsfField) {
            StructField* rwpsfNext = rwpsfField->rwptNext;
            free(rwpsfField->rwszName);
            if (rwpsfField->rwszStructName) free(rwpsfField->rwszStructName);
            free(rwpsfField);
            rwpsfField = rwpsfNext;
        }
        StructInfo* rwpsiNext = rwpsiStruct->rwpsiNext;
        free(rwpsiStruct->rwszName);
        free(rwpsiStruct);
        rwpsiStruct = rwpsiNext;
    }
    rwpsiStructs = NULL;

    rwullNextFuncId = 1;
    rwullGlobalSize = 0;
    rwullStackSize = 0;

    return rwpcbCodeBuf;
}

void CodeBufFree(CodeBuf* rwpcbCodeBuf) {
    if (!rwpcbCodeBuf) return;
    if (rwpcbCodeBuf->rwpData) free(rwpcbCodeBuf->rwpData);
    free(rwpcbCodeBuf);
}
