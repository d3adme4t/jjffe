#pragma once
#include "jjffe/span_arena.h"
#include "jjffe/game_clock.h"

namespace jjffe {

// Reserved-address placeholders: many symbols are declared for ASM parity but
// not yet referenced from every TU. Silence -Wunused-variable for this block only.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
// Batch3: independent
static uint32_t DATA_001697[105] = {};
// DATA_001698 DATA_001699: src/core/services_tables.inc (generated from ffedat.asm).
// Batch3: 68×0x10 opcode records (shl idx,4)
static uint8_t DATA_001701[1088] = {};
#define DATA_001702 DATA_001701[4]
#define DATA_001703 DATA_001701[8]
#define DATA_001704 DATA_001701[12]
// Batch3: 27×12 (lea*3;*4); word fields at +0/+4/+8
static uint8_t DATA_001705[324] = {};
#define DATA_001706 DATA_001705[4]
#define DATA_001707 DATA_001705[8]
// DATA_001913 DATA_002106: mission strings in src/core/services_tables.inc.
static uint8_t DATA_002244[2] = {0, 0};  // Batch4 RC5: ffedat db 0,0 (empty C string)
static uint32_t DATA_002245 = 0;                       // SCALAR [HIGH]
// Cluster 4 — journal/event entries: 170 × 0x10 (ffe.asm cmp ebx,0xaa; stride 0x10).
// Field labels at +0/+4/+8/+12/+14 into entry 0. Init from ffedat in core.cpp.
static uint8_t DATA_002280[170 * 0x10] = {};
#define DATA_002281 DATA_002280[4]
#define DATA_002282 DATA_002280[8]
#define DATA_002283 DATA_002280[12]
#define DATA_002284 DATA_002280[14]
// Batch3: independent
static uint32_t DATA_002289[6] = {};
// Batch3: independent
static uint32_t DATA_002290[5] = {};
// Batch3: independent
static uint32_t DATA_002291[8] = {};
// Batch3: journal mission.dat offsets; 293=&292[1]
static uint32_t DATA_002292[673] = {};
#define DATA_002293 DATA_002292[1]
static uint32_t DATA_002295 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_002296 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_002297 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_002298 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_002299 = 0;                       // SCALAR [HIGH]
// ffedat: the five journal ids fed to FUNC_000035 by FUNC_000088 (ffe.asm:11224).
static const int32_t DATA_002300[5] = {0x14, 0x12, 0x11, 0x13, 0x15};
// Batch3: independent
static uint32_t DATA_002308[3] = {};
// Batch3: independent
static const uint8_t* DATA_002309 = nullptr;           // 'rb' fopen mode (blob image)
static uint32_t DATA_002310 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_002311 = 1;                       // ffedat.asm: db 0x1 (first call writes the banner)
// FUNC_000107/108 trace-file strings (ffedat.asm): the ASM pushes each label's
// address; these hold the blob image's address (InitDataPointers).
static const uint8_t* DATA_002312 = nullptr;
static const uint8_t* DATA_002313 = nullptr;
static const uint8_t* DATA_002314 = nullptr;
static const uint8_t* DATA_002315 = nullptr;
static const uint8_t* DATA_002316 = nullptr;
static const uint8_t* DATA_002317 = nullptr;
static uint32_t DATA_002318 = 0;                       // SCALAR [HIGH]
static uintptr_t DATA_002320 = 0;                      // AVI FILE* (ffe.asm:13875 mov [DATA_002320],ebx)
static const char* const* DATA_002321 = nullptr;       // AVI name table (DATA_005258-005284)
static uint32_t DATA_002322 = 0;                       // SCALAR [HIGH]
// ffedat: DATA_002323 is 60-byte table; first dword used as video-active flag
static uint32_t DATA_002323 = 0;
// ffedat string blob: paths + "Press a key to exit" @0x39 + "Insert CDROM" @0x4d
static char DATA_002324[] =
    "Sound channel called with wrong parameter\n\0"
    "%c:\0\\DATA\0AVI\0Press a key to exit\0Insert CDROM";
static uint32_t DATA_004682 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004892 = 0;                       // SCALAR [HIGH]
static const uint8_t* DATA_004893 = nullptr;           // 21-byte hotkey table (blob image)
static uint32_t DATA_004894 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004895 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint32_t DATA_004896[5] = {};
static uint32_t DATA_004897 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004898 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004899 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004900 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004901 = 0;                       // SCALAR [HIGH]
// Hot-area lists handed to FUNC_001440 (5 dwords per area, -1 terminated); the
// ASM pushes the label address. Blob image address (InitDataPointers).
static const uint8_t* DATA_004906 = nullptr;
// ffedat `dd DATA_004898, 4903, 4902, 4904, 4905`: each system page's hot-area
// list (FUNC_000216/224 -> FUNC_001440, FUNC_000225). Blob image addresses
// (InitDataPointers), so LP64 pointers where the ASM has dwords.
static const uint8_t* DATA_004907[5] = {};
// Batch3: independent
static uint32_t DATA_004908[5] = {};
static const uint8_t* DATA_004909 = nullptr;
static uint32_t DATA_004910 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004911 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004912 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004913 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004914 = 0;                       // SCALAR [HIGH]
static const uint8_t* DATA_004916 = nullptr;           // 'Jameson' (blob image)
static uint32_t DATA_004917 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_004918 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_005025 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_005026 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_005037 = 0;                       // SCALAR [HIGH]
// Batch4 RC2: ffedat.asm DATA_005039 — 154-byte static table (not scalar).
// FUNC_000236: (int32_t*)(intptr_t)DATA_005039 — array decays to address.
static uint8_t DATA_005039[154] = {
    0x05, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x08, 0x00,
    0x00, 0x00, 0xf4, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x0f, 0x00, 0x00, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x14, 0x00, 0x00, 0x00, 0x58, 0x02,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00,
    0x14, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0xc4, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x28, 0x00, 0x00, 0x00, 0xb8, 0x0b, 0x00, 0x00,
    0x16, 0x00, 0x00, 0x00, 0x07, 0x00, 0x50, 0x00,
    0x00, 0x00, 0xa0, 0x0f, 0x00, 0x00, 0x63, 0x00,
    0x00, 0x00, 0x07, 0x00, 0x2c, 0x01, 0x00, 0x00,
    0x10, 0x27, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x07, 0x00,
};
static_assert(sizeof(DATA_005039) == 154, "Batch4 RC2 DATA_005039 size");
static uint32_t DATA_005053 = 0;                       // SCALAR [HIGH]
// Cluster 6 — 27 × 0xc (ffe.asm clamp idx to 0x1a; lea*3;*4 = *0xc).
// 055/056/057 = fields +0/+4/+8. DATA_005058 is a separate 9-byte LUT.
static uint8_t DATA_005055[27 * 0xc] = {};
#define DATA_005056 DATA_005055[4]
#define DATA_005057 DATA_005055[8]
static uint8_t DATA_005058[9] = {};
static int32_t* DATA_005088 = nullptr;                 // POINTER [HIGH]
static uint32_t DATA_005090 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_005092 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_005094 = 0;                       // SCALAR [HIGH]
static const uint8_t* DATA_005095 = nullptr;           // -1-terminated dword table (blob image)
static uint32_t DATA_005096 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_005097 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_005098 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_005099 = 0;                       // SCALAR [HIGH]
// Cluster 1 — ConButton[82]: stride 0x3e (ffe.asm cmp esi,0x52). DATA_005100 is
// the image-bank dword; button array starts at xpos. Visible/cur/radio at +0x30/+0x34/+0x3a.
static uint32_t DATA_005100 = 0;
static uint8_t DATA_CONBUTTON[82 * 0x3e] = {};
#define DATA_005101 (DATA_CONBUTTON[0x30])
#define DATA_005102 (DATA_CONBUTTON[0x34])
#define DATA_005103 (DATA_CONBUTTON[0x3a])
static uint32_t DATA_005104 = 0;                       // SCALAR [HIGH]
// ffedat.asm 5408: equipment bit -> name code; bits 30/31 run on into DATA_005107
static const uint32_t DATA_005106[30] = {
    0x9888, 0x9888, 0x9889, 0x988c, 0x9892, 0x988d, 0x988b, 0x989c, 0x988f, 0x98a7,
    0x989e, 0x989f, 0x9899, 0x9893, 0x9884, 0x989d, 0x9887, 0x9883, 0x98b2, 0x98b0,
    0x98b1, 0x9885, 0x9886, 0x9880, 0x98b3, 0x987f, 0x9898, 0x9881, 0x9882, 0x98b4};
// Batch3: independent
static uint32_t DATA_005107[151] = {};
static const uint8_t* DATA_005108 = nullptr;           // hot-area list (blob image)
// ffedat DATA_005110: 9 descending rank thresholds (36 bytes)
static int32_t DATA_005110[9] = {
    0x00177000, 0x000bb800, 0x0003e800, 0x00008000, 0x00004000,
    0x00002000, 0x00001000, 0x00000800, 0x00000400
};
// Batch3: independent
static uint32_t DATA_005111[15] = {};
// Batch3: independent
static uint32_t DATA_005112[9] = {};
// Batch3: independent
static uint32_t DATA_005113 = 0;  // Batch3: kept scalar (foot=4, no multi-index)
// Batch3: independent
static uint32_t DATA_005114[13] = {};
// Batch3: independent
static uint8_t DATA_005115[3] = {};
// Batch3: independent
static uint8_t DATA_005116[5] = {};
// Batch3: independent
static uint32_t DATA_005117[8] = {};
// Batch3: independent
static uint32_t DATA_005118[7] = {};
// ffedat: 7 signed bytes each (FUNC_000300 indexes by cargo type)
static int8_t DATA_005119[7] = {};
static int8_t DATA_005120[7] = {};
// PhysObj template (0x152) — source for FUNC_000928 / op 0x30
static uint8_t DATA_005121[338] = {};
static uint32_t DATA_005122 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint16_t DATA_005123[2] = {};
// Batch3: independent
static uint16_t DATA_005124[46] = {};
// DATA_005109 005125-005128 005258-005287 005298 005299 005320-005333 006159-006161:
// src/core/services_tables.inc (generated from ffedat.asm).
// Cluster A — item table: 66 × 0x16 (ffe.asm cmp idx,0x42; stride 0x16).
// Field labels are aliases into entry 0 (ASM: +0/+8/+16/+19). Init from ffedat in core.cpp.
static uint8_t DATA_005289[66 * 0x16] = {};
#define DATA_005290 DATA_005289[8]
#define DATA_005291 DATA_005289[16]
#define DATA_005292 DATA_005289[19]
// Batch3: independent FLAG: SKIP_EXPAND and0xff vs foot 60; keep foot
static uint32_t DATA_005300[15] = {};
// Batch3: independent
static uint32_t DATA_005301[15] = {};
// Cluster B — DATA_005302[256] score + DATA_005303 32-pair table live in core.cpp
// (ffedat packs them adjacent; ASM indexes score 0..255 and walks 32 pairs from 303)
// Cluster 2 — menu/list entries: 12 × 0x28 (ffe.asm lea*5;*8 = *0x28; ffedat 304..314).
// Fields 304..313 at +0/+4/+8/+12/+16/+20/+24/+28/+32/+36. Init in core.cpp.
static uint8_t DATA_005304[12 * 0x28] = {};
#define DATA_005305 DATA_005304[4]
#define DATA_005306 DATA_005304[8]
#define DATA_005307 DATA_005304[12]
#define DATA_005308 DATA_005304[16]
#define DATA_005309 DATA_005304[20]
#define DATA_005310 DATA_005304[24]
#define DATA_005311 DATA_005304[28]
#define DATA_005312 DATA_005304[32]
#define DATA_005313 DATA_005304[36]
static uint32_t DATA_005315 = 0;                       // SCALAR [HIGH]
// FUNC_001572 records, ffedat db 0x5, 0x4, 0x9 (3 bytes each)
static uint32_t DATA_005316 = 0x00090405;
static uint32_t DATA_005317 = 0x00090405;
// Batch3: independent
static uint8_t DATA_005319[3] = {};
static uint32_t DATA_006162 = 0;                       // SCALAR [HIGH]
// Cluster 5 — 33 × 0x10 (ffe.asm cmp ebx,0x21; shl 4).
static uint8_t DATA_006169[33 * 0x10] = {};
// DATA_006170 (FUNC_000621's generator table) is defined in core.cpp.
// DATA_006171..006174 (ffedat.asm:9498-9527) are one block: FUNC_000638 reads
// [edx*8+DATA_006171] / [edx*8+DATA_006172] across the label boundary.
static uint32_t g_passParcelTables[1 + 15 + 16 + 17] = {
    0x9918,                                                      // DATA_006171
    2, 0x9919, 3, 0x991a, 4, 0x991a, 5, 0x991b, 6, 0x991b, 7, 0x991b, 8, 0x991c, 1,  // DATA_006172
    0, 0, 0, 0, 0, 3, 10, 12, 15, 20, 30, 40, 50, 75, 100, 200,  // DATA_006173
    25, 40, 45, 50, 50, 55, 60, 75, 100, 125, 150, 200, 250, 300, 350, 400, 500,  // DATA_006174
};
using PassParcel6172 = uint32_t[15];
using PassParcel6173 = uint32_t[16];
using PassParcel6174 = uint32_t[17];
static uint32_t& DATA_006171 = g_passParcelTables[0];
static PassParcel6172& DATA_006172 = *reinterpret_cast<PassParcel6172*>(g_passParcelTables + 1);
static PassParcel6173& DATA_006173 = *reinterpret_cast<PassParcel6173*>(g_passParcelTables + 16);
static PassParcel6174& DATA_006174 = *reinterpret_cast<PassParcel6174*>(g_passParcelTables + 32);
/* Dock/bay offset tables for FUNC_000650 (ffedat DATA_006175–179). */
static uint8_t DATA_006175[24] = {};
static uint8_t DATA_006176[24] = {};
static uint8_t DATA_006177[24] = {};
static uint8_t DATA_006178[48] = {};
static uint8_t DATA_006179[72] = {};
/* Ship-type ID tables (ffedat DATA_006180–198) + picker DATA_006199.
 * DATA_006200 is historically &DATA_006199[4] (first entry's ptr field). */
static uint8_t DATA_006180[48] = {};
static uint8_t DATA_006181[64] = {};
static uint8_t DATA_006182[40] = {};
static uint8_t DATA_006183[32] = {};
static uint8_t DATA_006184[36] = {};
static uint8_t DATA_006185[72] = {};
static uint8_t DATA_006186[92] = {};
static uint8_t DATA_006187[108] = {};
static uint8_t DATA_006188[44] = {};
static uint8_t DATA_006189[48] = {};
static uint8_t DATA_006190[60] = {};
static uint8_t DATA_006191[96] = {};
static uint8_t DATA_006192[40] = {};
static uint8_t DATA_006193[8] = {};
static uint8_t DATA_006194[16] = {};
static uint8_t DATA_006195[12] = {};
static uint8_t DATA_006196[80] = {};
static uint8_t DATA_006197[48] = {};
static uint8_t DATA_006198[4] = {};
static uint8_t DATA_006199[152] = {};
#define DATA_006200 (DATA_006199 + 4)
// Batch3: independent
static int32_t DATA_006202[12] = {};
// Batch3: independent
static int32_t DATA_006203[12] = {};
// Batch3: independent
static int32_t DATA_006204[12] = {};
// Batch3: independent
static int32_t DATA_006205[12] = {};
// Batch3: 4×XYZ (lea*3;*4)
static int32_t DATA_006206[12] = {};
#define DATA_006207 DATA_006206[1]
#define DATA_006208 DATA_006206[2]
// Batch3: independent
static int32_t DATA_006209[12] = {};
// Batch3: independent
static uint32_t DATA_006210[6] = {};
// Batch3: independent
static uint32_t DATA_006211[8] = {};
// Batch3: independent
static uint32_t DATA_006212[8] = {};
// Batch3: independent
static uint32_t DATA_006213[8] = {};
// Batch3: independent
// DATA_006214 / DATA_006215 (ffedat.asm:9768-9785) are one table of 16
// (fee, rank) pairs: FUNC_000643 reads [k*8+DATA_006214] / [k*8+DATA_006215].
static uint32_t g_assassinationTable[1 + 31] = {};
using Assassination6215 = uint32_t[31];
static uint32_t& DATA_006214 = g_assassinationTable[0];
static Assassination6215& DATA_006215 = *reinterpret_cast<Assassination6215*>(g_assassinationTable + 1);
static uint32_t DATA_006257 = 0;                       // SCALAR [HIGH]
/* Munition PhysObj template (FUNC_000773 / op 0x29–0x2B) — ffedat.asm:10024 */
static uint8_t DATA_006259[338] = {};
static uint8_t DATA_006260[18] = {};
// Batch3: independent
static uint32_t DATA_006261[14] = {};
// Batch3: independent
static uint32_t DATA_006262[15] = {};
// Batch3: independent
static uint32_t DATA_006263[18] = {};
// Batch3: independent
static uint32_t DATA_006264[18] = {};
static uint8_t DATA_006265[72] = {};
// Batch3: independent
static uint32_t DATA_006266[15] = {};
static uint8_t DATA_006267[20] = {};
// Batch3: independent
static uint32_t DATA_006269[14] = {};
static uint32_t DATA_006270 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_006271 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_006272 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint32_t DATA_006274[12] = {};
// Batch3: independent
static uint32_t DATA_006275[4] = {};
static uint32_t DATA_006278 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_006279 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_006790 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_006792[6] = {};                   // 24-byte zero vector (ffedat 11774)
// Cluster 7a — handcoded sector starpos: 117 × 6-byte vectors + 117 words (ffe.asm *6 / *2;
// max end idx 0x75 from DATA_006801). 794 = +4 into vector.
static uint8_t DATA_006793[117 * 6] = {};
#define DATA_006794 DATA_006793[4]
static uint16_t DATA_006795[117] = {};
static uint32_t DATA_006796 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_006797 = 0;                       // SCALAR [HIGH]
// Handcoded sector keys (y << 16 | x), ffedat.asm:12181-12206: the 0x31 entries
// FUNC_000841 / FUNC_000853 scan; entry i's stars are DATA_006800[i]..DATA_006801[i].
static const int32_t DATA_006799[0x31] = {
    0x15241718, 0x15241719, 0x15251719, 0x15251718, 0x15251717, 0x15241717,
    0x15231717, 0x15231718, 0x15231719, 0x1523171a, 0x1524171a, 0x1525171a,
    0x15261719, 0x15261718, 0x15261717, 0x15251716, 0x15241716, 0x15231716,
    0x15221717, 0x15221718, 0x15221719, 0x15261715, 0x1526171d, 0x1523171d,
    0x15201719, 0x1528171c, 0x1528171e, 0x151e171e, 0x15131714, 0x14fc1712,
    0x150216f1, 0x15321753, 0x152a1774, 0x15441738, 0x15701718, 0x15791686,
    0x152e1714, 0x15291718, 0x15281718, 0x152a1719, 0x1529171a, 0x1529171b,
    0x152b1715, 0x151e1715, 0x151e1716, 0x151f1715, 0x151f1714, 0x151e1714,
    0x15281716,
};
// Cluster 7 — sector start/end star indices: 52 dwords; 801 = &800[1] (shifted ends).
static int32_t DATA_006800[52] = {};
static int32_t* const DATA_006801 = DATA_006800 + 1;
static uint32_t DATA_006802 = 0;                       // SCALAR [HIGH]
// Batch3: star-type lerp ladder. ASM (ffe.asm:56882–56893):
//   i = &0xf;  result = (804[i]-803[i])*frac + 803[i]
// 804 is a shifted view (803[1]), so 804[15] ≡ 803[16] → need [17].
// FLAG: ffedat foot 56 (14 dwords); ASM shifted-view needs 17; expand to 68 B.
static uint32_t DATA_006803[17] = {};
#define DATA_006804 DATA_006803[1]
// Cluster 7b — noise table: index &0x7f → [128]. FLAG: ffedat has 144 dwords (576 B);
// sized from ASM index mask, extra 16 dwords retained as trailing init padding below.
static int32_t DATA_006805[144] = {};
// Cluster 7c — terrain heightmap 128×129 (bilinear corners at +0/+1/+128/+129).
// Task 4.182: the "max index + 129 fits in 16512" reasoning below was off by one. FUNC_000853
// computes i = ((uVar4 & 0xfe00) + (uVar1 >> 7)) >> 2, whose maximum is 16383, so the corner read
// i+129 reaches 16512 - exactly ONE byte past the 16512-byte table (UBSan: "index 16512 out of
// bounds for type 'unsigned char [16512]'"). In the ASM that byte belongs to whatever follows the
// table; the two-byte guard keeps the edge read in bounds and deterministic instead of UB.
static uint8_t DATA_006806[128 * 129 + 2] = {};
#define DATA_006807 DATA_006806[1]
#define DATA_006808 DATA_006806[128]
#define DATA_006809 DATA_006806[129]
// Batch3: independent
static uint32_t DATA_006810[8] = {};
// Batch3: independent
static uint32_t DATA_006811[8] = {};
static const uint8_t* DATA_006813 = nullptr;           // DATA_006812 record +4: x (blob image)
static const uint8_t* DATA_006814 = nullptr;           // DATA_006812 record +8: y (blob image)
static const uint8_t* DATA_006815 = nullptr;           // DATA_006812 record +12: string id (blob image)
// Cluster 7d — sector x/y offset tables: 12 × 4 (FUNC_000922(0xc) → idx 0..11; *4).
static uint8_t DATA_006816[12 * 4] = {};
#define DATA_006817 DATA_006816[2]
static uint8_t DATA_006818[12 * 4] = {};
#define DATA_006819 DATA_006818[2]
static uint8_t DATA_006820[140] = {};
// Height→color LUT: 64 dwords (ffedat 256 B). Index = FUNC_000853>>10.
static int32_t DATA_006821[64] = {};
// 35 × 20-byte records (rep movsd ×5 from eax*4 base after *5).
static uint8_t DATA_006822[35 * 20] = {};
static uint32_t DATA_006823 = 0;                       // SCALAR [HIGH]
static const uint8_t* DATA_006824 = nullptr;           // 20-byte block (blob image)
static uint32_t DATA_006825 = 7;                       // FUNC_001572 record {7,0,0} (ffedat 14619)
// ffedat: 006826/827/829 are 20-byte (5×dd) colour/arg blocks; 006828 is 4-byte ptr
static uint32_t DATA_006826[5] = {};
static uint32_t DATA_006827[5] = {};
static uint32_t DATA_006828 = 0;                       // SCALAR / pointer [HIGH]
static uint32_t DATA_006829[5] = {};
static uint32_t DATA_007097 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007098 = 0;                       // SCALAR [HIGH]
static const uint8_t* DATA_007099 = nullptr;           // ',' (blob image)
static const uint8_t* DATA_007100 = nullptr;           // ': ' (blob image)
static uint32_t DATA_007101 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007102 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007103 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007107 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007108 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007109 = 0;                       // SCALAR [HIGH]
// Cluster 3 — handcoded systems: 340 × 0x36 (ffe.asm add edx,0x36 until key==0;
// ffedat 7110..7116 = 18360). Field labels at +0/+4/+10/+12/+13/+44.
static uint8_t DATA_007110[340 * 0x36] = {};
#define DATA_007111 DATA_007110[4]
#define DATA_007112 DATA_007110[10]
#define DATA_007113 DATA_007110[12]
#define DATA_007114 DATA_007110[13]
#define DATA_007115 DATA_007110[44]
// Special-system id list (ffe.asm cmp eax,4; +4 walk) — not part of 0x36 stride table.
static int32_t DATA_007116[4] = {};
static uint32_t DATA_007117 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint32_t DATA_007129[256] = {};
// Batch3: 16×8 (and 0xf; *8)
static uint8_t DATA_007130[128] = {};
/* ASM: DATA_007131 is the +4 dword of each 8-byte pair (starport lat/long). */
inline uint8_t* DATA_007131_ptr() { return DATA_007130 + 4; }
#define DATA_007131 (DATA_007130[4])
// Batch3: independent
static uint32_t DATA_007132[48] = {};
static uint32_t DATA_007133 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007134 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007135 = 0;                       // SCALAR [HIGH]
// Batch3: independent FLAG: and 0x300>>8 → 4 elems; keep foot 16
extern uint32_t DATA_007136[4];   // shared (task 4.162): FUNC_000892 model table
// Batch3: independent FLAG: and 0x300>>8 → 4 elems; keep foot 16
extern uint32_t DATA_007137[4];   // shared (task 4.162): FUNC_000892 model table
// Batch3: independent FLAG: and 0x300>>8 → 4 elems; keep foot 16
extern uint32_t DATA_007138[4];   // shared (task 4.162): FUNC_000892 model table
static uint32_t DATA_007185 = 0;                       // SCALAR [HIGH]
// DATA_007187: the drive table, 0xf entries of 0x10 bytes (ffedat.asm:18123-18156):
// +0 range numerator, +4 efficiency, +8 mass, +0xc name string. FUNC_000903 indexes
// it by obj+0xd0; DATA_007188 is entry 0 +8 (FUNC_000911) and DATA_007189 entry 2 +8
// (FUNC_000902), so the three labels are one block.
using DriveTable = uint8_t[0xf0];
alignas(4) static uint8_t g_driveTable[0xf0] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4d, 0x9c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x94, 0x98, 0x00, 0x00,
    0x30, 0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x97, 0x98, 0x00, 0x00,
    0xc0, 0xd4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0xa1, 0x98, 0x00, 0x00,
    0xb0, 0x1e, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xa3, 0x98, 0x00, 0x00,
    0x00, 0x53, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0xa5, 0x98, 0x00, 0x00,
    0xb0, 0x71, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0xa8, 0x98, 0x00, 0x00,
    0xc0, 0x7a, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0xab, 0x98, 0x00, 0x00,
    0x30, 0x6e, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0xad, 0x98, 0x00, 0x00,
    0x30, 0x14, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x02, 0x00, 0x00, 0x0f, 0x9c, 0x00, 0x00,
    0x30, 0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xa4, 0x98, 0x00, 0x00,
    0xc0, 0xd4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0xa6, 0x98, 0x00, 0x00,
    0xb0, 0x1e, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xaa, 0x98, 0x00, 0x00,
    0x00, 0x1b, 0xb7, 0x00, 0xbb, 0xfb, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x10, 0x9c, 0x00, 0x00,
    0x30, 0x14, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x86, 0x00, 0x00,
};
static DriveTable& DATA_007187 = g_driveTable;
static uint32_t (&DATA_007188)[8] = *reinterpret_cast<uint32_t(*)[8]>(g_driveTable + 8);
static uint32_t& DATA_007189 = *reinterpret_cast<uint32_t*>(g_driveTable + 0x28);
static uint32_t DATA_007190 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007201 = 0x5a82;                  // game-over light vec X (ffedat 18370)
static uint32_t DATA_007202 = 0;                       // light vec Y
static uint32_t DATA_007203 = 0x5a82;                  // light vec Z
// R1227: ffedat DATA_007204..7206 = {0,0,5} (title/panel-close black record).
// Was left 0; FUNC_000281 then emitted (3,3,3) instead of ASM (3,3,0x17).
static uint32_t DATA_007204 = 0;
static uint32_t DATA_007205 = 0;
static uint32_t DATA_007206 = 5;
// Batch3: independent
static uint16_t DATA_007207[30] = {};
// Batch3: independent
static uint32_t DATA_007208[10] = {};
// Batch3: independent
static uint32_t DATA_007209[11] = {};
// Cargo index per destroyed-ship debris roll (ffedat DATA_007212, byte×40)
static uint8_t DATA_007212[40] = {};
// Batch3: independent
static uint32_t DATA_007220[10] = {};
// Batch3: missile launch mission-var ids (ffedat DATA_007221)
static uint32_t DATA_007221[10] = {};
/* Cabin mass→bitmap table (ffedat DATA_007222/7223): 5×{thresh, idx}, stride 8. */
static int32_t DATA_007222[10] = {
    0x20, 0, 0x4b, 1, 0x12c, 2, 0x3e8, 3, 0xffff, 4};
