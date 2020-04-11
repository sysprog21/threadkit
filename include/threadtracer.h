#ifndef THREAD_TRACER_H
#define THREAD_TRACER_H

#include <stddef.h>

#define TT_ENTRY(S) tt_signin(S)
int tt_signin(const char *threadname);

#define TT_BEGIN(S) tt_stamp("generic", S, "B")
#define TT_END(S) tt_stamp("generic", S, "E")
int tt_stamp(const char *cat, const char *tag, const char *phase);

#define TT_REPORT() tt_report(NULL)
int tt_report(const char *oname);

#endif
