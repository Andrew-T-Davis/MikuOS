#include "asm.h"
#include "int.h"
#include <string.h>

static AsmEntry g_rwaAsmOps[] = {
    {"MOVQ", ASM_OP_MOVQ}, {"MOVD", ASM_OP_MOVD}, {"MOVW", ASM_OP_MOVW}, {"MOVB", ASM_OP_MOVB},
    {"MOVZX", ASM_OP_MOVZX}, {"MOVSX", ASM_OP_MOVSX},
    {"LEAQ", ASM_OP_LEAQ}, {"LEAD", ASM_OP_LEAD}, {"LEAW", ASM_OP_LEAW}, {"LEAB", ASM_OP_LEAB},
    {"PUSHQ", ASM_OP_PUSHQ}, {"PUSHD", ASM_OP_PUSHD}, {"PUSHW", ASM_OP_PUSHW}, {"PUSHB", ASM_OP_PUSHB},
    {"POPQ", ASM_OP_POPQ}, {"POPD", ASM_OP_POPD}, {"POPW", ASM_OP_POPW}, {"POPB", ASM_OP_POPB},
    {"XCHGQ", ASM_OP_XCHGQ}, {"XCHGD", ASM_OP_XCHGD}, {"XCHGW", ASM_OP_XCHGW}, {"XCHGB", ASM_OP_XCHGB},
    {"ADDQ", ASM_OP_ADDQ}, {"ADDD", ASM_OP_ADDD}, {"ADDW", ASM_OP_ADDW}, {"ADDB", ASM_OP_ADDB},
    {"SUBQ", ASM_OP_SUBQ}, {"SUBD", ASM_OP_SUBD}, {"SUBW", ASM_OP_SUBW}, {"SUBB", ASM_OP_SUBB},
    {"IMULQ", ASM_OP_IMULQ}, {"IMULD", ASM_OP_IMULD}, {"IMULW", ASM_OP_IMULW}, {"IMULB", ASM_OP_IMULB},
    {"MULQ", ASM_OP_MULQ}, {"MULD", ASM_OP_MULD}, {"MULW", ASM_OP_MULW}, {"MULB", ASM_OP_MULB},
    {"IDIVQ", ASM_OP_IDIVQ}, {"IDIVD", ASM_OP_IDIVD}, {"IDIVW", ASM_OP_IDIVW}, {"IDIVB", ASM_OP_IDIVB},
    {"DIVQ", ASM_OP_DIVQ}, {"DIVD", ASM_OP_DIVD}, {"DIVW", ASM_OP_DIVW}, {"DIVB", ASM_OP_DIVB},
    {"INCQ", ASM_OP_INCQ}, {"INCD", ASM_OP_INCD}, {"INCW", ASM_OP_INCW}, {"INCB", ASM_OP_INCB},
    {"DECQ", ASM_OP_DECQ}, {"DECD", ASM_OP_DECD}, {"DECW", ASM_OP_DECW}, {"DECB", ASM_OP_DECB},
    {"NEGQ", ASM_OP_NEGQ}, {"NEGD", ASM_OP_NEGD}, {"NEGW", ASM_OP_NEGW}, {"NEGB", ASM_OP_NEGB},
    {"CMPQ", ASM_OP_CMPQ}, {"CMPD", ASM_OP_CMPD}, {"CMPW", ASM_OP_CMPW}, {"CMPB", ASM_OP_CMPB},
    {"TESTQ", ASM_OP_TESTQ}, {"TESTD", ASM_OP_TESTD}, {"TESTW", ASM_OP_TESTW}, {"TESTB", ASM_OP_TESTB},
    {"ANDQ", ASM_OP_ANDQ}, {"ANDD", ASM_OP_ANDD}, {"ANDW", ASM_OP_ANDW}, {"ANDB", ASM_OP_ANDB},
    {"ORQ", ASM_OP_ORQ}, {"ORD", ASM_OP_ORD}, {"ORW", ASM_OP_ORW}, {"ORB", ASM_OP_ORB},
    {"XORQ", ASM_OP_XORQ}, {"XORD", ASM_OP_XORD}, {"XORW", ASM_OP_XORW}, {"XORB", ASM_OP_XORB},
    {"NOTQ", ASM_OP_NOTQ}, {"NOTD", ASM_OP_NOTD}, {"NOTW", ASM_OP_NOTW}, {"NOTB", ASM_OP_NOTB},
    {"SHLQ", ASM_OP_SHLQ}, {"SHLD", ASM_OP_SHLD}, {"SHLW", ASM_OP_SHLW}, {"SHLB", ASM_OP_SHLB},
    {"SHRQ", ASM_OP_SHRQ}, {"SHRD", ASM_OP_SHRD}, {"SHRW", ASM_OP_SHRW}, {"SHRB", ASM_OP_SHRB},
    {"SARQ", ASM_OP_SARQ}, {"SARD", ASM_OP_SARD}, {"SARW", ASM_OP_SARW}, {"SARB", ASM_OP_SARB},
    {"SALQ", ASM_OP_SALQ}, {"SALD", ASM_OP_SALD}, {"SALW", ASM_OP_SALW}, {"SALB", ASM_OP_SALB},
    {"BTQ", ASM_OP_BTQ}, {"BTD", ASM_OP_BTD}, {"BTW", ASM_OP_BTW}, {"BTB", ASM_OP_BTB},
    {"BTSQ", ASM_OP_BTSQ}, {"BTSD", ASM_OP_BTSD}, {"BTSW", ASM_OP_BTSW}, {"BTSB", ASM_OP_BTSB},
    {"BTRQ", ASM_OP_BTRQ}, {"BTRD", ASM_OP_BTRD}, {"BTRW", ASM_OP_BTRW}, {"BTRB", ASM_OP_BTRB},
    {"BTCQ", ASM_OP_BTCQ}, {"BTCD", ASM_OP_BTCD}, {"BTCW", ASM_OP_BTCW}, {"BTCB", ASM_OP_BTCB},
    {"BSFQ", ASM_OP_BSFQ}, {"BSFD", ASM_OP_BSFD}, {"BSFW", ASM_OP_BSFW}, {"BSFB", ASM_OP_BSFB},
    {"BSRQ", ASM_OP_BSRQ}, {"BSRD", ASM_OP_BSRD}, {"BSRW", ASM_OP_BSRW}, {"BSRB", ASM_OP_BSRB},
    {"POPCNTQ", ASM_OP_POPCNTQ}, {"POPCNTD", ASM_OP_POPCNTD}, {"POPCNTW", ASM_OP_POPCNTW}, {"POPCNTB", ASM_OP_POPCNTB},
    {"LZCNTQ", ASM_OP_LZCNTQ}, {"LZCNTD", ASM_OP_LZCNTD}, {"LZCNTW", ASM_OP_LZCNTW}, {"LZCNTB", ASM_OP_LZCNTB},
    {"TZCNTQ", ASM_OP_TZCNTQ}, {"TZCNTD", ASM_OP_TZCNTD}, {"TZCNTW", ASM_OP_TZCNTW}, {"TZCNTB", ASM_OP_TZCNTB},
    {"JMP", ASM_OP_JMP}, {"JZ", ASM_OP_JZ}, {"JNZ", ASM_OP_JNZ},
    {"JE", ASM_OP_JE}, {"JNE", ASM_OP_JNE},
    {"JL", ASM_OP_JL}, {"JG", ASM_OP_JG}, {"JLE", ASM_OP_JLE}, {"JGE", ASM_OP_JGE},
    {"JB", ASM_OP_JB}, {"JA", ASM_OP_JA}, {"JBE", ASM_OP_JBE}, {"JAE", ASM_OP_JAE},
    {"JC", ASM_OP_JC}, {"JNC", ASM_OP_JNC}, {"JO", ASM_OP_JO}, {"JNO", ASM_OP_JNO},
    {"JS", ASM_OP_JS}, {"JNS", ASM_OP_JNS}, {"JP", ASM_OP_JP}, {"JNP", ASM_OP_JNP},
    {"JECXZ", ASM_OP_JECXZ}, {"JRCXZ", ASM_OP_JRCXZ},
    {"CALL", ASM_OP_CALL}, {"RET", ASM_OP_RET},
    {"NOP", ASM_OP_NOP}, {"HLT", ASM_OP_HLT}, {"CPUID", ASM_OP_CPUID}, {"INT", ASM_OP_INT},
    {"CLI", ASM_OP_CLI}, {"STI", ASM_OP_STI}, {"IRETQ", ASM_OP_IRETQ},
    {"INVLPG", ASM_OP_INVLPG}, {"WBINVD", ASM_OP_WBINVD},
    {"RDMSR", ASM_OP_RDMSR}, {"WRMSR", ASM_OP_WRMSR},
    {"RDTSC", ASM_OP_RDTSC}, {"RDTSCP", ASM_OP_RDTSCP}, {"SWAPGS", ASM_OP_SWAPGS},
    {"LGDT", ASM_OP_LGDT}, {"LIDT", ASM_OP_LIDT}, {"SGDT", ASM_OP_SGDT}, {"SIDT", ASM_OP_SIDT},
    {"LLDT", ASM_OP_LLDT}, {"LTR", ASM_OP_LTR}, {"SLDT", ASM_OP_SLDT}, {"STR", ASM_OP_STR},
    {"INB", ASM_OP_INB}, {"INW", ASM_OP_INW}, {"IND", ASM_OP_IND},
    {"OUTB", ASM_OP_OUTB}, {"OUTW", ASM_OP_OUTW}, {"OUTD", ASM_OP_OUTD},
    {"PAUSE", ASM_OP_PAUSE}, {"MFENCE", ASM_OP_MFENCE}, {"LFENCE", ASM_OP_LFENCE}, {"SFENCE", ASM_OP_SFENCE},
    {"XADDQ", ASM_OP_XADDQ}, {"XADDD", ASM_OP_XADDD}, {"XADDW", ASM_OP_XADDW}, {"XADDB", ASM_OP_XADDB},
    {"CMPXCHGQ", ASM_OP_CMPXCHGQ}, {"CMPXCHGD", ASM_OP_CMPXCHGD}, {"CMPXCHGW", ASM_OP_CMPXCHGW}, {"CMPXCHGB", ASM_OP_CMPXCHGB},
    {"REP_MOVSB", ASM_OP_REP_MOVSB}, {"REP_MOVSW", ASM_OP_REP_MOVSW}, {"REP_MOVSD", ASM_OP_REP_MOVSD}, {"REP_MOVSQ", ASM_OP_REP_MOVSQ},
    {"REP_STOSB", ASM_OP_REP_STOSB}, {"REP_STOSW", ASM_OP_REP_STOSW}, {"REP_STOSD", ASM_OP_REP_STOSD}, {"REP_STOSQ", ASM_OP_REP_STOSQ},
    {"REP_LODSB", ASM_OP_REP_LODSB}, {"REP_LODSW", ASM_OP_REP_LODSW}, {"REP_LODSD", ASM_OP_REP_LODSD}, {"REP_LODSQ", ASM_OP_REP_LODSQ},
    {"REP_CMPSB", ASM_OP_REP_CMPSB}, {"REP_CMPSW", ASM_OP_REP_CMPSW}, {"REP_CMPSD", ASM_OP_REP_CMPSD}, {"REP_CMPSQ", ASM_OP_REP_CMPSQ},
    {"REP_SCASB", ASM_OP_REP_SCASB}, {"REP_SCASW", ASM_OP_REP_SCASW}, {"REP_SCASD", ASM_OP_REP_SCASD}, {"REP_SCASQ", ASM_OP_REP_SCASQ},
    {"REPNE_MOVSB", ASM_OP_REPNE_MOVSB}, {"REPNE_MOVSW", ASM_OP_REPNE_MOVSW}, {"REPNE_MOVSD", ASM_OP_REPNE_MOVSD}, {"REPNE_MOVSQ", ASM_OP_REPNE_MOVSQ},
    {"REPNE_STOSB", ASM_OP_REPNE_STOSB}, {"REPNE_STOSW", ASM_OP_REPNE_STOSW}, {"REPNE_STOSD", ASM_OP_REPNE_STOSD}, {"REPNE_STOSQ", ASM_OP_REPNE_STOSQ},
    {"REPNE_LODSB", ASM_OP_REPNE_LODSB}, {"REPNE_LODSW", ASM_OP_REPNE_LODSW}, {"REPNE_LODSD", ASM_OP_REPNE_LODSD}, {"REPNE_LODSQ", ASM_OP_REPNE_LODSQ},
    {"REPNE_CMPSB", ASM_OP_REPNE_CMPSB}, {"REPNE_CMPSW", ASM_OP_REPNE_CMPSW}, {"REPNE_CMPSD", ASM_OP_REPNE_CMPSD}, {"REPNE_CMPSQ", ASM_OP_REPNE_CMPSQ},
    {"REPNE_SCASB", ASM_OP_REPNE_SCASB}, {"REPNE_SCASW", ASM_OP_REPNE_SCASW}, {"REPNE_SCASD", ASM_OP_REPNE_SCASD}, {"REPNE_SCASQ", ASM_OP_REPNE_SCASQ},
    {"REPZ_MOVSB", ASM_OP_REPZ_MOVSB}, {"REPZ_MOVSW", ASM_OP_REPZ_MOVSW}, {"REPZ_MOVSD", ASM_OP_REPZ_MOVSD}, {"REPZ_MOVSQ", ASM_OP_REPZ_MOVSQ},
    {"REPZ_STOSB", ASM_OP_REPZ_STOSB}, {"REPZ_STOSW", ASM_OP_REPZ_STOSW}, {"REPZ_STOSD", ASM_OP_REPZ_STOSD}, {"REPZ_STOSQ", ASM_OP_REPZ_STOSQ},
    {"REPZ_LODSB", ASM_OP_REPZ_LODSB}, {"REPZ_LODSW", ASM_OP_REPZ_LODSW}, {"REPZ_LODSD", ASM_OP_REPZ_LODSD}, {"REPZ_LODSQ", ASM_OP_REPZ_LODSQ},
    {"REPZ_CMPSB", ASM_OP_REPZ_CMPSB}, {"REPZ_CMPSW", ASM_OP_REPZ_CMPSW}, {"REPZ_CMPSD", ASM_OP_REPZ_CMPSD}, {"REPZ_CMPSQ", ASM_OP_REPZ_CMPSQ},
    {"REPZ_SCASB", ASM_OP_REPZ_SCASB}, {"REPZ_SCASW", ASM_OP_REPZ_SCASW}, {"REPZ_SCASD", ASM_OP_REPZ_SCASD}, {"REPZ_SCASQ", ASM_OP_REPZ_SCASQ},
    {"SSE_MOVAPS", ASM_OP_SSE_MOVAPS}, {"SSE_MOVUPS", ASM_OP_SSE_MOVUPS}, {"SSE_MOVSS", ASM_OP_SSE_MOVSS}, {"SSE_MOVSD", ASM_OP_SSE_MOVSD},
    {"SSE_MOVDQA", ASM_OP_SSE_MOVDQA}, {"SSE_MOVDQU", ASM_OP_SSE_MOVDQU},
    {"SSE_ADDPS", ASM_OP_SSE_ADDPS}, {"SSE_ADDPD", ASM_OP_SSE_ADDPD}, {"SSE_ADDSS", ASM_OP_SSE_ADDSS}, {"SSE_ADDSD", ASM_OP_SSE_ADDSD},
    {"SSE_SUBPS", ASM_OP_SSE_SUBPS}, {"SSE_SUBPD", ASM_OP_SSE_SUBPD}, {"SSE_SUBSS", ASM_OP_SSE_SUBSS}, {"SSE_SUBSD", ASM_OP_SSE_SUBSD},
    {"SSE_MULPS", ASM_OP_SSE_MULPS}, {"SSE_MULPD", ASM_OP_SSE_MULPD}, {"SSE_MULSS", ASM_OP_SSE_MULSS}, {"SSE_MULSD", ASM_OP_SSE_MULSD},
    {"SSE_DIVPS", ASM_OP_SSE_DIVPS}, {"SSE_DIVPD", ASM_OP_SSE_DIVPD}, {"SSE_DIVSS", ASM_OP_SSE_DIVSS}, {"SSE_DIVSD", ASM_OP_SSE_DIVSD},
    {"SSE_SQRTPS", ASM_OP_SSE_SQRTPS}, {"SSE_SQRTPD", ASM_OP_SSE_SQRTPD}, {"SSE_SQRTSS", ASM_OP_SSE_SQRTSS}, {"SSE_SQRTSD", ASM_OP_SSE_SQRTSD},
    {"SSE_MINPS", ASM_OP_SSE_MINPS}, {"SSE_MINPD", ASM_OP_SSE_MINPD}, {"SSE_MAXPS", ASM_OP_SSE_MAXPS}, {"SSE_MAXPD", ASM_OP_SSE_MAXPD},
    {"SSE_ANDPS", ASM_OP_SSE_ANDPS}, {"SSE_ANDPD", ASM_OP_SSE_ANDPD}, {"SSE_ORPS", ASM_OP_SSE_ORPS}, {"SSE_ORPD", ASM_OP_SSE_ORPD},
    {"SSE_XORPS", ASM_OP_SSE_XORPS}, {"SSE_XORPD", ASM_OP_SSE_XORPD},
    {"SSE_PXOR", ASM_OP_SSE_PXOR}, {"SSE_PAND", ASM_OP_SSE_PAND}, {"SSE_POR", ASM_OP_SSE_POR}, {"SSE_PANDN", ASM_OP_SSE_PANDN},
    {"SSE_CMPPS", ASM_OP_SSE_CMPPS}, {"SSE_CMPPD", ASM_OP_SSE_CMPPD}, {"SSE_CMPSS", ASM_OP_SSE_CMPSS}, {"SSE_CMPSD", ASM_OP_SSE_CMPSD},
    {"SSE_COMISS", ASM_OP_SSE_COMISS}, {"SSE_COMISD", ASM_OP_SSE_COMISD}, {"SSE_UCOMISS", ASM_OP_SSE_UCOMISS}, {"SSE_UCOMISD", ASM_OP_SSE_UCOMISD},
    {"SSE_CVTSI2SS", ASM_OP_SSE_CVTSI2SS}, {"SSE_CVTSI2SD", ASM_OP_SSE_CVTSI2SD},
    {"SSE_CVTSS2SI", ASM_OP_SSE_CVTSS2SI}, {"SSE_CVTSD2SI", ASM_OP_SSE_CVTSD2SI},
    {"SSE_CVTPS2PD", ASM_OP_SSE_CVTPS2PD}, {"SSE_CVTPD2PS", ASM_OP_SSE_CVTPD2PS},
    {"SSE_CVTSS2SD", ASM_OP_SSE_CVTSS2SD}, {"SSE_CVTSD2SS", ASM_OP_SSE_CVTSD2SS},
    {"AVX_VMOVAPS", ASM_OP_AVX_VMOVAPS}, {"AVX_VMOVUPS", ASM_OP_AVX_VMOVUPS}, {"AVX_VMOVSS", ASM_OP_AVX_VMOVSS}, {"AVX_VMOVSD", ASM_OP_AVX_VMOVSD},
    {"AVX_VMOVDQA", ASM_OP_AVX_VMOVDQA}, {"AVX_VMOVDQU", ASM_OP_AVX_VMOVDQU},
    {"AVX_VADDPS", ASM_OP_AVX_VADDPS}, {"AVX_VADDPD", ASM_OP_AVX_VADDPD}, {"AVX_VADDSS", ASM_OP_AVX_VADDSS}, {"AVX_VADDSD", ASM_OP_AVX_VADDSD},
    {"AVX_VSUBPS", ASM_OP_AVX_VSUBPS}, {"AVX_VSUBPD", ASM_OP_AVX_VSUBPD}, {"AVX_VSUBSS", ASM_OP_AVX_VSUBSS}, {"AVX_VSUBSD", ASM_OP_AVX_VSUBSD},
    {"AVX_VMULPS", ASM_OP_AVX_VMULPS}, {"AVX_VMULPD", ASM_OP_AVX_VMULPD}, {"AVX_VMULSS", ASM_OP_AVX_VMULSS}, {"AVX_VMULSD", ASM_OP_AVX_VMULSD},
    {"AVX_VDIVPS", ASM_OP_AVX_VDIVPS}, {"AVX_VDIVPD", ASM_OP_AVX_VDIVPD}, {"AVX_VDIVSS", ASM_OP_AVX_VDIVSS}, {"AVX_VDIVSD", ASM_OP_AVX_VDIVSD},
    {"AVX_VSQRTPS", ASM_OP_AVX_VSQRTPS}, {"AVX_VSQRTPD", ASM_OP_AVX_VSQRTPD},
    {"AVX_VANDPS", ASM_OP_AVX_VANDPS}, {"AVX_VANDPD", ASM_OP_AVX_VANDPD}, {"AVX_VORPS", ASM_OP_AVX_VORPS}, {"AVX_VORPD", ASM_OP_AVX_VORPD},
    {"AVX_VXORPS", ASM_OP_AVX_VXORPS}, {"AVX_VXORPD", ASM_OP_AVX_VXORPD},
    {"AVX_VPXOR", ASM_OP_AVX_VPXOR}, {"AVX_VPAND", ASM_OP_AVX_VPAND}, {"AVX_VPOR", ASM_OP_AVX_VPOR},
    {"AVX_VZEROUPPER", ASM_OP_AVX_VZEROUPPER}, {"AVX_VZEROALL", ASM_OP_AVX_VZEROALL},
};