#define DATA_007223 (reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(DATA_007222) + 4))
static uint32_t DATA_007224 = 0xffffffffu;             // current cabin bitmap (ffedat dd -1)
// ffedat: DATA_007226 = "Mine\0"; DATA_007227 = "Missile\0" (copied into munition +0x124)
static char DATA_007226[5] = {'M', 'i', 'n', 'e', '\0'};
static char DATA_007227[8] = {'M', 'i', 's', 's', 'i', 'l', 'e', '\0'};
static char DATA_007228[] = "cabin%i.bmp";
static char DATA_007229[] = "rb";
// Title-scene light / shade / object seeds from ffedat.asm (JUMP_003840 path).
static int32_t  DATA_007231 = (int32_t)0xffffb619;     // light vec X
static int32_t  DATA_007232 = (int32_t)0xffffb619;     // light vec Y
static int32_t  DATA_007233 = (int32_t)0x000049e7;     // light vec Z
static uint8_t  DATA_007234[24] = {                    // shade table (0x18)
    7,7,7,7,7,7,6,6,6,5,5,5,4,4,4,3,3,3,2,2,2,1,1,1
};
static int32_t  DATA_007235[3] = {0, 0, 0x00014820};   // title object pos (Z)
// Diagonal 0x7f000000 ≈ unit basis in title matrix format (ffedat DATA_007236).
static int32_t  DATA_007236[9] = {
    (int32_t)0x7f000000, 0, 0,
    0, (int32_t)0x7f000000, 0,
    0, 0, (int32_t)0x7f000000
};
static uint32_t DATA_007237 = 0;                       // SCALAR [HIGH]
static char     DATA_007238[4] = {'0', '1', 0, 0};     // version digits "01"
static uint32_t DATA_007240 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint32_t DATA_007241[10] = {};
static const uint8_t* DATA_007242 = nullptr;           // 20-byte string (blob image)
static uint32_t DATA_007246 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007247 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007248 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007249 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007250 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007251 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007252 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint32_t DATA_007255 = 0;  // Batch3: kept scalar (foot=4, no multi-index)
// DATA_007258 / DATA_007259 (console hot-area lists) are defined in core.cpp.
// Batch3: independent
static uint32_t DATA_007260[15] = {};
static uint32_t DATA_007265 = 0;                       // SCALAR [HIGH]
// Task 4.273: values verified from the ASM binary's .data (ffeasm 0xc7b57: dword 0x00090707;
// 0xc7b59: byte 0x09) - the flight ambient color record that seeds DATA_008815-16-17 via
// FUNC_000998/001052 -> FUNC_001575 -> the DPal chain (root cause of the map palette offset,
// 4.272). The ASM stores these as overlapping byte fields (word 0x0707 at 7266, byte 0x09 at
// 7267); the port's separate uint32 scalars are read as (uint8)v, (uint8)(v>>8), (uint8)v.
static uint32_t DATA_007266 = 0x00090707u;             // SCALAR [HIGH] (ASM .data 0xc7b57)
static uint32_t DATA_007267 = 0x09u;                   // SCALAR [HIGH] (ASM .data 0xc7b59)
// Batch3: independent
static uint16_t DATA_007268[32] = {};
static uint32_t DATA_007269 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007270 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007271 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007272 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007273 = 0;                       // SCALAR [HIGH]
static const uint8_t* DATA_007274 = nullptr;           // 24-byte shade ramp (blob image)
static uint32_t DATA_007275 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007276 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007277 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007634 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007635 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007636 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007646 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007647 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007648 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint32_t DATA_007649[21] = {};
// ffedat.asm:23215 month lengths; DATA_007651 is February, patched per year.
static uint8_t DATA_007650[12] = {0x1f, 0x1c, 0x1f, 0x1e, 0x1f, 0x1e,
                                  0x1f, 0x1f, 0x1e, 0x1f, 0x1e, 0x1f};
