/*
* This file is part of Gix-IDE, an IDE and platform for GnuCOBOL
* Copyright (C) 2021 Marco Ridoni
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License
* as published by the Free Software Foundation; either version 3,
* or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; see the file COPYING.LIB.  If
* not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor
* Boston, MA 02110-1301 USA
*/

/* Nothing here */

#include <stdio.h>

#if 0
extern void gixsql_shutdown();

#ifdef __linux__
void __attribute__ ((destructor)) some_name_unload(void)
{    
    fprintf(stderr, "--- terminating libgixsql\n");
    gixsql_shutdown();
} 
#endif

#ifdef _WIN32

#include "Windows.h"

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD fdwReason,     // reason for calling function
    LPVOID lpvReserved)  // reserved
{
    switch (fdwReason)
    {
    case DLL_PROCESS_DETACH:
        fprintf(stderr, "--- terminating libgixsql\n");    
        gixsql_shutdown();
        break;
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH.
}
#endif

#endif