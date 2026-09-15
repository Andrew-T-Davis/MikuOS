#include "grammar.h"
#include "ast.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>

static char* FileReadAll(const char* rwroszPath, U64* rwpullSize) {
    FILE* rwpfFile = fopen(rwroszPath, "rb");
    if (!rwpfFile) return NULL;
    fseek(rwpfFile, 0, SEEK_END);
    long rwslLen = ftell(rwpfFile);
    fseek(rwpfFile, 0, SEEK_SET);
    if (rwslLen < 0) { fclose(rwpfFile); return NULL; }
    char* rwszBuf = malloc((U64)rwslLen + 1);
    U64 rwullRead = fread(rwszBuf, 1, (U64)rwslLen, rwpfFile);
    rwszBuf[rwullRead] = '\0';
    fclose(rwpfFile);
    *rwpullSize = rwullRead;
    return rwszBuf;
}

int main(int rwsiArgc, char** rwpszArgv) {
    if (rwsiArgc != 3) {
        fprintf(stderr, "usage: %s <input.hm> <output.bin>\n", rwpszArgv[0]);
        return 1;
    }

    U64 rwullSrcSize = 0;
    char* rwszCode = FileReadAll(rwpszArgv[1], &rwullSrcSize);
    if (!rwszCode) {
        fprintf(stderr, "failed to read %s\n", rwpszArgv[1]);
        return 1;
    }

    TokenRoot* rwptrRoot = CodeToToken(rwszCode);
    if (!rwptrRoot) {
        fprintf(stderr, "lex failed\n");
        free(rwszCode);
        return 1;
    }

    Node* rwpnRoot = TokenToAst(rwptrRoot);
    if (!rwpnRoot) {
        fprintf(stderr, "parse failed\n");
        TokenRootFree(rwptrRoot);
        free(rwszCode);
        return 1;
    }

    AstPrint(rwpnRoot, 0);

    CodeBuf* rwpcbCodeBuf = AstToCode(rwpnRoot);

    FILE* rwpfOut = fopen(rwpszArgv[2], "wb");
    if (!rwpfOut) {
        fprintf(stderr, "failed to write %s\n", rwpszArgv[2]);
        CodeBufFree(rwpcbCodeBuf);
        AstFree(rwpnRoot);
        TokenRootFree(rwptrRoot);
        free(rwszCode);
        return 1;
    }
    fwrite(rwpcbCodeBuf->rwpData, 1, rwpcbCodeBuf->rwullSize, rwpfOut);
    fclose(rwpfOut);

    fprintf(stderr, "wrote %llu bytes to %s\n",
            (unsigned long long)rwpcbCodeBuf->rwullSize, rwpszArgv[2]);

    CodeBufFree(rwpcbCodeBuf);
    AstFree(rwpnRoot);
    TokenRootFree(rwptrRoot);
    free(rwszCode);
    return 0;
}
