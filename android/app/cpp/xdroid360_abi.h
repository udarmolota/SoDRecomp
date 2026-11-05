// The version of the interface between the launcher (XDroid360's GameActivity) and the game
// library libgame.so: the JNI methods, the SDL build, the command-line options. It is not
// defined here: it comes from the launcher's own header in the DAO tree (read only), whose
// path CMakeLists.txt passes as XDROID360_ABI_HEADER, so SoD's library can't drift from the
// launcher. Both nativeAbiVersion (virtual_pad_jni.cpp) and the ELF note the launcher reads
// before loading (xdroid360_note.c) use XDROID360_ABI.
#pragma once

#include XDROID360_ABI_HEADER

// DAO's header names it DAO_ABI_VERSION today; XDROID360_ABI once DAO renames it.
#if !defined(XDROID360_ABI) && defined(DAO_ABI_VERSION)
#define XDROID360_ABI DAO_ABI_VERSION
#endif
#ifndef XDROID360_ABI
#error "XDROID360_ABI_HEADER defines no ABI version"
#endif