static uint8_t& DATA_007651 = DATA_007650[1];
// Batch3: independent
static uint32_t DATA_007652[55] = {};
static uint32_t DATA_007655 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007656 = 0;                       // SCALAR [HIGH]
static const char DATA_007670[] = "%02i:%02i:%02i";   // ffedat.asm:23311
// Batch3: independent
static uint32_t DATA_007687[256] = {};
static uint32_t DATA_007688 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007689 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007691 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007693 = 0x80;                    // SCALAR [HIGH]  R1174: released, not "pressed"
static uint32_t DATA_007695 = 0x80;                    // SCALAR [HIGH]  R1174: released, not "pressed"
static uint32_t DATA_007696 = 0x80;                    // SCALAR [HIGH]  R1174: released, not "pressed"
static uint32_t DATA_007705 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007717 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007718 = 0;                       // SCALAR [HIGH]
// The ASM label's image address (`add eax,DATA_007750`, ffe.asm:94342-94345) is the
// glyph-row base, stride 10. The port must hold that address as a value: until 4.119
// FUNC_001598_DrawChar took the address of this static and read rows from .bss.
static const uint8_t* DATA_007750 = nullptr;
// The ASM label's image address (`byte [eax*2+DATA_007751]`): blob + OFFSET_DATA_007751.
static const uint8_t* DATA_007751 = nullptr;
// Batch3: independent
static uint32_t DATA_007759[36] = {};
static uint32_t DATA_007768 = 0;                       // SCALAR [HIGH]
extern uint8_t* DATA_007770;                           // dPalMap RGB444→(VGA+1)
// ASM: dword pointer to 320x200 back buffer (`mov ecx,[DATA_007773]`).
static uint8_t* DATA_007773 = nullptr;
// Batch3: independent
static uint32_t DATA_007784 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007785 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007786 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007790 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007791 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007792 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007800 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007801 = 0;                       // SCALAR [HIGH]
// Batch3: independent
// Contiguous ASM material table (DATA_007811..007814). Labeled size for
// 007811 is 8; the table continues through 007814 (148 × 0x1E = 4440 B).
constexpr uint32_t kMaterialTableBytes = 4440;
extern uint8_t DATA_007811[kMaterialTableBytes];
extern uint8_t* DATA_007812; // DATA_007811 + 8 (shade0.R base)
extern uint8_t* DATA_007813; // DATA_007811 + 9 (shade0.G base)
extern uint8_t* DATA_007814; // DATA_007811 + 10 (shade0.B base)
void InitMaterialTableFromBlob();
static uint32_t DATA_007818[16] = {};
// Batch3: independent
static uint32_t DATA_007820[14] = {};
// Batch3: independent
static uint32_t DATA_007821[14] = {};
// Batch3: independent
static uint32_t DATA_007822[14] = {};
static uint32_t DATA_007823 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint8_t DATA_007825[33] = {};
static uint32_t DATA_007826 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007827 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint32_t DATA_007828[14] = {};
static uint32_t DATA_007829 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007833 = 0;                       // SCALAR [HIGH]
// Shared across TUs (planet LOD). Must not be per-TU static — leaf dumps / 830
// / 829 / 876 all read the same BSS word as ASM.
extern uint32_t DATA_007837;                           // LOD bias
extern uint32_t DATA_007838[16];                       // clip flags (64)
static uint32_t DATA_007839[32]{};                     // LOD radius scale (128)
static uint32_t DATA_007840[32]{};                     // LOD midpoint scale (128)
// Batch3 FLAG: offset holder (planet_icosa); not embedded table
static uint32_t DATA_007841 = 0;
// Batch3 FLAG: offset holder (planet_icosa); not embedded table
static uint32_t DATA_007842 = 0;
// Batch3 FLAG: offset holder (planet_icosa); not embedded table
static uint32_t DATA_007843 = 0;
// Dither mix tables (task 4.177). ffedat declares DATA_007846 as 24 dwords and
// DATA_007847 as 144, adjacent in .data. FUNC_001841 fills DATA_007846 as four rows of
// eight dwords -- stride 8, seven writes per row, so indices 0..6, 8..14, 16..22, 24..30 --
// and the readers address it the same way (DATA_007846[row*8], row reaching 3). The table's
// fourth row therefore *is* DATA_007847[0..7], including the eighth dword the writer never
// touches. Keeping both in one object makes that alias explicit and layout-independent;
// before this the fourth row landed on whichever global happened to follow (ASan caught it
// writing 0 bytes past DATA_007846, i.e. into DATA_007847[0]).
struct DitherMixStorage { uint32_t t846[24]; uint32_t t847[144]; };
static DitherMixStorage g_ditherMix{};
static uint32_t (&DATA_007846)[32] = *reinterpret_cast<uint32_t(*)[32]>(g_ditherMix.t846);
static uint32_t (&DATA_007847)[144] = g_ditherMix.t847;
static uint32_t DATA_007848[22]{};                     // dither mix out (88)
static uint32_t DATA_007849[147]{};                    // dither mix out (588)
static uint32_t DATA_007850[88]{};                     // mix weights (352)
static uint32_t DATA_007851[588]{};                    // mix weights (2352)
static uint32_t DATA_007852[32]{};                     // dither mix out (128)
static uint32_t DATA_007853[128]{};                    // mix weights (512)
static int16_t DATA_007854[16]{};                      // midpoint screen nudge
static int16_t DATA_007855[16]{};                      // midpoint screen nudge Y
static int16_t DATA_007856[16]{};                      // midpoint screen nudge (850)
static int16_t DATA_007857[16]{};                      // midpoint screen nudge Y (850)
static int16_t DATA_007858[16]{};                      // midpoint screen nudge (851)
static int16_t DATA_007859[16]{};                      // midpoint screen nudge Y (851)
static int16_t DATA_007860[16]{};                      // midpoint screen nudge (852)
// Batch3: independent
static uint8_t DATA_007861[30] = {};
static int16_t DATA_007862[16]{};                      // midpoint screen nudge (853)
static int16_t DATA_007863[16]{};                      // midpoint screen nudge Y (853)
static int16_t DATA_007864[16]{};                      // midpoint screen nudge (854)
static int16_t DATA_007865[16]{};                      // midpoint screen nudge Y (854)
static int16_t DATA_007866[16]{};                      // midpoint screen nudge (855)
static int16_t DATA_007867[16]{};                      // midpoint screen nudge Y (855)
static int16_t DATA_007868[16]{};                      // midpoint screen nudge (856)
// Batch3: independent
static uint8_t DATA_007869[30] = {};
static int16_t DATA_007870[16]{};                      // midpoint screen nudge (857)
// Batch3: independent
static uint8_t DATA_007871[30] = {};
static int16_t DATA_007872[16]{};                      // midpoint screen nudge (858)
static uint32_t DATA_007873 = 0;                       // SCALAR [HIGH]
static int16_t DATA_007874[16]{};                      // midpoint screen nudge (859)
static uint32_t DATA_007875 = 0;                       // SCALAR [HIGH]
static int16_t DATA_007876[16]{};                      // midpoint screen nudge (860)
static uint32_t DATA_007877 = 0;                       // SCALAR [HIGH]
static int16_t DATA_007878[16]{};                      // midpoint screen nudge (861)
static int16_t DATA_007879[16]{};                      // midpoint screen nudge Y (861)
static int16_t DATA_007880[16]{};                      // midpoint screen nudge (862)
static int16_t DATA_007881[16]{};                      // midpoint screen nudge Y (862)
static int16_t DATA_007882[16]{};                      // midpoint screen nudge (863)
static int16_t DATA_007883[16]{};                      // midpoint screen nudge Y (863)
static int16_t DATA_007884[16]{};                      // midpoint screen nudge (864)
static uint32_t DATA_007885 = 0;                       // SCALAR [HIGH]
static int16_t DATA_007886[16]{};                      // midpoint screen nudge (865)
static uint32_t DATA_007887 = 0;                       // SCALAR [HIGH]
// Rocky midpoint screen nudge (FUNC_001866/867) — ±1 tables, same layout as
// DATA_007854/855 (X base + Y base overlapping by 2 bytes in the blob).
static int16_t DATA_007888[16]{};
static int16_t DATA_007889[16]{};
static int16_t DATA_007890[16]{};
static int16_t DATA_007891[16]{};
static uint32_t DATA_007892 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007893 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007894 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007895 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_007975 = 0;                       // SCALAR [HIGH]
static const uint8_t* DATA_007976 = nullptr;           // 128 x int16 table (blob image)
static uint32_t DATA_007977 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008240 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint8_t DATA_008428[263] = {};
// Batch3: independent (ffedat foot 32) resized foot 4→32
/* The scripts' global variables (FUNC_000034 / FUNC_000035, FUNC_000031 op 4):
 * indices run past DATA_008548's 8 dwords into DATA_008549 (63 dwords). */