static AsmRegEntry g_rwaAsmRegs[] = {
    {"QAX", ASM_REG_QAX}, {"QBX", ASM_REG_QBX}, {"QCX", ASM_REG_QCX}, {"QDX", ASM_REG_QDX},
    {"QSI", ASM_REG_QSI}, {"QDI", ASM_REG_QDI}, {"QSP", ASM_REG_QSP}, {"QBP", ASM_REG_QBP},
    {"Q8", ASM_REG_Q8}, {"Q9", ASM_REG_Q9}, {"Q10", ASM_REG_Q10}, {"Q11", ASM_REG_Q11},
    {"Q12", ASM_REG_Q12}, {"Q13", ASM_REG_Q13}, {"Q14", ASM_REG_Q14}, {"Q15", ASM_REG_Q15},
    {"QIP", ASM_REG_QIP},
    {"DAX", ASM_REG_DAX}, {"DBX", ASM_REG_DBX}, {"DCX", ASM_REG_DCX}, {"DDX", ASM_REG_DDX},
    {"DSI", ASM_REG_DSI}, {"DDI", ASM_REG_DDI}, {"DSP", ASM_REG_DSP}, {"DBP", ASM_REG_DBP},
    {"Q8D", ASM_REG_Q8D}, {"Q9D", ASM_REG_Q9D}, {"Q10D", ASM_REG_Q10D}, {"Q11D", ASM_REG_Q11D},
    {"Q12D", ASM_REG_Q12D}, {"Q13D", ASM_REG_Q13D}, {"Q14D", ASM_REG_Q14D}, {"Q15D", ASM_REG_Q15D},
    {"WAX", ASM_REG_WAX}, {"WBX", ASM_REG_WBX}, {"WCX", ASM_REG_WCX}, {"WDX", ASM_REG_WDX},
    {"WSI", ASM_REG_WSI}, {"WDI", ASM_REG_WDI}, {"WSP", ASM_REG_WSP}, {"WBP", ASM_REG_WBP},
    {"Q8W", ASM_REG_Q8W}, {"Q9W", ASM_REG_Q9W}, {"Q10W", ASM_REG_Q10W}, {"Q11W", ASM_REG_Q11W},
    {"Q12W", ASM_REG_Q12W}, {"Q13W", ASM_REG_Q13W}, {"Q14W", ASM_REG_Q14W}, {"Q15W", ASM_REG_Q15W},
    {"BAL", ASM_REG_BAL}, {"BBL", ASM_REG_BBL}, {"BCL", ASM_REG_BCL}, {"BDL", ASM_REG_BDL},
    {"BAH", ASM_REG_BAH}, {"BBH", ASM_REG_BBH}, {"BCH", ASM_REG_BCH}, {"BDH", ASM_REG_BDH},
    {"BSIL", ASM_REG_BSIL}, {"BDIL", ASM_REG_BDIL}, {"BSPL", ASM_REG_BSPL}, {"BBPL", ASM_REG_BBPL},
    {"Q8B", ASM_REG_Q8B}, {"Q9B", ASM_REG_Q9B}, {"Q10B", ASM_REG_Q10B}, {"Q11B", ASM_REG_Q11B},
    {"Q12B", ASM_REG_Q12B}, {"Q13B", ASM_REG_Q13B}, {"Q14B", ASM_REG_Q14B}, {"Q15B", ASM_REG_Q15B},
    {"XMM0", ASM_REG_XMM0}, {"XMM1", ASM_REG_XMM1}, {"XMM2", ASM_REG_XMM2}, {"XMM3", ASM_REG_XMM3},
    {"XMM4", ASM_REG_XMM4}, {"XMM5", ASM_REG_XMM5}, {"XMM6", ASM_REG_XMM6}, {"XMM7", ASM_REG_XMM7},
    {"XMM8", ASM_REG_XMM8}, {"XMM9", ASM_REG_XMM9}, {"XMM10", ASM_REG_XMM10}, {"XMM11", ASM_REG_XMM11},
    {"XMM12", ASM_REG_XMM12}, {"XMM13", ASM_REG_XMM13}, {"XMM14", ASM_REG_XMM14}, {"XMM15", ASM_REG_XMM15},
    {"YMM0", ASM_REG_YMM0}, {"YMM1", ASM_REG_YMM1}, {"YMM2", ASM_REG_YMM2}, {"YMM3", ASM_REG_YMM3},
    {"YMM4", ASM_REG_YMM4}, {"YMM5", ASM_REG_YMM5}, {"YMM6", ASM_REG_YMM6}, {"YMM7", ASM_REG_YMM7},
    {"YMM8", ASM_REG_YMM8}, {"YMM9", ASM_REG_YMM9}, {"YMM10", ASM_REG_YMM10}, {"YMM11", ASM_REG_YMM11},
    {"YMM12", ASM_REG_YMM12}, {"YMM13", ASM_REG_YMM13}, {"YMM14", ASM_REG_YMM14}, {"YMM15", ASM_REG_YMM15},
    {"ST0", ASM_REG_ST0}, {"ST1", ASM_REG_ST1}, {"ST2", ASM_REG_ST2}, {"ST3", ASM_REG_ST3},
    {"ST4", ASM_REG_ST4}, {"ST5", ASM_REG_ST5}, {"ST6", ASM_REG_ST6}, {"ST7", ASM_REG_ST7},
    {"CR0", ASM_REG_CR0}, {"CR2", ASM_REG_CR2}, {"CR3", ASM_REG_CR3}, {"CR4", ASM_REG_CR4}, {"CR8", ASM_REG_CR8},
    {"DR0", ASM_REG_DR0}, {"DR1", ASM_REG_DR1}, {"DR2", ASM_REG_DR2}, {"DR3", ASM_REG_DR3},
    {"DR6", ASM_REG_DR6}, {"DR7", ASM_REG_DR7},
};

