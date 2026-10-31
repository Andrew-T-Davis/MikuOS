#include "grammar.h"
#include "ast.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

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

static char* EatOneInclude(const char* roszSrc, U64* rwpullOutSize) {
    if (strncmp(roszSrc, "@Include \"", 10) != 0) return NULL;

    const char* roszName    = roszSrc + 10;
    const char* roszNameEnd = roszName;
    while (*roszNameEnd && *roszNameEnd != '"' && *roszNameEnd != '\n')
        roszNameEnd++;

    if (*roszNameEnd != '"') {
        fprintf(stderr, "Include: missing closing '\"'\n");
        return NULL;
    }

    U64 rwullNameLen = (U64)(roszNameEnd - roszName);
    char rwszNameBuf[0x1000];
    if (rwullNameLen >= sizeof(rwszNameBuf)) {
        fprintf(stderr, "Include: filename too long\n");
        return NULL;
    }
    memcpy(rwszNameBuf, roszName, rwullNameLen);
    rwszNameBuf[rwullNameLen] = '\0';

    U64 rwullIncSize = 0;
    char* rwszInc = FileReadAll(rwszNameBuf, &rwullIncSize);
    if (!rwszInc) {
        fprintf(stderr, "Include: cannot read %s\n", rwszNameBuf);
        return NULL;
    }

    const char* roszRest = roszNameEnd + 1;
    if (*roszRest == '\r') roszRest++;
    if (*roszRest == '\n') roszRest++;

    U64 rwullRestLen = strlen(roszRest);

    char* rwszNew = malloc(rwullIncSize + rwullRestLen + 1);
    if (!rwszNew) {
        free(rwszInc);
        return NULL;
    }

    memcpy(rwszNew + rwullRestLen, rwszInc, rwullIncSize);
    memcpy(rwszNew, roszRest, rwullRestLen);

    *rwpullOutSize = rwullIncSize + rwullRestLen;
    free(rwszInc);
    return rwszNew;
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

    U64 rwullIncludeCount = 0;
    while (1) {
        U64 rwullNewSize = 0;
        char* rwszNew = EatOneInclude(rwszCode, &rwullNewSize);
        if (!rwszNew) break;

        free(rwszCode);
        rwszCode     = rwszNew;
        rwullSrcSize = rwullNewSize;
        rwullIncludeCount++;
    }

    printf("Include %" PRIu64 " files\n", rwullIncludeCount);

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
