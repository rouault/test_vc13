#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#if 0
typedef long long GIntBig;
typedef unsigned long long GUIntBig;
#endif
#define CPLAssert assert
#define HAVE_VSNPRINTF
#define HAVE_SNPRINTF

/************************************************************************/
/*                  CPLvsnprintf_get_end_of_formatting()                */
/************************************************************************/

static const char* CPLvsnprintf_get_end_of_formatting(const char* fmt)
{
    char ch;
    /*flag */
    for( ; (ch = *fmt) != '\0'; ++fmt )
    {
        if( ch == '\'' )
            continue; /* bad idea as this is locale specific */
        if( ch == '-' || ch == '+' || ch == ' ' || ch == '#' || ch == '0' )
            continue;
        break;
    }

    /* field width */
    for( ; (ch = *fmt) != '\0'; ++fmt )
    {
        if( ch == '$' )
            return NULL; /* we don't want to support this */
        if( *fmt >= '0' && *fmt <= '9' )
            continue;
        break;
    }

    /* precision */
    if( ch == '.' )
    {
        ++fmt;
        for( ; (ch = *fmt) != '\0'; ++fmt )
        {
            if( ch == '$' )
                return NULL; /* we don't want to support this */
            if( *fmt >= '0' && *fmt <= '9' )
                continue;
            break;
        }
    }

    /* length modifier */
    for( ; (ch = *fmt) != '\0'; ++fmt )
    {
        if( ch == 'h' || ch == 'l' || ch == 'j' || ch == 'z' ||
            ch == 't' || ch == 'L' )
            continue;
        else if( ch == 'I' && fmt[1] == '6' && fmt[2] == '4' )
            fmt += 2;
        else
            return fmt;
    }

    return NULL;
}

/************************************************************************/
/*                           CPLvsnprintf()                             */
/************************************************************************/

#define call_native_snprintf(type) \
    local_ret = snprintf(str + offset_out, size - offset_out, localfmt, va_arg(wrk_args, type))

/** vsnprintf() wrapper that is not sensitive to LC_NUMERIC settings.
  *
  * This function has the same contract as standard vsnprintf(), except that
  * formatting of floating-point numbers will use decimal point, whatever the
  * current locale is set.
  *
  * @param str output buffer
  * @param size size of the output buffer (including space for terminating nul)
  * @param fmt formatting string
  * @param args arguments
  * @return the number of characters (excluding terminating nul) that would be
  * written if size is big enough
  * @since GDAL 2.0
  */
int CPLvsnprintf(char *str, size_t size, const char* fmt, va_list args)
{
    if( size == 0 )
        return vsnprintf(str, size, fmt, args);

    va_list wrk_args;

#ifdef va_copy
    va_copy( wrk_args, args );
#else
    wrk_args = args;
#endif

    const char* fmt_ori = fmt;
    size_t offset_out = 0;
    char ch;
    bool bFormatUnknown = false;

    for( ; (ch = *fmt) != '\0'; ++fmt )
    {
        if( ch == '%' )
        {
            const char* ptrend = CPLvsnprintf_get_end_of_formatting(fmt+1);
            if( ptrend == NULL || ptrend - fmt >= 20 )
            {
                bFormatUnknown = true;
                break;
            }
            char end = *ptrend;
            char end_m1 = ptrend[-1];

            char localfmt[22];
            memcpy(localfmt, fmt, ptrend - fmt + 1);
            localfmt[ptrend-fmt+1] = '\0';

            int local_ret;
            if( end == '%' )
            {
                if( offset_out == size-1 )
                    break;
                local_ret = 1;
                str[offset_out] = '%';
            }
            else if( end == 'd' ||  end == 'i' ||  end == 'c' )
            {
                if( end_m1 == 'h' )
                    call_native_snprintf(int);
                else if( end_m1 == 'l' && ptrend[-2] != 'l' )
                    call_native_snprintf(long);
#if 0
                else if( end_m1 == 'l' && ptrend[-2] == 'l' )
                    call_native_snprintf(GIntBig);
                else if( end_m1 == '4' && ptrend[-2] == '6' && ptrend[-3] == 'I' ) /* Microsoft I64 modifier */
                    call_native_snprintf(GIntBig);
#endif
                else if( end_m1 == 'z')
                    call_native_snprintf(size_t);
                else if( (end_m1 >= 'a' && end_m1 <= 'z') ||
                         (end_m1 >= 'A' && end_m1 <= 'Z') )
                {
                    bFormatUnknown = true;
                    break;
                }
                else
                    call_native_snprintf(int);
            }
            else  if( end == 'o' || end == 'u' || end == 'x' || end == 'X' )
            {
                if( end_m1 == 'h' )
                    call_native_snprintf(unsigned int);
                else if( end_m1 == 'l' && ptrend[-2] != 'l' )
                    call_native_snprintf(unsigned long);
#if 0
                else if( end_m1 == 'l' && ptrend[-2] == 'l' )
                    call_native_snprintf(GUIntBig);
                else if( end_m1 == '4' && ptrend[-2] == '6' && ptrend[-3] == 'I' ) /* Microsoft I64 modifier */
                    call_native_snprintf(GUIntBig);
#endif
                else if( end_m1 == 'z')
                    call_native_snprintf(size_t);
                else if( (end_m1 >= 'a' && end_m1 <= 'z') ||
                         (end_m1 >= 'A' && end_m1 <= 'Z') )
                {
                    bFormatUnknown = true;
                    break;
                }
                else
                    call_native_snprintf(unsigned int);
            }
            else if( end == 'e' || end == 'E' ||
                     end == 'f' || end == 'F' ||
                     end == 'g' || end == 'G' ||
                     end == 'a' || end == 'A' )
            {
                if( end_m1 == 'L' )
                    call_native_snprintf(long double);
                else
                    call_native_snprintf(double);
                /* MSVC vsnprintf() returns -1... */
                if( local_ret <  0 || offset_out + local_ret >= size )
                    break;
                for( int j = 0; j < local_ret; ++j )
                {
                    if( str[offset_out + j] == ',' )
                    {
                        str[offset_out + j] = '.';
                        break;
                    }
                }
            }
            else if( end == 's' )
            {
                const char* pszPtr = va_arg(wrk_args, const char*);
                CPLAssert(pszPtr);
                local_ret = snprintf(str + offset_out, size - offset_out, localfmt, pszPtr);
            }
            else if( end == 'p' )
            {
                call_native_snprintf(void*);
            }
            else
            {
                bFormatUnknown = true;
                break;
            }
            /* MSVC vsnprintf() returns -1... */
            if( local_ret <  0 || offset_out + local_ret >= size )
                break;
            offset_out += local_ret;
            fmt = ptrend;
        }
        else
        {
            if( offset_out == size-1 )
                break;
            str[offset_out++] = *fmt;
        }
    }
    if( ch == '\0' && offset_out < size )
        str[offset_out] = '\0';
    else
    {
        if( bFormatUnknown )
        {
            fprintf(stderr, "CPLvsnprintf() called with unsupported "
                      "formatting string: %s", fmt_ori);
        }
#ifdef va_copy
        va_end( wrk_args );
        va_copy( wrk_args, args );
#else
        wrk_args = args;
#endif
#if defined(HAVE_VSNPRINTF)
        offset_out = vsnprintf(str, size, fmt_ori, wrk_args);
#else
        offset_out = vsprintf(str, fmt_ori, wrk_args);
#endif
    }

#ifdef va_copy
    va_end( wrk_args );
#endif

    return static_cast<int>( offset_out );
}