U64 AsmOpLookup(const char* rwszName) {
    for (U64 i = 0; i < sizeof(g_rwaAsmOps) / sizeof(AsmEntry); i++) {
        if (strcmp(g_rwaAsmOps[i].rwszName, rwszName) == 0) return g_rwaAsmOps[i].rwullOp;
    }
    return 0;
}

U64 AsmRegLookup(const char* rwszName) {
    for (U64 i = 0; i < sizeof(g_rwaAsmRegs) / sizeof(AsmRegEntry); i++) {
        if (strcmp(g_rwaAsmRegs[i].rwszName, rwszName) == 0) return g_rwaAsmRegs[i].rwullReg;
    }
    return 0;
}

const char* AsmOpName(U64 rwullOp) {
    for (U64 i = 0; i < sizeof(g_rwaAsmOps) / sizeof(AsmEntry); i++) {
        if (g_rwaAsmOps[i].rwullOp == rwullOp) return g_rwaAsmOps[i].rwszName;
    }
    return "?";
}

const char* AsmRegName(U64 rwullReg) {
    for (U64 i = 0; i < sizeof(g_rwaAsmRegs) / sizeof(AsmRegEntry); i++) {
        if (g_rwaAsmRegs[i].rwullReg == rwullReg) return g_rwaAsmRegs[i].rwszName;
    }
    return "?";
}

