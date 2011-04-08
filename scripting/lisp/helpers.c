/*
 * Copyright (c) 2010-2011 Kevin M. Bowling, <kevin.bowling@kev009.com>, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

static inline
cl_object
cdcl_str (const char* string)
{
    return make_simple_base_string((char*) string);
}

static inline
cl_object
cdcl_str_intern (const char* string)
{
    return cl_intern(1, cdcl_str(string));
}

static inline
bool
cdcl_to_bool (cl_object self)
{
    return self != Cnil;
}

static inline
cl_object
cdcl_eval (const char* format, ...)
{
    va_list ap;

    va_start(ap, format);

    CDString* code   = CD_CreateStringFromFormatList(format, ap);
    cl_object result = Cnil;

    CL_CATCH_ALL_BEGIN(ecl_process_env()) {
        result = cl_eval(ecl_read_from_cstring((char*) CD_StringContent(code)));
    } CL_CATCH_ALL_IF_CAUGHT {
        errno = EILSEQ;
    } CL_CATCH_ALL_END;

    va_end(ap);

    CD_DestroyString(code);

    return result;
}

static inline
void
cdcl_in_package (const char* name)
{
    si_select_package(cdcl_str(name));
}

static
CDString*
cdcl_MakeParameters (CDList* parameters, va_list args)
{
    CDString* code = CD_CreateString();

    CD_LIST_FOREACH(parameters, it) {
        const char* type = (const char*) CD_ListIteratorValue(it);

        if (CD_CStringIsEqual(type, "CDClient")) {
            code = CD_AppendStringAndClean(code, CD_CreateStringFromFormat(
                "(craftd:wrap (uffi:make-pointer %p) 'client) ", va_arg(args, void*)));
        }
    }

    return code;
}