/************************************************************************/
/*                           CPLsnprintf()                              */
/************************************************************************/

/** snprintf() wrapper that is not sensitive to LC_NUMERIC settings.
  *
  * This function has the same contract as standard snprintf(), except that
  * formatting of floating-point numbers will use decimal point, whatever the
  * current locale is set.
  *
  * @param str output buffer
  * @param size size of the output buffer (including space for terminating nul)
  * @param fmt formatting string
  * @param ... arguments
  * @return the number of characters (excluding terminating nul) that would be
  * written if size is big enough
  * @since GDAL 2.0
  */
int CPLsnprintf(char *str, size_t size, const char* fmt, ...)
{
    va_list args;

    va_start( args, fmt );
    const int ret = CPLvsnprintf( str, size, fmt, args );
    va_end( args );
    return ret;
}

static void TextFillR( char *pszTarget, unsigned int nMaxChars, 
                       const char *pszSrc )

{
    if( strlen(pszSrc) < nMaxChars )
    {
        memset( pszTarget, ' ', nMaxChars - strlen(pszSrc) );
        memcpy( pszTarget + nMaxChars - strlen(pszSrc), pszSrc, 
                strlen(pszSrc) );
    }
    else
        memcpy( pszTarget, pszSrc, nMaxChars );
}

/************************************************************************/
/*                         USGSDEMPrintDouble()                         */
/*                                                                      */
/*      The MSVC C runtime library uses 3 digits                        */
/*      for the exponent.  This causes various problems, so we try      */
/*      to correct it here.                                             */
/************************************************************************/

#if defined(_MSC_VER) || defined(__MSVCRT__)
#  define MSVC_HACK
#endif

static void USGSDEMPrintDouble( char *pszBuffer, double dfValue )

{
    if ( !pszBuffer )
        return;

#ifdef MSVC_HACK
    const char *pszFormat = "%25.15e";
#else
    const char *pszFormat = "%24.15e";
#endif

    const int DOUBLE_BUFFER_SIZE = 64;
    char szTemp[DOUBLE_BUFFER_SIZE];
#if defined(HAVE_SNPRINTF)
    CPLsnprintf( szTemp, DOUBLE_BUFFER_SIZE, pszFormat, dfValue );
#else
    CPLsprintf( szTemp, pszFormat, dfValue );
#endif
    szTemp[DOUBLE_BUFFER_SIZE - 1] = '\0';

    for( int i = 0; szTemp[i] != '\0'; i++ )
    {
        if( szTemp[i] == 'E' || szTemp[i] == 'e' )
            szTemp[i] = 'D';
#ifdef MSVC_HACK
        if( (szTemp[i] == '+' || szTemp[i] == '-')
            && szTemp[i+1] == '0' && isdigit(szTemp[i+2]) 
            && isdigit(szTemp[i+3]) && szTemp[i+4] == '\0' )
        {
            szTemp[i+1] = szTemp[i+2];
            szTemp[i+2] = szTemp[i+3];
            szTemp[i+3] = '\0';
            break;
        }
#endif
    }

    TextFillR( pszBuffer, 24, szTemp );
}

int main()
{
    char* szBuf = (char*)malloc(50);
    //printf("%d\n", CPLsnprintf(szBuf, 50, "%24.15e", -2.88e5));
    //printf("%s\n", szBuf);
    memset(szBuf, 0, 50);
    USGSDEMPrintDouble(szBuf, -2.88e5);
    printf("%s\n", szBuf);
    free(szBuf);
    return 0;
}
