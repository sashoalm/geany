/*
*   Copyright (c) 1998-2002, Darren Hiebert
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License version 2 or (at your option) any later version.
*
*   External interface to get.c
*/
#ifndef CTAGS_MAIN_GET_H
#define CTAGS_MAIN_GET_H

/*
*   INCLUDE FILES
*/
#include "general.h"  /* must always come first */

#include "ctags.h"  /* to define langType */

/*
*   MACROS
*/
/*  Is the character valid as a character of a C identifier?
 *  VMS allows '$' in identifiers.
 */
#define cppIsident(c)  (isalnum(c) || (c) == '_' || (c) == '$')

/*  Is the character valid as the first character of a C identifier?
 *  C++ allows '~' in destructors.
 *  VMS allows '$' in identifiers.
 *  Vala allows '@' in identifiers.
 */
#define isident1(c)  (isalpha(c) || (c) == '_' || (c) == '~' || (c) == '$' || (c) == '@')

/*
*   FUNCTION PROTOTYPES
*/
extern boolean isBraceFormat (void);
extern unsigned int getDirectiveNestLevel (void);
extern void cppInit (const boolean state, const boolean hasAtLiteralStrings,
                     const boolean hasCxxRawLiteralStrings,
                     const kindOption *defineMacroKind);
extern void cppTerminate (void);
extern void cppBeginStatement (void);
extern void cppEndStatement (void);
extern void cppUngetc (const int c);
extern int cppGetc (void);
extern int skipOverCComment (void);
extern char *getArglistFromFilePos(MIOPos startPosition, const char *tokenName);
extern char *getArglistFromStr(char *buf, const char *name);

#endif  /* CTAGS_MAIN_GET_H */

/* vi:set tabstop=4 shiftwidth=4: */