static uint32_t s_scriptVars[8 + 63] = {};
using ScriptVarsLo = uint32_t[8];
using ScriptVarsHi = uint32_t[63];
static ScriptVarsLo& DATA_008548 = *reinterpret_cast<ScriptVarsLo*>(&s_scriptVars[0]);
static ScriptVarsHi& DATA_008549 = *reinterpret_cast<ScriptVarsHi*>(&s_scriptVars[8]);
static uint32_t DATA_008550 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008551 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008552 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008553 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008554 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008555 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008556 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008557 = 0;                       // SCALAR [HIGH]
static intptr_t DATA_008558 = 0;  // script text cursor (FUNC_000064 window start; ops 0x23/0x24/0x2d)
static intptr_t DATA_008559 = 0;  // FUNC_000064 window end
static uint32_t DATA_008560 = 0;                       // SCALAR [HIGH]
// Batch3: independent
// Batch3: independent
// Batch3: independent
// Batch3: file-slot table 150×0x24 (cmp esi,0x96; add ebx,0x24). Fields +0/+24/+32.
static uint8_t DATA_008561[150 * 0x24] = {};
#define DATA_008562 DATA_008561[0x18]
#define DATA_008563 DATA_008561[0x20]
static uintptr_t DATA_008564 = 0;                     // Cmd-handler BST root (was uint32_t; 64-bit ptr)
// ffedat resb 24: one "done" bit per DATA_002280 journal entry (0xaa bits, 0x16 bytes used).
static uint8_t DATA_008565[24] = {};
static uint32_t DATA_008566 = 0;                       // SCALAR [HIGH]
// Cluster C — SaveMissionData tables (ffedat: 20 + 8×4 + 228 = 280 B = 5×0x38).
// Batch4 RC1: 008570–008578 are integer *base addresses* into the blob (ASM dword
// fields / lea bases), NOT byte lvalues. Record stride = 0x38 bytes (= 0xe int32s).
static int32_t DATA_008568[100] = {};
/* The list heads here and the +0x3c/+0x40 node links (DATA_008579) are 32-bit
 * pointers, and scripts hand the expander 32-bit pointers to these rows. */