int AsmRegIsGPR(U64 rwullReg) {
    return rwullReg >= ASM_REG_QAX && rwullReg <= ASM_REG_Q15B;
}

int AsmRegIs64(U64 rwullReg) {
    if (rwullReg >= ASM_REG_QAX && rwullReg <= ASM_REG_QBP) return 1;
    if (rwullReg >= ASM_REG_Q8 && rwullReg <= ASM_REG_Q15) return 1;
    if (rwullReg == ASM_REG_QIP) return 1;
    return 0;
}

int AsmRegIs32(U64 rwullReg) {
    if (rwullReg >= ASM_REG_DAX && rwullReg <= ASM_REG_DBP) return 1;
    if (rwullReg >= ASM_REG_Q8D && rwullReg <= ASM_REG_Q15D) return 1;
    return 0;
}

int AsmRegIs16(U64 rwullReg) {
    if (rwullReg >= ASM_REG_WAX && rwullReg <= ASM_REG_WBP) return 1;
    if (rwullReg >= ASM_REG_Q8W && rwullReg <= ASM_REG_Q15W) return 1;
    return 0;
}

int AsmRegIs8(U64 rwullReg) {
    if (rwullReg >= ASM_REG_BAL && rwullReg <= ASM_REG_Q15B) return 1;
    return 0;
}

