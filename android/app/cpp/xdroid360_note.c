// xdroid360_note.c: the launcher reads the ABI from this note without loading the library
// (recipe format v2, "Result"): an SHT_NOTE section, which lld keeps under --gc-sections,
// in a PT_NOTE segment. Name "XDroid360", type 1, desc = uint32 abi, uint32 reserved (0).
// Kept identical in both games' front ends.
#include <stdint.h>
#include "xdroid360_abi.h"   // XDROID360_ABI (today DAO_ABI_VERSION): the one source of the ABI
__attribute__((section(".note.xdroid360"), aligned(4), used, retain))
static const struct { uint32_t namesz, descsz, type; char name[12]; uint32_t abi, reserved; }
xdroid360_note = { 10, 8, 1, "XDroid360", XDROID360_ABI, 0 };