using MissionListTable = uint8_t[5 * 0x38];
static MissionListTable& DATA_008569 = Below4G<MissionListTable, 8569>();
#define DATA_008570 (reinterpret_cast<intptr_t>(DATA_008569 + 0x14))
#define DATA_008571 (reinterpret_cast<intptr_t>(DATA_008569 + 0x18))
#define DATA_008572 (reinterpret_cast<intptr_t>(DATA_008569 + 0x1c))
#define DATA_008573 (reinterpret_cast<intptr_t>(DATA_008569 + 0x20))
#define DATA_008574 (reinterpret_cast<intptr_t>(DATA_008569 + 0x24))
#define DATA_008575 (reinterpret_cast<intptr_t>(DATA_008569 + 0x28))
#define DATA_008576 (reinterpret_cast<intptr_t>(DATA_008569 + 0x2c))
#define DATA_008577 (reinterpret_cast<intptr_t>(DATA_008569 + 0x30))
#define DATA_008578 (reinterpret_cast<intptr_t>(DATA_008569 + 0x34))
static_assert(sizeof(DATA_008569) == 280, "Batch4 RC1 DATA_008569 size");
static_assert(0x14 == 20 && 0x1c == 28 && 0x30 == 48 && 0x34 == 52, "Batch4 RC1 field offsets");
/* Mission list slots: 700 × 0x44 (ffedat DATA_008579 resb 47600). Free = dword0 == -1. */
using MissionSlotTable = uint8_t[700 * 0x44];
static MissionSlotTable& DATA_008579 = Below4G<MissionSlotTable, 8579>();
static_assert(sizeof(DATA_008579) == 47600, "DATA_008579 mission slot table");
static uint32_t DATA_008582 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008583 = 0;                       // AVI/extravideo state
static uint32_t DATA_008584 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008585 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008586 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008587 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008588 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008589 = 0;                       // AVI/extravideo state
static uint32_t DATA_008590 = 0;                       // AVI/extravideo state
static uint32_t DATA_008591 = 0;                       // AVI/extravideo state
static uint32_t DATA_008592 = 0;                       // AVI/extravideo state
static uint32_t DATA_008593 = 0;                       // AVI/extravideo state
static uint32_t DATA_008594 = 0;                       // AVI/extravideo state
// ffedat resb 56 / 48 / 44: 'avih' (FUNC_000113), 'strh' (FUNC_000114) and
// 'strf' BITMAPINFOHEADER (FUNC_000115) chunk images.
static uint32_t DATA_008595[0x38 / 4] = {};
static uint32_t DATA_008596[0x30 / 4] = {};
static uint32_t DATA_008597[0x2c / 4] = {};
static uint32_t DATA_008598 = 0;                       // SCALAR [HIGH]
// ffedat resb 1024 each: FUNC_000112's 256-entry tables (written, never read).
static int32_t DATA_008599[256] = {};
static int32_t DATA_008600[256] = {};
static int32_t DATA_008601[256] = {};
static int32_t DATA_008602[256] = {};
// ffedat: DATA_008603 is a char* (dest ptr into frame buffer); 008604 is 256-byte remap LUT
static uintptr_t DATA_008603 = 0;                      // frame-buffer dest (ffe.asm:13867)
static uint8_t DATA_008604[256] = {};                  // remap table (ffedat resb 256)
static uint8_t DATA_008605[14400] = {};                // frame decode scratch (ffedat resb 14400)
static void* DATA_008606 = nullptr;                    // POINTER [MEDIUM]
static uint32_t DATA_008607 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008608 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008611 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008612 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008613 = 0;                       // SCALAR [HIGH]
// ffedat DATA_008614 resb 8192: the read buffer itself (push dword DATA_008614 ->
// fread, ffe.asm:14243/14254); DATA_008615 the cursor into it (14262).
static uint8_t DATA_008614[8192] = {};
static uintptr_t DATA_008615 = 0;
static uint32_t DATA_008616 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008619 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008620 = 0;                       // module index being dispatched (FUNC_000152)
static uint32_t DATA_008621 = 0;                       // SCALAR [HIGH]
// Module registry (ffedat.asm resb chain DATA_008622..DATA_008652, 0x200 bytes
// memset by FUNC_000143): 21 x 8-byte entries, per entry +4 module code,
// +5 flags (bit 0 action 10, bit 1 action 2, bit 2 action 11), +6 action 3,
// +7 action 4. Every symbol is a byte inside DATA_008622; the offsets are the
// resb chain's, not the names' (DATA_008623 = +5). Signed where the ASM
// branches signed on the byte (jl / jnl / jg).
extern uint8_t  DATA_008622[0x200];
extern uint8_t& DATA_008623;   // +0x05  e0 +5
extern uint8_t& DATA_008624;   // +0x06  e0 +6
extern uint8_t& DATA_008625;   // +0x07  e0 +7
extern uint8_t& DATA_008626;   // +0x10  e2
extern int8_t&  DATA_008627;   // +0x16  e2 +6
extern uint8_t& DATA_008628;   // +0x17  e2 +7
extern uint8_t& DATA_008629;   // +0x18  e3
extern int8_t&  DATA_008630;   // +0x1e  e3 +6 (0xff ship module active, 0x7f inactive)
extern uint8_t& DATA_008631;   // +0x1f  e3 +7
extern uint8_t& DATA_008632;   // +0x20  e4
extern uint8_t& DATA_008633;   // +0x2c  e5 +4
extern uint8_t& DATA_008634;   // +0x2e  e5 +6
extern uint8_t& DATA_008635;   // +0x2f  e5 +7
extern uint8_t& DATA_008636;   // +0x3d  e7 +5
extern uint8_t& DATA_008637;   // +0x50  e10
extern uint8_t& DATA_008638;   // +0x5d  e11 +5
extern uint8_t& DATA_008639;   // +0x60  e12
extern uint8_t& DATA_008640;   // +0x66  e12 +6 (comms module)
extern uint8_t& DATA_008641;   // +0x67  e12 +7
extern uint8_t& DATA_008642;   // +0x6e  e13 +6
extern uint8_t& DATA_008643;   // +0x70  e14
extern uint8_t& DATA_008644;   // +0x75  e14 +5
extern uint8_t& DATA_008645;   // +0x76  e14 +6
extern uint8_t& DATA_008646;   // +0x7d  e15 +5
extern int8_t&  DATA_008647;   // +0x7e  e15 +6
extern uint8_t& DATA_008648;   // +0x84  e16 +4
extern uint8_t& DATA_008649;   // +0x8d  e17 +5
extern uint8_t& DATA_008650;   // +0x98  e19
extern uint8_t& DATA_008651;   // +0x9e  e19 +6
extern uint8_t& DATA_008652;   // +0xa0  e20
// DATA_008653..8655 are one 3-byte FUNC_001572 record (ffedat 27818-27823).
static uint8_t DATA_008653_rec[3] = {};
static uint8_t& DATA_008653 = DATA_008653_rec[0];
static uint8_t& DATA_008654 = DATA_008653_rec[1];
static uint8_t& DATA_008655 = DATA_008653_rec[2];
// DATA_008656 / DATA_008657: the system name (FUNC_000832 → FUNC_000830) and
// its description; FUNC_000855 copies 0x14 bytes from DATA_008656: one block.
using SystemName8656 = char[0x11];
using SystemName8657 = char[0x40];
alignas(4) static char g_systemNames[0x11 + 0x40] = {};
static SystemName8656& DATA_008656 = *reinterpret_cast<SystemName8656*>(g_systemNames);
static SystemName8657& DATA_008657 = *reinterpret_cast<SystemName8657*>(g_systemNames + 0x11);
static uint32_t DATA_008658 = 0;                       // Error code (commander file)
static uint32_t DATA_008659 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008660 = 0;                       // SCALAR [HIGH]
// ffedat: Last file FileInfo — resb 20 at 661 + adjacent; ASM rep movsd ecx=6 → 24 bytes
static uint8_t DATA_008661[24] = {};
static uint32_t DATA_008662 = 0;                       // SCALAR [HIGH]
static uint8_t DATA_008663[0x40] = {};                 // ffedat resb 64: FUNC_000170/171 copy it whole
// Batch3: independent (ffedat foot 1536) resized foot 4→1536
static uint8_t DATA_008665[1536] = {};
static uint32_t DATA_008669 = 0;                       // Scramble key
static uint32_t DATA_008670 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008671 = 0;                       // ffedat resb 4 (FUNC_000212/213 clear it)
// DATA_008672 (8) / 008673 (12) / 008674 (44): FUNC_000259 copies DATA_005095's
// eleven dwords from DATA_008672 straight through 008673 into 008674: one block.
alignas(4) static uint8_t g_block8672[8 + 12 + 44] = {};
static uint32_t& DATA_008672 = *reinterpret_cast<uint32_t*>(g_block8672);
static uint32_t& DATA_008673 = *reinterpret_cast<uint32_t*>(g_block8672 + 8);
static uint32_t& DATA_008674 = *reinterpret_cast<uint32_t*>(g_block8672 + 0x14);
static uint32_t DATA_008675 = 0;                       // Display string ID
static uint32_t DATA_008676 = 0;                       // Display parameter
static uint32_t DATA_008677 = 0;                       // SCALAR [HIGH]
// Cluster E — chat log ring: 10 × 0x40e (ffedat BSS sum; ASM [reg*2+base] stride 0x40e).
// Field bases relative to slot 0: 678=+0, 679=+4, 680=+8, 681=+9, 682=+0x40a.
static uint8_t DATA_008678[10 * 0x40e] = {};
#define DATA_008679 DATA_008678[4]
#define DATA_008680 DATA_008678[8]
#define DATA_008681 DATA_008678[9]
#define DATA_008682 DATA_008678[0x40a]
// ffedat DATA_008683: resb 76 — scanner stalk color table (19 dwords)
static int32_t DATA_008683[19] = {};
static uint32_t DATA_008684 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008685 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008686 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008687 = 0;                       // SCALAR [HIGH]
// ffedat DATA_008688..DATA_008691 (resb 4/4/4/36): FUNC_000347 copies 12 dwords
// over all four (ffe.asm:27211-27213); FUNC_000327 reads 8689..8691 back.
using Bss8688 = uint8_t[48];
static Bss8688& g_bss8688 = Below4G<Bss8688, 8688>();
static uint32_t& DATA_008689 = *reinterpret_cast<uint32_t*>(g_bss8688 + 4);
static uint32_t& DATA_008690 = *reinterpret_cast<uint32_t*>(g_bss8688 + 8);
static uint32_t& DATA_008691 = *reinterpret_cast<uint32_t*>(g_bss8688 + 12);
static uint32_t DATA_008692 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008693 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008694 = 0;                       // SCALAR [HIGH]
// Batch3: independent (ffedat foot 372) resized foot 4→372
static uint8_t DATA_008695[372] = {};
static uint32_t DATA_008696 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008697 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008698 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008699 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008701 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008702 = 0;                       // SCALAR [HIGH]
// Batch3: independent
// Task 4.182: ffedat lays 008703..008707 out as five CONSECUTIVE dwords, and the oracle fixtures
// watch EIGHT bytes at DATA_008703 (i.e. 008703+008704), so one object with the labels as macros -
// the shape this header already uses in 28 other places. Before this the 8-byte watch read past a
// 4-byte scalar (ASan: global-buffer-overflow, 0 bytes after DATA_008703).
static uint32_t DATA_008703_VEC[5] = {};
#define DATA_008703 DATA_008703_VEC[0]
#define DATA_008704 DATA_008703_VEC[1]
#define DATA_008705 DATA_008703_VEC[2]
#define DATA_008706 DATA_008703_VEC[3]
#define DATA_008707 DATA_008703_VEC[4]
static uint32_t DATA_008711 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008712 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008713 = 0;                       // SCALAR [HIGH]
static void* DATA_008714 = nullptr;                    // POINTER [HIGH]
// ffedat Vec64 "laser hit position": six contiguous dwords (Int64Mul64 / Vec64Add / rep movsd).
static uint32_t DATA_008715_VEC[6] = {};
#define DATA_008715 DATA_008715_VEC[0]
#define DATA_008716 DATA_008715_VEC[1]
#define DATA_008717 DATA_008715_VEC[2]
#define DATA_008718 DATA_008715_VEC[3]
#define DATA_008719 DATA_008715_VEC[4]
#define DATA_008720 DATA_008715_VEC[5]
// Task 4.182: ffedat lays 008721..008732 out as TWELVE consecutive dwords holding three
// OVERLAPPING views - a 3-vector at 008721 (FUNC_001670_VecMatTMul reads it), the Vec64 at
// 008724..008729 (Int64Mul64 / Vec64Add read it whole), and the 3-vector at 008730..008732 that
// FUNC_000818 writes through FUNC_000817 - whose second argument is the 008744 run below. One
// object with the labels as macros so the views alias as they do in the ASM, instead of each write
// running off a 4-byte scalar (ASan: WRITE of size 4, 0 bytes after DATA_008730).
static uint32_t DATA_008721_VEC[12] = {};
#define DATA_008721 DATA_008721_VEC[0]
#define DATA_008722 DATA_008721_VEC[1]
#define DATA_008723 DATA_008721_VEC[2]
#define DATA_008724 DATA_008721_VEC[3]
#define DATA_008725 DATA_008721_VEC[4]
#define DATA_008726 DATA_008721_VEC[5]
#define DATA_008727 DATA_008721_VEC[6]
#define DATA_008728 DATA_008721_VEC[7]
#define DATA_008729 DATA_008721_VEC[8]
#define DATA_008730 DATA_008721_VEC[9]
#define DATA_008731 DATA_008721_VEC[10]
#define DATA_008732 DATA_008721_VEC[11]
static uint32_t DATA_008733 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008734 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint32_t DATA_008735 = 0;  // Batch3: kept scalar (foot=4, no multi-index)
static uint32_t DATA_008736 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008737 = 0;                       // SCALAR [HIGH]
// ffedat Vec64 (six contiguous dwords; Vec64Add and rep movsd read it whole).
static uint32_t DATA_008738_VEC[6] = {};
#define DATA_008738 DATA_008738_VEC[0]
#define DATA_008739 DATA_008738_VEC[1]
#define DATA_008740 DATA_008738_VEC[2]
#define DATA_008741 DATA_008738_VEC[3]
#define DATA_008742 DATA_008738_VEC[4]
#define DATA_008743 DATA_008738_VEC[5]
// Batch3: independent
// Task 4.182: ffedat lays 008744..008752 out as nine CONSECUTIVE dwords holding TWO OVERLAPPING
// Vec64 views - 008744..008749 and 008747..008752, which share 008747..008749. Both are read: the
// "for (iVar3 = 6; ...) *puVar6 = *puVar5" copies from &DATA_008744, and Vec64Truncate / rep movsd
// read &DATA_008747 whole. They must therefore alias ONE object; as separate storage (three scalars
// plus DATA_008747_VEC[]) a write through one view was invisible to the other - a fidelity bug, not
// just the 24-byte read past a 4-byte scalar that ASan reported (0 bytes after DATA_008744).
static uint32_t DATA_008744_VEC[9] = {};
#define DATA_008744 DATA_008744_VEC[0]
#define DATA_008745 DATA_008744_VEC[1]
#define DATA_008746 DATA_008744_VEC[2]
#define DATA_008747 DATA_008744_VEC[3]
#define DATA_008748 DATA_008744_VEC[4]
#define DATA_008749 DATA_008744_VEC[5]
#define DATA_008750 DATA_008744_VEC[6]
#define DATA_008751 DATA_008744_VEC[7]
#define DATA_008752 DATA_008744_VEC[8]
// ffe.asm:28018 — 753..755 are three contiguous dwords and FUNC_000814 passes
// &DATA_008753 to FUNC_001669_VecMatMul as a 3-vector (it reads [0..2]) and to
// FUNC_001670_VecMatTMul as its destination. Three separate scalars made that
// cross into whatever the linker placed next (ASan global-buffer-overflow at
// core.cpp:46063 on player-fire). Keep the ASM's contiguity like 744.
static uint32_t DATA_008753_VEC[3] = {};
#define DATA_008753 DATA_008753_VEC[0]
#define DATA_008754 DATA_008753_VEC[1]
#define DATA_008755 DATA_008753_VEC[2]
static uint32_t DATA_008756[6] = {};                   // ffedat resb 24: a Vec64
static uint32_t DATA_008757 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008758 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008759 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008760 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008761 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008762 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008763 = 0;                       // SCALAR [HIGH]
static uint32_t* DATA_008764 = nullptr;                // POINTER [HIGH]
static uint32_t DATA_008765 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008766 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008767 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008768 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008769 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008770 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008771 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_008772[3] = {};                   // ffedat 8772..8774: the hit normal
static uint32_t DATA_008775 = 0;                       // SCALAR [HIGH]
// Batch3: independent (ffedat foot 138) resized foot 4→138
// DATA_008776 / DATA_008777: FUNC_000829 writes 49 dwords (0xc4 bytes) from
// DATA_008776, through the end of DATA_008777: one block.
// ffedat.asm:28064-28091: DATA_008776 (138) through DATA_008789 (9002) are one
// 0x243c-byte span. FUNC_000878 lays 0x44-byte SysObj records from DATA_008778
// (ffe.asm:58456), and FUNC_000843 / FUNC_000851 read record i-1's +0xa word
// as `word [i*0x44 + DATA_008777]` (ffe.asm:58640, 59040).
using Table8776 = uint16_t[69];
using Table8777 = uint8_t[0x3a];
alignas(4) static uint8_t g_block8776[0x243c] = {};
static uint8_t (&g_table8776)[0x8a + 0x3a] = *reinterpret_cast<uint8_t (*)[0x8a + 0x3a]>(g_block8776);
static Table8776& DATA_008776 = *reinterpret_cast<Table8776*>(g_block8776);
static Table8777& DATA_008777 = *reinterpret_cast<Table8777*>(g_block8776 + 0x8a);
// Batch3: independent
// Batch3: independent
// Batch3: independent
// Batch3: independent
// Batch3: independent
// Batch3: independent
// Batch3: independent
// Batch3: independent
// Batch3: independent
// Batch3: independent
// Batch3: sys record fields 778..787 (78 B to 788). C++ uses *0x44 — FLAG vs 0x4e footprint unit.
// The first record's fields; later records follow at +0x44 through DATA_008789.
using Table8778 = uint8_t[0x4e];
static Table8778& DATA_008778 = *reinterpret_cast<Table8778*>(g_block8776 + 0xc4);
#define DATA_008779 DATA_008778[4]
#define DATA_008780 DATA_008778[8]
#define DATA_008781 DATA_008778[0x0a]
#define DATA_008782 DATA_008778[0x0e]
#define DATA_008783 DATA_008778[0x14]
#define DATA_008784 DATA_008778[0x16]
#define DATA_008785 DATA_008778[0x20]
#define DATA_008786 DATA_008778[0x22]
#define DATA_008787 DATA_008778[0x24]
// ffedat: string field (40 B) within sys-record; bulk continues at 008789
using Table8788 = uint8_t[40];
using Table8789 = uint8_t[9002];
static Table8788& DATA_008788 = *reinterpret_cast<Table8788*>(g_block8776 + 0xea);
static Table8789& DATA_008789 = *reinterpret_cast<Table8789*>(g_block8776 + 0x112);
// Batch3: independent (ffedat foot 32) resized foot 4→32
static uint32_t DATA_008790[8] = {};
static uint32_t DATA_008791 = 0;                       // SCALAR [HIGH]
// Batch3: independent (ffedat foot 38) resized foot 4→38
static uint16_t DATA_008792[19] = {};
static uint32_t DATA_008793 = 0;                       // SCALAR [HIGH]
extern uint8_t DATA_008794[0x44]; // shared (task 4.162): FUNC_000892 template               // FUNC_000892 template (leading 0x44 of a larger ASM region)
extern uint8_t DATA_008795[0x44]; // shared (task 4.162): FUNC_000892 template               // FUNC_000892 template (leading 0x44 of a larger ASM region)
static uint32_t DATA_008796 = 0;                       // SCALAR [HIGH]
extern uint8_t DATA_008797[0x44]; // shared (task 4.162): FUNC_000892 template               // FUNC_000892 template (leading 0x44 of a larger ASM region)
static uint8_t DATA_008800[68] = {};                   // resb 68 (ffedat 28112)
static uint8_t& DATA_008811 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xe);  // SCALAR [HIGH]
extern uint32_t& DATA_008812;                          // gfx detail → LOD bias base
extern uint32_t& DATA_008813;                          // paired bias when 812 bit0 set
// R1116: the AB_LOD_BIAS harness override (defaults 1/1, the live state). All
// init-time writers of 8812/8813 must re-apply through AbApplyLodBias() instead
// of a literal, otherwise they clobber the -1/-2 (highest-detail) state the A/B
// needs to reproduce. The env is read on every call, so a harness may set it at
// the top of main.
uint32_t AbLodBiasA();
uint32_t AbLodBiasB();
void AbApplyLodBias();
// ASM layout (ffedat): obj+0x1c = dword, obj+0x20 = u8, +0x21 = u8, +0x22 = u16.
// FUNC_001575 reads the 3-byte record {815, 816, 817} contiguously.
extern uint32_t& DATA_008814;                          // title obj +0x1c (dword)
extern uint8_t&  DATA_008815;                          // title obj +0x20 (record byte 0)
extern uint8_t&  DATA_008816;                          // title obj +0x21 (record byte 1)
extern uint16_t& DATA_008817;                          // title obj +0x22 (record byte 2)
static int32_t& DATA_008821 = *reinterpret_cast<int32_t*>(g_bss8804 + 0x2c);  // title light copy X
static int32_t& DATA_008822 = *reinterpret_cast<int32_t*>(g_bss8804 + 0x30);  // title light copy Y
static int32_t& DATA_008823 = *reinterpret_cast<int32_t*>(g_bss8804 + 0x34);  // title light copy Z
static uint32_t& DATA_008824 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x38);  // SCALAR [HIGH]
static uint32_t& DATA_008825 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x3c);  // SCALAR [HIGH]
static uint32_t& DATA_008826 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x40);  // SCALAR [HIGH]
static uint32_t& DATA_008827 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x44);  // SCALAR [HIGH]
static uint32_t& DATA_008828 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x48);  // SCALAR [HIGH]
static uint32_t& DATA_008829 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x4c);  // SCALAR [HIGH]
static uint32_t& DATA_008830 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x50);  // Object processing counter
using Span008831 = uint8_t[24];
static Span008831& DATA_008831 = *reinterpret_cast<Span008831*>(g_bss8804 + 0x54);  // shade table working copy (0x18)
static uint16_t& DATA_008832 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x6c);  // SCALAR [HIGH]
static uint16_t& DATA_008833 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x6e);  // SCALAR [HIGH]
static uint32_t& DATA_008834 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x70);  // SCALAR [HIGH]
static uint8_t& DATA_008836 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x75);  // SCALAR [HIGH]
static u32_unaligned& DATA_008837 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x7a);  // SCALAR [HIGH]
static uint8_t& DATA_008838 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x7e);  // SCALAR [HIGH]
static uint32_t& DATA_008846 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0xa4);  // SCALAR [HIGH]
static uint8_t& DATA_008847 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xa8);  // SCALAR [HIGH]
static uint8_t& DATA_008848 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xa9);  // SCALAR [HIGH]
static uint8_t& DATA_008849 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xaa);  // SCALAR [HIGH]
// ffedat resb 1 / resb 2: FUNC_000878 copies the SysGenParams star count
// (+0x08) into both. 8850 has no ASM reader; 8851 counts the remaining stars
// for FUNC_000897 (the reached port keeps it as Gen::d8851, sysgen_proc.cpp).
static uint8_t& DATA_008850 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xab);
static uint8_t& DATA_008851 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xac);
static u32_unaligned& DATA_008852 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0xae);  // SCALAR [HIGH]
static u32_unaligned& DATA_008853 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0xb2);  // SCALAR [HIGH]
static uint8_t& DATA_008855 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xba);  // SCALAR [HIGH]
static u32_unaligned& DATA_008856 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0xbb);  // SCALAR [HIGH]
static uint8_t& DATA_008858 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xc4);  // SCALAR [HIGH]
static uint8_t& DATA_008859 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xc5);  // SCALAR [HIGH]
static int16_t& DATA_008863 = *reinterpret_cast<int16_t*>(g_bss8804 + 0xd0);  // SCALAR [HIGH]
static uint32_t& DATA_008865 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0xd4);  // SCALAR [HIGH]
static uint32_t& DATA_008868 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0xe0);  // SCALAR [HIGH]
static uint32_t& DATA_008869 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0xe4);  // SCALAR [HIGH]
static uint8_t& DATA_008873 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xeb);  // SCALAR [HIGH]
static uint8_t& DATA_008875 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xed);  // SCALAR [HIGH]
static uint8_t& DATA_008876 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xee);  // SCALAR [HIGH]
static uint32_t& DATA_008878 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0xf0);  // SCALAR [HIGH]
static uint32_t& DATA_008879 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0xf4);  // SCALAR [HIGH]
static u32_unaligned& DATA_008881 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0xfa);  // SCALAR [HIGH]
static u32_unaligned& DATA_008882 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x102);  // SCALAR [HIGH]
static uint16_t& DATA_008883 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x104);  // SCALAR [HIGH]
static u32_unaligned& DATA_008886 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x10e);  // SCALAR [HIGH]
static uint32_t& DATA_008892 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x124);  // SCALAR [HIGH]
static uint32_t& DATA_008893 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x128);  // SCALAR [HIGH]
static uint32_t& DATA_008894 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x12c);  // SCALAR [HIGH]
// Batch3: word stock/stats table. ASM [idx*2]. C++ zeros 0x26 words.
// FLAG: ASM/C++ 76 B > ffedat foot 8; expanded to 0x26 words.
using Span008896 = uint16_t[0x26];
static Span008896& DATA_008896 = *reinterpret_cast<Span008896*>(g_bss8804 + 0x134);
// ffedat.asm: DATA_008897-008903 are labels inside the cargo rows (+8/+18/+22/+24/+38/+40/+58)
static uint16_t& DATA_008897 = DATA_008896[0x04];
static uint16_t& DATA_008898 = DATA_008896[0x09];
static uint16_t& DATA_008899 = DATA_008896[0x0b];
static uint16_t& DATA_008900 = DATA_008896[0x0c];
static uint16_t& DATA_008901 = DATA_008896[0x13];
static uint16_t& DATA_008902 = DATA_008896[0x14];
static uint16_t& DATA_008903 = DATA_008896[0x1d];
// Batch3: independent
static uint16_t& DATA_008906 = DATA_008896[0x25];  // last cargo row (DATA_008804+0x17e)
static uint32_t& DATA_008907 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x180);  // SCALAR [HIGH]
// Batch3: 6 contiguous dwords (908+909+910 span in ffedat.asm)
using Span008908 = uint32_t[6];
static Span008908& DATA_008908 = *reinterpret_cast<Span008908*>(g_bss8804 + 0x184);
#define DATA_008909 DATA_008908[1]
#define DATA_008910 DATA_008908[2]
static uint32_t& DATA_008911 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x19c);  // SCALAR [HIGH]
static uint32_t& DATA_008912 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x1a0);  // SCALAR [HIGH]
static uint32_t& DATA_008913 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x1a4);  // SCALAR [HIGH]
// Batch3: independent (ffedat foot 22) resized foot 4→22
using Span008919 = uint16_t[11];
static Span008919& DATA_008919 = *reinterpret_cast<Span008919*>(g_bss8804 + 0x1bc);
using Span008920 = char[26];
static Span008920& DATA_008920 = *reinterpret_cast<Span008920*>(g_bss8804 + 0x1d2);  // system name string (ffedat resb 26)
static uint32_t& DATA_008921 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x1ec);  // SCALAR [HIGH]
static uint16_t& DATA_008927 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x204);  // SCALAR [HIGH]
static uint16_t& DATA_008929 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x208);  // SCALAR [HIGH]
static uint16_t& DATA_008930 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x20a);  // SCALAR [HIGH]
static uint16_t& DATA_008931 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x20c);  // SCALAR [HIGH]
static uint16_t& DATA_008933 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x210);  // SCALAR [HIGH]
static uint32_t& DATA_008941 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x22c);  // SCALAR [HIGH]
static u32_unaligned& DATA_008942 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x231);  // SCALAR [HIGH]
static u32_unaligned& DATA_008944 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x23a);  // SCALAR [HIGH]
static u32_unaligned& DATA_008945 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x23e);  // SCALAR [HIGH]
static u32_unaligned& DATA_008946 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x242);  // SCALAR [HIGH]
static uint16_t& DATA_008949 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x24e);  // SCALAR [HIGH]
static uint32_t& DATA_008950 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x250);  // SCALAR [HIGH]
static uint32_t& DATA_008951 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x254);  // SCALAR [HIGH]
static uint32_t& DATA_008952 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x258);  // SCALAR [HIGH]
static uint8_t& DATA_008954 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x269);  // SCALAR [HIGH]
static uint32_t& DATA_008955 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x26c);  // SCALAR [HIGH]
static uint32_t& DATA_008956 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x274);  // SCALAR [HIGH]
static uint32_t& DATA_008957 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x278);  // SCALAR [HIGH]
static uint32_t& DATA_008958 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x27c);  // SCALAR [HIGH]
static uint32_t& DATA_008960 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x280);  // SCALAR [HIGH]
static uint32_t& DATA_008961 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x284);  // SCALAR [HIGH]
static uint32_t& DATA_008962 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x28c);  // SCALAR [HIGH]
static uint32_t& DATA_008963 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x294);  // SCALAR [HIGH]
static uint8_t& DATA_008964 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x298);  // SCALAR [HIGH]
static u32_unaligned& DATA_008966 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x29a);  // SCALAR [HIGH]
static u32_unaligned& DATA_008967 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x29e);  // SCALAR [HIGH]
// Batch3: independent (ffedat foot 24) resized foot 4→24
using Span008968 = uint32_t[6];
static Span008968& DATA_008968 = *reinterpret_cast<Span008968*>(g_bss8804 + 0x2a4);
static uint32_t& DATA_008969 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x2bc);  // SCALAR [HIGH]
static uint32_t& DATA_008970 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x2c0);  // SCALAR [HIGH]
static uint32_t& DATA_008971 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x2c4);  // SCALAR [HIGH]
static u32_unaligned& DATA_008972 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x2c9);  // SCALAR [HIGH]
static u32_unaligned& DATA_008980 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x3f6);  // SCALAR [HIGH]
static int16_t& DATA_008981 = *reinterpret_cast<int16_t*>(g_bss8804 + 0x3f8);  // SCALAR [HIGH]
static uint32_t& DATA_008984 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x3fc);  // SCALAR [HIGH]
static uint32_t& DATA_008986 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x400);  // SCALAR [HIGH]
static uint32_t& DATA_008987 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x404);  // SCALAR [HIGH]
static uint32_t& DATA_008988 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x408);  // SCALAR [HIGH]
// Cargo/system flags: ffedat resb 32; ASM loops 0..0x20 (0x21 entries)
// DATA_008989 / DATA_008990: FUNC_000622 copies 0x26 bytes (DATA_008790 then
// DATA_008791) over both; good 0x20's flag byte is DATA_008990's first byte.
using MarketFlags = uint8_t[0x21];
using MarketFlagsBlock = uint8_t[0x20 + 6];
static MarketFlagsBlock& g_marketFlags = *reinterpret_cast<MarketFlagsBlock*>(g_bss8804 + 0x40c);
static MarketFlags& DATA_008989 = *reinterpret_cast<MarketFlags*>(g_bss8804 + 0x40c);
static uint32_t& DATA_008990 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x42c);
static uint16_t& DATA_008991 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x432);  // SCALAR [HIGH]
static uint16_t& DATA_008992 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x434);  // SCALAR [HIGH]
static uint16_t& DATA_008993 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x436);  // SCALAR [HIGH]
static uint16_t& DATA_008994 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x438);  // SCALAR [HIGH]
static uint16_t& DATA_008995 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x43a);  // SCALAR [HIGH]
static uint16_t& DATA_008996 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x43c);  // word: every ffe.asm access (write 42869)
// FUNC_000622: the station's seed (obj+0xa0) with its halves swapped.
static u32_unaligned& DATA_008998 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x446);
// DATA_008999 / DATA_009000: the 0x12 starport records of 0x322 bytes
// (FUNC_000617/618/622). The ASM reaches record i as DATA_008999 + 0x322*i and
// its slot byte as DATA_009000 + 0x322*i (= record + 0x130): one block.
// FUNC_000378 keeps a record's address in a 32-bit console-record dword (+0x24),
// as the ASM does: g_bss8804 is below 4 GiB.
using StarportRecordHead = uint8_t[0x130];
using StarportRecordTail = uint8_t[14132];
static StarportRecordHead& DATA_008999 = *reinterpret_cast<StarportRecordHead*>(g_bss8804 + 0x44a);
static StarportRecordTail& DATA_009000 = *reinterpret_cast<StarportRecordTail*>(g_bss8804 + 0x57a);
static u32_unaligned& DATA_009001 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x3cae);  // SCALAR [HIGH]
// DATA_009004 / DATA_009005: FUNC_000624's 0x19 news entries of 0x18 bytes
// plus the terminating dword run past DATA_009004's 529 bytes into DATA_009005.
using NewsEntriesHead = uint8_t[529];
using NewsEntriesTail = uint8_t[95];
using NewsEntriesBlock = uint8_t[529 + 95];
static NewsEntriesBlock& g_newsEntries = *reinterpret_cast<NewsEntriesBlock*>(g_bss8804 + 0x3cba);
static NewsEntriesHead& DATA_009004 = *reinterpret_cast<NewsEntriesHead*>(g_bss8804 + 0x3cba);
static NewsEntriesTail& DATA_009005 = *reinterpret_cast<NewsEntriesTail*>(g_bss8804 + 0x3ecb);
static u32_unaligned& DATA_009010 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x3f3a);  // SCALAR [HIGH]
static u32_unaligned& DATA_009011 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x3f3e);  // SCALAR [HIGH]
static uint32_t& DATA_009012 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x3f44);  // SCALAR [HIGH]
static uint32_t& DATA_009013 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x3f48);  // SCALAR [HIGH]
// DATA_009014..DATA_009027 (the SysMenu option bytes, DATA_008804+0x3f4c..)
// are core.cpp's s_sysMenuFlags.
static uint8_t& DATA_009024 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x3f57);  // SCALAR [HIGH]
static uint8_t& DATA_009025 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x3f58);  // SCALAR [HIGH]
static uint8_t& DATA_009026 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x3f59);  // SCALAR [HIGH]
// DATA_009027 (SysMenu option byte 14, DATA_008804+0x3f5a) is core.cpp's
// s_sysMenuFlags[0xe].
// DATA_009028..DATA_009037 (sound option bytes, DATA_008804+0x3f5c..) are
// core.cpp's s_sysMenuFlags[0x10..0x19].
// DATA_009038 (song enable bytes, DATA_008804+0x3f66) is core.cpp's
// s_sysMenuFlags[0x1a..0x2b].
static uint8_t& DATA_009039 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x3f78);  // SCALAR [HIGH]
static uint8_t& DATA_009040 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x3f79);  // SCALAR [HIGH]
static uint8_t& DATA_009041 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x3f7a);  // SCALAR [HIGH]
static uint8_t& DATA_009042 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x3f7b);  // SCALAR [HIGH]
static uint8_t& DATA_009043 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x3f7c);  // SCALAR [HIGH]
static uint8_t& DATA_009044 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x3fb0);  // SCALAR [HIGH]
// Batch3: independent (ffedat foot 2216) resized foot 4→2216
/* Console rows, 0x30 bytes each. FUNC_000361 stores a row's address in the
 * click record's 32-bit +0x20 field (ffe.asm:27875-27878), and the handlers
 * dereference it, so the rows must sit below 4 GiB (g_bss8804 is). */