int AsmRegIsXMM(U64 rwullReg) {
    return rwullReg >= ASM_REG_XMM0 && rwullReg <= ASM_REG_XMM15;
}

int AsmRegIsYMM(U64 rwullReg) {
    return rwullReg >= ASM_REG_YMM0 && rwullReg <= ASM_REG_YMM15;
}

int AsmRegIsST(U64 rwullReg) {
    return rwullReg >= ASM_REG_ST0 && rwullReg <= ASM_REG_ST7;
}

int AsmRegIsCR(U64 rwullReg) {
    return rwullReg == ASM_REG_CR0 || rwullReg == ASM_REG_CR2 ||
           rwullReg == ASM_REG_CR3 || rwullReg == ASM_REG_CR4 || rwullReg == ASM_REG_CR8;
}

int AsmRegIsDR(U64 rwullReg) {
    return rwullReg >= ASM_REG_DR0 && rwullReg <= ASM_REG_DR7;
}

U64 AsmRegLow3(U64 rwullReg) {
    switch (rwullReg) {
        case ASM_REG_QAX: case ASM_REG_DAX: case ASM_REG_WAX: case ASM_REG_BAL: return 0;
        case ASM_REG_QCX: case ASM_REG_DCX: case ASM_REG_WCX: case ASM_REG_BCL: return 1;
        case ASM_REG_QDX: case ASM_REG_DDX: case ASM_REG_WDX: case ASM_REG_BDL: return 2;
        case ASM_REG_QBX: case ASM_REG_DBX: case ASM_REG_WBX: case ASM_REG_BBL: return 3;
        case ASM_REG_QSP: case ASM_REG_DSP: case ASM_REG_WSP: case ASM_REG_BSPL: return 4;
        case ASM_REG_QBP: case ASM_REG_DBP: case ASM_REG_WBP: case ASM_REG_BBPL: return 5;
        case ASM_REG_QSI: case ASM_REG_DSI: case ASM_REG_WSI: case ASM_REG_BSIL: return 6;
        case ASM_REG_QDI: case ASM_REG_DDI: case ASM_REG_WDI: case ASM_REG_BDIL: return 7;

        case ASM_REG_BAH: return 4;
        case ASM_REG_BCH: return 5;
        case ASM_REG_BDH: return 6;
        case ASM_REG_BBH: return 7;

        case ASM_REG_Q8: case ASM_REG_Q8D: case ASM_REG_Q8W: case ASM_REG_Q8B: return 0;
        case ASM_REG_Q9: case ASM_REG_Q9D: case ASM_REG_Q9W: case ASM_REG_Q9B: return 1;
        case ASM_REG_Q10: case ASM_REG_Q10D: case ASM_REG_Q10W: case ASM_REG_Q10B: return 2;
        case ASM_REG_Q11: case ASM_REG_Q11D: case ASM_REG_Q11W: case ASM_REG_Q11B: return 3;
        case ASM_REG_Q12: case ASM_REG_Q12D: case ASM_REG_Q12W: case ASM_REG_Q12B: return 4;
        case ASM_REG_Q13: case ASM_REG_Q13D: case ASM_REG_Q13W: case ASM_REG_Q13B: return 5;
        case ASM_REG_Q14: case ASM_REG_Q14D: case ASM_REG_Q14W: case ASM_REG_Q14B: return 6;
        case ASM_REG_Q15: case ASM_REG_Q15D: case ASM_REG_Q15W: case ASM_REG_Q15B: return 7;

        case ASM_REG_QIP: return 5;

        case ASM_REG_XMM0: case ASM_REG_XMM8: return 0;
        case ASM_REG_XMM1: case ASM_REG_XMM9: return 1;
        case ASM_REG_XMM2: case ASM_REG_XMM10: return 2;
        case ASM_REG_XMM3: case ASM_REG_XMM11: return 3;
        case ASM_REG_XMM4: case ASM_REG_XMM12: return 4;
        case ASM_REG_XMM5: case ASM_REG_XMM13: return 5;
        case ASM_REG_XMM6: case ASM_REG_XMM14: return 6;
        case ASM_REG_XMM7: case ASM_REG_XMM15: return 7;

        case ASM_REG_YMM0: case ASM_REG_YMM8: return 0;
        case ASM_REG_YMM1: case ASM_REG_YMM9: return 1;
        case ASM_REG_YMM2: case ASM_REG_YMM10: return 2;
        case ASM_REG_YMM3: case ASM_REG_YMM11: return 3;
        case ASM_REG_YMM4: case ASM_REG_YMM12: return 4;
        case ASM_REG_YMM5: case ASM_REG_YMM13: return 5;
        case ASM_REG_YMM6: case ASM_REG_YMM14: return 6;
        case ASM_REG_YMM7: case ASM_REG_YMM15: return 7;

        case ASM_REG_ST0: return 0;
        case ASM_REG_ST1: return 1;
        case ASM_REG_ST2: return 2;
        case ASM_REG_ST3: return 3;
        case ASM_REG_ST4: return 4;
        case ASM_REG_ST5: return 5;
        case ASM_REG_ST6: return 6;
        case ASM_REG_ST7: return 7;

        case ASM_REG_CR0: return 0;
        case ASM_REG_CR2: return 2;
        case ASM_REG_CR3: return 3;
        case ASM_REG_CR4: return 4;
        case ASM_REG_CR8: return 0;

        case ASM_REG_DR0: return 0;
        case ASM_REG_DR1: return 1;
        case ASM_REG_DR2: return 2;
        case ASM_REG_DR3: return 3;
        case ASM_REG_DR6: return 6;
        case ASM_REG_DR7: return 7;
    }
    return 0;
}

