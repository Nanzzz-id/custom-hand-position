#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fungsi-fungsi dari libpreloader.so
// Persis seperti yang dipakai libThirdPersonNametag.so
void  GlossInit();
void* GlossOpen(const char* lib_name);
void* GlossSymbol(void* handle, const char* sym_name);
int   GlossHook(void* target, void* hook, void** orig);
int   GlossHookByName(void* handle, const char* sym, void* hook, void** orig);

// pl_resolve_signature - cari fungsi MC pakai byte pattern
// Jauh lebih stabil daripada offset karena tidak berubah tiap versi
// pattern: string hex seperti "? ? 40 F9 ? ? ? EB ..."
// "?" = wildcard (skip byte ini)
void* pl_resolve_signature(const char* lib, const char* pattern);

#ifdef __cplusplus
}
#endif
