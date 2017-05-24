/*
 * $Id: sprintf_irc.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $ 
 */

#ifndef SPRINTF_IRC
#define SPRINTF_IRC

#include <stdarg.h>

/*=============================================================================
 * Proto types
 */

extern int vsprintf_irc(char *str, const char *format, va_list);
extern int ircsprintf(char *str, const char *format, ...);

#endif /* SPRINTF_IRC */