using ConsoleRowTable = uint8_t[2216];
static ConsoleRowTable& DATA_009048 = *reinterpret_cast<ConsoleRowTable*>(g_bss8804 + 0x402a);
static u32_unaligned& DATA_009049 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0x48d2);  // SCALAR [HIGH]
// DATA_009053 (resb 5) is DATA_008804+0x48e3: core.cpp aliases it into g_bss8804.
static uint32_t& DATA_009054 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x48e8);  // SCALAR [HIGH]
static uint32_t& DATA_009055 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x48ec);  // SCALAR [HIGH]
static uint32_t& DATA_009058 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x48f8);  // POINTER [HIGH]
static uint32_t& DATA_009059 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x48fc);  // SCALAR [HIGH]
// DATA_009060 (resb 28) is DATA_008804+0x4900: core.cpp aliases it into g_bss8804.
static uint8_t& DATA_009062 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x4920);  // SCALAR [HIGH]
static i32_unaligned& DATA_009064 = *reinterpret_cast<i32_unaligned*>(g_bss8804 + 0x4922);  // Tech price multiplier (ASM dword 9..0xc)
static uint8_t& DATA_009065 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x492a);  // SCALAR [HIGH]
static uint32_t& DATA_009066 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x492c);  // SCALAR [HIGH]
static uint32_t& DATA_009067 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x49f4);  // SCALAR [HIGH]
static uint32_t& DATA_009068 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x49f8);  // SCALAR [HIGH]
static uint32_t& DATA_009069 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x49fc);  // SCALAR [HIGH]
static uint32_t& DATA_009073 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x4a18);  // SCALAR [HIGH]
static uint32_t& DATA_009074 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x4a1c);  // SCALAR [HIGH]
// Phase79 slice1: ffedat resb 20 — string-format dest buffer (not scalar).
// FUNC_000439: push dword DATA_009075 / mov eax,DATA_009075 (= address).
using Span009075 = uint8_t[20];
static Span009075& DATA_009075 = *reinterpret_cast<Span009075*>(g_bss8804 + 0x4a20);
static uint32_t& DATA_009076 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x4a34);  // SCALAR [HIGH]
static uint8_t& DATA_009077 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x4a38);  // SCALAR [HIGH]
static uint8_t& DATA_009078 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x4a39);  // SCALAR [HIGH]
// Batch3: independent FLAG: SKIP_EXPAND and0xff vs foot 4; keep foot
// Batch3: independent
// Batch3: independent
// Batch3: 60×0x14 (079/080/081). 080 at +4 (×5 dword idx), 081 at +16.
using Span009079 = uint8_t[60 * 0x14];
static Span009079& DATA_009079 = *reinterpret_cast<Span009079*>(g_bss8804 + 0x4a3a);
#define DATA_009080 DATA_009079[4]
#define DATA_009081 DATA_009079[0x10]
// DATA_009083 (resb 5) is DATA_008804+0x4eeb: core.cpp aliases it into g_bss8804.
static uint32_t& DATA_009084 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x4ef0);  // SCALAR [HIGH]
// Cluster D — combat log: 6 × 0x34 (ffedat resb 312). DAT_002241a8 is field +4 (not a far symbol).
using Span009085 = uint8_t[6 * 0x34];
static Span009085& DATA_009085 = *reinterpret_cast<Span009085*>(g_bss8804 + 0x4ef4);
#define DAT_002241a8 (DATA_009085[4])
static uint32_t& DATA_009087 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x5030);  // SCALAR [HIGH]
// Batch3: independent
// ffedat.asm: DATA_009088 (4B) + DATA_009089 (156B) are twenty 8-byte crew records
using CrewRecords = uint32_t[40];
static CrewRecords& g_crewRecords = *reinterpret_cast<CrewRecords*>(g_bss8804 + 0x5034);
static uint32_t& DATA_009088 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x5034);
using CrewRecordTail = uint32_t[39];
static CrewRecordTail& DATA_009089 = *reinterpret_cast<CrewRecordTail*>(g_bss8804 + 0x5038);
static uint32_t& DATA_009090 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x50d4);  // SCALAR [HIGH]
static uint32_t& DATA_009091 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x50d8);  // SCALAR [HIGH]
static uint32_t& DATA_009092 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x50dc);  // POINTER [HIGH]
// Batch3: zeroed as 6 dwords in FUNC_000*. FLAG: C++ 24 B > foot 14; expand.
using Span009093 = uint32_t[6];
static Span009093& DATA_009093 = *reinterpret_cast<Span009093*>(g_bss8804 + 0x50e0);
// Batch3: independent
// Batch3: independent
// Batch3: 11×0x34 entry table (ffedat 094..112 span 572).
// DATA_009095: ASM ship-stat window at DATA_008804+0x50f8 (Batch 5b).
// core.cpp #undefs this and aliases g_bss8804[0x50f8]. The DATA_009094
// overlay below remains for sibling field macros (9106/…) only.
using Span009094 = uint8_t[11 * 0x34];
static Span009094& DATA_009094 = *reinterpret_cast<Span009094*>(g_bss8804 + 0x50ee);
#define DATA_009095 DATA_009094[0x0a]  // overridden in core.cpp → g_bss8804[0x50f8]
static_assert(0x0a + 4 <= sizeof(DATA_009094), "DATA_009095 dword fits in 9094 blob");
#define DATA_009106 DATA_009094[0x34]
#define DATA_009107 DATA_009094[0x38]
#define DATA_009108 DATA_009094[0x39]  // 0x5127-0x50ee; was wrongly scalar
#define DATA_009109 DATA_009094[0x52]
#define DATA_009110 DATA_009094[0x53]
#define DATA_009111 DATA_009094[0x54]  // 0x5142-0x50ee; was wrongly scalar
#define DATA_009112 DATA_009094[0x68]
static uint32_t& DATA_009096 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x50fc);  // SCALAR [HIGH]
static uint32_t& DATA_009097 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x5100);  // SCALAR [HIGH]
static uint8_t& DATA_009098 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x5110);  // SCALAR [HIGH]
static uint8_t& DATA_009099 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x5111);  // SCALAR [HIGH]
static uint8_t& DATA_009100 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x5112);  // SCALAR [HIGH]
static uint32_t& DATA_009101 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x5114);  // object pointer (FUNC_000234/235)
static uint8_t& DATA_009102 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x5118);  // SCALAR [HIGH]
static uint8_t& DATA_009103 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x5119);  // SCALAR [HIGH]
// Batch3: independent
static uint8_t& DATA_009113 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0x532a);  // SCALAR [HIGH]
static uint32_t& DATA_009114 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x532c);  // SCALAR [HIGH]
// DATA_009117..DATA_009121 (resb 20/2/1/3/26): core.cpp, g_bss8804 + 0x5538.
static uint8_t DATA_009124[256] = {};  // ffedat.asm resb 256: object name buffer
static uint32_t DATA_009126 = 0;                       // SCALAR [HIGH]
// Title ObjectStruct BSS (ffedat DATA_009127..). Labels 009128/129/130/131
// overlay fields at +0x82 / +0x9c / +0x124 / +0x125 (see FUNC_000959).
alignas(4) static uint8_t DATA_009127[0x160] = {};
#define DATA_009128 (*(uint32_t*)(DATA_009127 + 0x82))
#define DATA_009129 (*(uint16_t*)(DATA_009127 + 0x9c))
#define DATA_009130 DATA_009127[0x124]
#define DATA_009131 DATA_009127[0x125]
static uint32_t DATA_009132 = 0;                       // title clock sample
static uint32_t DATA_009135 = 0;                       // SCALAR [HIGH]
// ASM ffedat DATA_009136 resb 2 / DATA_009137 resb 2 — sky-record latch used by
// FUNC_001002/1003 (restore into DATA_008815..817). Must be process-wide: a
// per-TU static left mission's JUMP_004336 latch invisible to core's 1002
// (R1219 shadeB phase fork).
extern uint32_t DATA_009136;
extern uint32_t DATA_009137;
static uint32_t DATA_009138 = 0;                       // SCALAR [HIGH]
extern uint8_t* DATA_009139;                           // dPalSecondary occupancy/cluster
extern uint8_t* DATA_009140;                           // dPalReverse cluster→VGA
static uint32_t DATA_009141 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009142 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009143 = 0;                       // SCALAR [HIGH]
// Batch3: independent
static uint32_t DATA_009144 = 0;  // Batch3: kept scalar (foot=4, no multi-index)
static uint32_t DATA_009145 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009146 = 0;                       // SCALAR [HIGH]
// Batch5a: ASM ffedat DATA_009148 resb 612 — string-expand dest (hashable).
// Below 4 GiB (task 4.110): FUNC_000470 keeps this address in the console
// record's 32-bit dword 0xb, and FF30StringParamPtr(4) dereferences it; the ASM
// links the label at a 32-bit address, the port's image does not.
using ExpandDestTable = uint8_t[612];
static ExpandDestTable& DATA_009148 = Below4G<ExpandDestTable, 9148>();
static_assert(sizeof(DATA_009148) == 612, "Batch5a DATA_009148 size");
static uint32_t DATA_009158 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009159 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009160 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009161 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009162 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009163 = 0xf0;                    // SCALAR [HIGH]  R1174: no joystick buttons held
static uint32_t DATA_009176 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009181 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009188 = 0;                       // SCALAR [HIGH]
// GUI object table: ffedat DATA_009202 resb 6968; entries stride 0xb dwords (44 bytes).
static uint32_t DATA_009202[6968 / 4] = {};
static_assert(sizeof(DATA_009202) == 6968, "DATA_009202 size");
static uint32_t DATA_009206 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009207 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009208 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009209 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009210 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009212 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009213 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009214 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009216 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009217 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009220 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009221 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009225 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009222 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009223 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009224 = 0;                       // SCALAR [HIGH]
static uint8_t* DATA_009235 = nullptr;                 // POINTER [HIGH]
// Batch3: independent
static uint32_t DATA_009236 = 0;  // Batch3: kept scalar (foot=4, no multi-index)
// Batch3: independent
static uint32_t DATA_009237 = 0;  // Batch3: kept scalar (foot=4, no multi-index)
static uint32_t DATA_009238 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009239 = 0;                       // SCALAR [HIGH]
static void* DATA_009240 = nullptr;                    // POINTER [MEDIUM]
static void* DATA_009241 = nullptr;                    // POINTER [MEDIUM]
static void* DATA_009242 = nullptr;                    // POINTER [MEDIUM]
static void* DATA_009243 = nullptr;                    // POINTER [MEDIUM]
// Batch3: independent
static uint32_t DATA_009244 = 0;  // Batch3: kept scalar (foot=4, no multi-index)
static uint32_t DATA_009245 = 0;                       // SCALAR [HIGH]
static void* DATA_009246 = nullptr;                    // POINTER [MEDIUM]
static void* DATA_009247 = nullptr;                    // POINTER [MEDIUM]
static void* DATA_009248 = nullptr;                    // POINTER [MEDIUM]
// Vertex/scratch arena (ffedat resb 32768). Cursor = DATA_007823.
alignas(16) static uint8_t DATA_009251[0x8000] = {};
static uint32_t DATA_009252 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009258 = 0;                       // SCALAR [HIGH]
extern uint32_t DATA_009259;                           // planet mesh colour (FUNC_001815/1876 → 1844/1845)
static int32_t DATA_009260[128] = {};                  // resb 512 — height lookup scratch (planet midpoints)
static void* DATA_009261 = nullptr;                    // POINTER [HIGH]
extern uint32_t DATA_009262;                           // planet shade param5 gate (FUNC_001877)
extern uint32_t DATA_009263;                           // planet span scale (FUNC_001877)
static uint32_t DATA_009264 = 0;                       // SCALAR [HIGH]
extern uint32_t DATA_009265;                           // planet mesh word low16 (FUNC_001877)
extern uint32_t DATA_009267;                           // planet shade divisor (FUNC_001877)
static uint32_t DATA_009268 = 0;                       // Render state flag
extern uint32_t DATA_009269;                           // planet LOD MSB threshold
static void* DATA_009270 = nullptr;                    // POINTER [MEDIUM]
static uint32_t DATA_009271 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009274 = 0;                       // SCALAR [HIGH]
static void* DATA_009275 = nullptr;                    // POINTER [MEDIUM]
static uint32_t DATA_009276 = 0;                       // SCALAR [HIGH]
static uint32_t DATA_009277 = 0;                       // SCALAR [HIGH]
static void* DATA_009278 = nullptr;                    // POINTER [MEDIUM]
static void* DATA_009279 = nullptr;                    // POINTER [MEDIUM]
static void* DATA_009280 = nullptr;                    // POINTER [MEDIUM]
static void* DATA_009281 = nullptr;                    // POINTER [MEDIUM]
static void* DATA_009282 = nullptr;                    // POINTER [MEDIUM]
extern uint32_t DATA_009283;                           // planet min height (FUNC_001876 ↔ 1870-1872)
extern uint32_t DATA_009284;                           // planet min-height tie word
extern uint32_t DATA_009285;                           // planet height-test bound
static uint32_t DATA_102307 = 0;                       // SCALAR [HIGH]
#if defined(__GNUC__) || defined(__clang__)


