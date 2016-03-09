/*
 * The MIT License (MIT)
 *
 * Copyright © 2016 Franklin "Snaipe" Mathieu <http://snai.pe/>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "assert.h"
#include <stdint.h>

#include "plt.h"
#include <tlhelp32.h>

#define IDIR_IMPORT 1 // Index of the import directory entry

plt_ctx plt_init_ctx (void) {
    return NULL;
}

static plt_fn **plt_find_offset (const char *name, plt_lib *lib)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,
            GetCurrentProcessId());
    mmk_assert (snap != INVALID_HANDLE_VALUE);

    MODULEENTRY32 mod = { .dwSize = sizeof(MODULEENTRY32) };
    for (BOOL more = Module32First(snap, &mod); more;
            more = Module32Next(snap, &mod))
    {
        plt_fn **fn = plt_get_offset (mod.hModule, name);
        if (fn) {
            if (lib)
                *lib = mod.hModule;
            return fn;
        }
    }
    mmk_assert (GetLastError() == ERROR_NO_MORE_FILES);
    return NULL;
}

plt_lib plt_get_lib (plt_ctx ctx, const char *name)
{
    (void) ctx;
    if (!name)
        name = "self";

    if (!strcmp(name, "self"))
        return GetModuleHandle(NULL);

    if (!strncmp(name, "file:", 5) || !strncmp(name, "lib:", 4)) {
        const char *val = strchr(name, ':') + 1;
        HMODULE m = GetModuleHandle(val);
        if (!m) {
            size_t sz = strlen(val) + 4;
            char buf[sz];
            snprintf(buf, sz, "lib%s", val);
            m = GetModuleHandle(buf);
        }
        return m;
    } else if (strncmp(name, "sym:", 4)) {
        plt_lib lib;
        plt_fn **fn = plt_find_offset (name + 4, &lib);
        if (fn)
            return lib;
        return NULL;
    } else {
        char *end_sel = strchr(name, ':');
        if (end_sel) {
            size_t len = (size_t) (end_sel - name + 1);
            fprintf(stderr, "mimick: unknown '%.*s' selector.\n", (int) len, name);
        } else {
            fprintf(stderr, "mimick: unknown target kind '%s'.\n", name);
        }
    }
    abort ();
}

static inline PIMAGE_NT_HEADERS nt_header_from_lib (plt_lib lib)
{
    PIMAGE_DOS_HEADER dos_hdr = (PIMAGE_DOS_HEADER) lib;
    return (PIMAGE_NT_HEADERS) ((char *) dos_hdr + dos_hdr->e_lfanew);
}

static inline PIMAGE_IMPORT_DESCRIPTOR get_first_import_descriptor (plt_lib lib)
{
    PIMAGE_NT_HEADERS nthdr = nt_header_from_lib (lib);
    DWORD off = nthdr->OptionalHeader.DataDirectory[IDIR_IMPORT].VirtualAddress;
    mmk_assert (off != 0);
    return (PIMAGE_IMPORT_DESCRIPTOR) ((char *) lib + off);
}

plt_fn **plt_get_offset (plt_lib lib, const char *name)
{
    char *base = lib;
    for (PIMAGE_IMPORT_DESCRIPTOR entry = get_first_import_descriptor (lib);
            entry->Name;
            entry++)
    {
        uintptr_t *thunk = (uintptr_t *) (base + entry->FirstThunk);
        PIMAGE_THUNK_DATA thunk_data = (PIMAGE_THUNK_DATA) (base
                + (entry->OriginalFirstThunk
                    ? entry->OriginalFirstThunk
                    : entry->FirstThunk));
        for (; thunk_data->u1.AddressOfData != 0; ++thunk, ++thunk_data)
        {
            PIMAGE_IMPORT_BY_NAME ibn = (void *) thunk_data->u1.AddressOfData;
            if (!strcmp(name, base + (uintptr_t) ibn->Name))
                return (plt_fn **) thunk;
        }
    }
    return NULL;
}

void plt_set_offset(plt_fn **offset, plt_fn *newval)
{
    DWORD old;
    VirtualProtect(offset, sizeof (void*), PAGE_EXECUTE_READWRITE, &old);
    *offset = newval;
    VirtualProtect(offset, sizeof (void*), old, &old);
}

plt_fn *plt_get_real_fn(plt_ctx ctx, const char *name)
{
    (void) ctx;
    plt_fn **fn = plt_find_offset (name + 4, NULL);
    if (fn)
        return *fn;
    return NULL;
}
