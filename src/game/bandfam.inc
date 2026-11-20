// R261 FAMILY-B table (hull<<16|meshIndex): back-facing band-family faces the
// SIDECAR draws every pose. Ground truths banked this round:
//   - ffeasm gdb: FUNC_001708 dispatch on #61 = faceidx {0,1,3,4,5,7,12,13}
//     with mats 0x400b/0x4009 - IDENTICAL in all 8 poses (zero per-pose cull
//     at the walk layer; occlusion = painter order + rect + shade0!=0).
//   - band bbox (x36..276,y35..110) @06 ASM final-value histogram:
//     {5:2564, 4:1897, 6:1663, 7:650, 8:460, 1:201, 3:53, 22:134}.
//   - measured remedies: uncull+textured +6093; uncull+BuildTexShadePalette
//     flat +16268; FlatRgbToPal(0x222)=22 != sidecar 5 - the paint ramp is the
//     A708-TAIL one (material DATA_007811 row rgb + facing-word addition,
//     nibble-clamped, DPal16toPal'd per slot - core.cpp:24538-24583, already
//     ported!) fed to a 1805-style TEXTURED draw (29275: shadePal[i]=
//     DPal16toPal(job ramp word)). Next build: route these faces through that
//     exact ramp at draw time (walk-faceidx<->meshIndex map needed; join
//     scratch/r1/join61b.py matched faces 8/10 by verts already).
0x003d0004, // hull 61 mesh 4 (walk faceidx ~3, mat 0x400b, sh 0x666)
0x003d0005, // hull 61 mesh 5 (mat 0x400b, sh 0x666)
0x003d0006, // hull 61 mesh 6 (mat 0x4009, sh 0x222/0x666)
0x003d0007, // hull 61 mesh 7
0x003d0008, // hull 61 mesh 8