static_assert(sizeof(DATA_008896) == 76, "Batch3 DATA_008896 size");
// Batch3 contiguity / size asserts
static_assert(sizeof(DATA_001697) == 420, "Batch3 DATA_001697 size");
static_assert(sizeof(DATA_001701) == 1088, "Batch3 DATA_001701 size");
static_assert(sizeof(DATA_001705) == 324, "Batch3 DATA_001705 size");
static_assert(sizeof(DATA_002280) == 2720, "Batch3 DATA_002280 size");
static_assert(sizeof(DATA_002289) == 24, "Batch3 DATA_002289 size");
static_assert(sizeof(DATA_002290) == 20, "Batch3 DATA_002290 size");
static_assert(sizeof(DATA_002291) == 32, "Batch3 DATA_002291 size");
static_assert(sizeof(DATA_002292) == 2692, "Batch3 DATA_002292 size");
static_assert(sizeof(DATA_002308) == 12, "Batch3 DATA_002308 size");
static_assert(sizeof(DATA_004896) == 20, "Batch3 DATA_004896 size");
static_assert(sizeof(DATA_004907) == 5 * sizeof(const uint8_t*), "DATA_004907: five page lists");
static_assert(sizeof(DATA_004908) == 20, "Batch3 DATA_004908 size");
static_assert(sizeof(DATA_005055) == 324, "Batch3 DATA_005055 size");
static_assert(sizeof(DATA_005058) == 9, "Batch3 DATA_005058 size");
static_assert(sizeof(DATA_005107) == 604, "Batch3 DATA_005107 size");
static_assert(sizeof(DATA_005111) == 60, "Batch3 DATA_005111 size");
static_assert(sizeof(DATA_005112) == 36, "Batch3 DATA_005112 size");
static_assert(sizeof(DATA_005114) == 52, "Batch3 DATA_005114 size");
static_assert(sizeof(DATA_005115) == 3, "Batch3 DATA_005115 size");
static_assert(sizeof(DATA_005116) == 5, "Batch3 DATA_005116 size");
static_assert(sizeof(DATA_005117) == 32, "Batch3 DATA_005117 size");
static_assert(sizeof(DATA_005118) == 28, "Batch3 DATA_005118 size");
static_assert(sizeof(DATA_005119) == 7, "Batch3 DATA_005119 size");
static_assert(sizeof(DATA_005120) == 7, "Batch3 DATA_005120 size");
static_assert(sizeof(DATA_005121) == 338, "DATA_005121 PhysObj template");
static_assert(sizeof(DATA_005123) == 4, "Batch3 DATA_005123 size");
static_assert(sizeof(DATA_005124) == 92, "Batch3 DATA_005124 size");
static_assert(sizeof(DATA_005289) == 1452, "Batch3 DATA_005289 size");
static_assert(sizeof(DATA_005300) == 60, "Batch3 DATA_005300 size");
static_assert(sizeof(DATA_005301) == 60, "Batch3 DATA_005301 size");
static_assert(sizeof(DATA_005304) == 480, "Batch3 DATA_005304 size");
static_assert(sizeof(DATA_005319) == 3, "Batch3 DATA_005319 size");
static_assert(sizeof(DATA_006169) == 528, "Batch3 DATA_006169 size");
static_assert(sizeof(DATA_006202) == 48, "Batch3 DATA_006202 size");
static_assert(sizeof(DATA_006203) == 48, "Batch3 DATA_006203 size");
static_assert(sizeof(DATA_006204) == 48, "Batch3 DATA_006204 size");
static_assert(sizeof(DATA_006205) == 48, "Batch3 DATA_006205 size");
static_assert(sizeof(DATA_006206) == 48, "Batch3 DATA_006206 size");
static_assert(sizeof(DATA_006209) == 48, "Batch3 DATA_006209 size");
static_assert(sizeof(DATA_006210) == 24, "Batch3 DATA_006210 size");
static_assert(sizeof(DATA_006211) == 32, "Batch3 DATA_006211 size");
static_assert(sizeof(DATA_006212) == 32, "Batch3 DATA_006212 size");
static_assert(sizeof(DATA_006213) == 32, "Batch3 DATA_006213 size");
static_assert(sizeof(DATA_006215) == 124, "Batch3 DATA_006215 size");
static_assert(sizeof(DATA_006199) == 152, "DATA_006199 ship-type picker");
static_assert(sizeof(DATA_006259) == 338, "DATA_006259 munition PhysObj template");
static_assert(sizeof(DATA_006260) == 18, "DATA_006260 munition byte table");
static_assert(sizeof(DATA_006261) == 56, "Batch3 DATA_006261 size");
static_assert(sizeof(DATA_006262) == 60, "Batch3 DATA_006262 size");
static_assert(sizeof(DATA_006263) == 72, "Batch3 DATA_006263 size");
static_assert(sizeof(DATA_006264) == 72, "Batch3 DATA_006264 size");
static_assert(sizeof(DATA_006265) == 72, "DATA_006265 damage-tier table");
static_assert(sizeof(DATA_006266) == 60, "Batch3 DATA_006266 size");
static_assert(sizeof(DATA_006267) == 20, "DATA_006267 munition-type byte table");
static_assert(sizeof(DATA_006269) == 56, "Batch3 DATA_006269 size");
static_assert(sizeof(DATA_006274) == 48, "Batch3 DATA_006274 size");
static_assert(sizeof(DATA_006275) == 16, "Batch3 DATA_006275 size");
static_assert(sizeof(DATA_006793) == 702, "Batch3 DATA_006793 size");
static_assert(sizeof(DATA_006795) == 234, "Batch3 DATA_006795 size");
static_assert(sizeof(DATA_006800) == 208, "Batch3 DATA_006800 size");
static_assert(sizeof(DATA_006803) == 68, "Batch3 DATA_006803 size");
static_assert(sizeof(DATA_006805) == 576, "Batch3 DATA_006805 size");
static_assert(sizeof(DATA_006806) == 16514, "Batch3 DATA_006806 size (128*129 + 2-byte bilinear guard, task 4.182)");
static_assert(sizeof(DATA_006810) == 32, "Batch3 DATA_006810 size");
static_assert(sizeof(DATA_006811) == 32, "Batch3 DATA_006811 size");
static_assert(sizeof(DATA_006816) == 48, "Batch3 DATA_006816 size");
static_assert(sizeof(DATA_006818) == 48, "Batch3 DATA_006818 size");
static_assert(sizeof(DATA_006820) == 140, "Batch3 DATA_006820 size");
static_assert(sizeof(DATA_006821) == 256, "Batch3 DATA_006821 size");
static_assert(sizeof(DATA_006822) == 700, "Batch3 DATA_006822 size");
static_assert(sizeof(DATA_007110) == 18360, "Batch3 DATA_007110 size");
static_assert(sizeof(DATA_007116) == 16, "Batch3 DATA_007116 size");
static_assert(sizeof(DATA_008920) == 26, "DATA_008920 system name");
static_assert(sizeof(DATA_007129) == 1024, "Batch3 DATA_007129 size");
static_assert(sizeof(DATA_007130) == 128, "Batch3 DATA_007130 size");
static_assert(sizeof(DATA_007132) == 192, "Batch3 DATA_007132 size");
static_assert(sizeof(DATA_007136) == 16, "Batch3 DATA_007136 size");
static_assert(sizeof(DATA_007137) == 16, "Batch3 DATA_007137 size");
static_assert(sizeof(DATA_007138) == 16, "Batch3 DATA_007138 size");
static_assert(sizeof(DATA_007188) == 32, "Batch3 DATA_007188 size");
static_assert(sizeof(DATA_007207) == 60, "Batch3 DATA_007207 size");
static_assert(sizeof(DATA_007208) == 40, "Batch3 DATA_007208 size");
static_assert(sizeof(DATA_007209) == 44, "Batch3 DATA_007209 size");
static_assert(sizeof(DATA_007220) == 40, "Batch3 DATA_007220 size");
static_assert(sizeof(DATA_007221) == 40, "Batch3 DATA_007221 size");
static_assert(sizeof(DATA_007226) == 5, "DATA_007226 Mine string");
static_assert(sizeof(DATA_007227) == 8, "DATA_007227 Missile string");
static_assert(sizeof(DATA_007241) == 40, "Batch3 DATA_007241 size");
static_assert(sizeof(DATA_007260) == 60, "Batch3 DATA_007260 size");
static_assert(sizeof(DATA_007268) == 64, "Batch3 DATA_007268 size");
static_assert(sizeof(DATA_007649) == 84, "Batch3 DATA_007649 size");
static_assert(sizeof(DATA_007652) == 220, "Batch3 DATA_007652 size");
static_assert(sizeof(DATA_007687) == 1024, "Batch3 DATA_007687 size");
static_assert(sizeof(DATA_007759) == 144, "Batch3 DATA_007759 size");
static_assert(sizeof(DATA_007811) == kMaterialTableBytes, "material table size");
static_assert(sizeof(DATA_007818) == 64, "Batch3 DATA_007818 size");
static_assert(sizeof(DATA_007820) == 56, "Batch3 DATA_007820 size");
static_assert(sizeof(DATA_007821) == 56, "Batch3 DATA_007821 size");
static_assert(sizeof(DATA_007822) == 56, "Batch3 DATA_007822 size");
static_assert(sizeof(DATA_007825) == 33, "Batch3 DATA_007825 size");
static_assert(sizeof(DATA_007828) == 56, "Batch3 DATA_007828 size");
static_assert(sizeof(DATA_007861) == 30, "Batch3 DATA_007861 size");
static_assert(sizeof(DATA_007869) == 30, "Batch3 DATA_007869 size");
static_assert(sizeof(DATA_007871) == 30, "Batch3 DATA_007871 size");
static_assert(sizeof(DATA_008428) == 263, "Batch3 DATA_008428 size");
static_assert(sizeof(DATA_008548) == 32, "Batch3 DATA_008548 size");
static_assert(sizeof(DATA_008908) == 24, "Batch3 DATA_008908 size");
static_assert(sizeof(DATA_008561) == 5400, "Batch3 DATA_008561 size");
static_assert(sizeof(DATA_008568) == 400, "Batch3 DATA_008568 size");
static_assert(sizeof(DATA_008569) == 280, "Batch4 RC1 DATA_008569 size");
static_assert(sizeof(DATA_008665) == 1536, "Batch3 DATA_008665 size");
static_assert(sizeof(DATA_008678) == 10380, "Batch3 DATA_008678 size");
static_assert(sizeof(DATA_008695) == 372, "Batch3 DATA_008695 size");
static_assert(sizeof(DATA_008776) == 138, "Batch3 DATA_008776 size");
static_assert(sizeof(DATA_008778) == 78, "Batch3 DATA_008778 size");
static_assert(sizeof(DATA_008789) == 9002, "Batch3 DATA_008789 size");
static_assert(sizeof(DATA_008790) == 32, "Batch3 DATA_008790 size");
static_assert(sizeof(DATA_008792) == 38, "Batch3 DATA_008792 size");
static_assert(sizeof(DATA_008831) == 24, "Batch3 DATA_008831 size");
static_assert(sizeof(DATA_008919) == 22, "Batch3 DATA_008919 size");
static_assert(sizeof(DATA_008968) == 24, "Batch3 DATA_008968 size");
static_assert(sizeof(DATA_009000) == 14132, "Batch3 DATA_009000 size");
static_assert(sizeof(DATA_009004) == 529, "Batch3 DATA_009004 size");
static_assert(sizeof(DATA_009048) == 2216, "Batch3 DATA_009048 size");
static_assert(sizeof(DATA_005110) == 36, "Phase79 DATA_005110 size");
static_assert(sizeof(DATA_008683) == 76, "Phase79 DATA_008683 size");
static_assert(sizeof(DATA_009075) == 20, "Phase79 DATA_009075 size");
static_assert(sizeof(DATA_009079) == 1200, "Batch3 DATA_009079 size");
static_assert(sizeof(DATA_009085) == 312, "Batch3 DATA_009085 size");
static_assert(sizeof(DATA_009093) == 24, "Batch3 DATA_009093 size");
static_assert(sizeof(DATA_009094) == 572, "Batch3 DATA_009094 size");
static_assert(sizeof(DATA_009127) == 352, "Batch3 DATA_009127 size");
static_assert(sizeof(DATA_009251) == 32768, "Batch3 DATA_009251 size");
static_assert(sizeof(DATA_009260) == 512, "Batch3 DATA_009260 size");
#pragma GCC diagnostic pop
#endif
}  // namespace jjffe