int AsmRegNeedsRex(U64 rwullReg) {
    if (rwullReg >= ASM_REG_Q8 && rwullReg <= ASM_REG_Q15) return 1;
    if (rwullReg >= ASM_REG_Q8D && rwullReg <= ASM_REG_Q15D) return 1;
    if (rwullReg >= ASM_REG_Q8W && rwullReg <= ASM_REG_Q15W) return 1;
    if (rwullReg >= ASM_REG_Q8B && rwullReg <= ASM_REG_Q15B) return 1;
    if (rwullReg >= ASM_REG_BSIL && rwullReg <= ASM_REG_BBPL) return 1;
    if (rwullReg >= ASM_REG_XMM8 && rwullReg <= ASM_REG_XMM15) return 1;
    if (rwullReg >= ASM_REG_YMM8 && rwullReg <= ASM_REG_YMM15) return 1;
    return 0;
}

U64 AsmRegRexBit(U64 rwullReg) {
    if (rwullReg >= ASM_REG_Q8 && rwullReg <= ASM_REG_Q15) return 1;
    if (rwullReg >= ASM_REG_Q8D && rwullReg <= ASM_REG_Q15D) return 1;
    if (rwullReg >= ASM_REG_Q8W && rwullReg <= ASM_REG_Q15W) return 1;
    if (rwullReg >= ASM_REG_Q8B && rwullReg <= ASM_REG_Q15B) return 1;
    if (rwullReg >= ASM_REG_XMM8 && rwullReg <= ASM_REG_XMM15) return 1;
    if (rwullReg >= ASM_REG_YMM8 && rwullReg <= ASM_REG_YMM15) return 1;
    return 0;
}
