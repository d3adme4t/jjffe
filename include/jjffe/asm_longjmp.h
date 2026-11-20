#pragma once

namespace jjffe {

// The ASM's _LongJmp. _asmmain has two _SetJmp sites: DATA_009125 before the
// title loop (ffe.asm:68550-68555), where FUNC_000916 and FUNC_000917 land,
// and DATA_009126 before the game loop (ffe.asm:68663-68677), where
// FUNC_000919 lands. The port throws AsmLongJmp where the ASM jumps and
// catches it where the ASM's _SetJmp stands: the C++ unwind discards the same
// frames the jump does, and none of them holds state the jump would restore.
enum class AsmJumpTarget { TitleLoop, GameLoop };
struct AsmLongJmp {
    AsmJumpTarget target;
    int value;
};

// _asmmain after _SetJmp(DATA_009125) returns (ffe.asm:68557-68598): the
// title loop's setup.
void TitleLoopEnter();
// One title-loop frame (ffe.asm:68601-68648) with no title key pressed.
void TitleLoopFrame();

} // namespace jjffe
