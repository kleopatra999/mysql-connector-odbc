/*
  Copyright (C) 2007 MySQL AB

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  There are special exceptions to the terms and conditions of the GPL
  as it is applied to this software. View the full text of the exception
  in file LICENSE.exceptions in the top-level directory of this software
  distribution.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/**
  @file  unicode.c
  @brief Entry points for Unicode versions of ODBC functions
*/

#include "driver.h"
#include <sqlucode.h>
#include <stdarg.h>


#define NOT_IMPLEMENTED \
  return SQL_ERROR


/* Forward declarations. */
SQLCHAR *sqlwchar_as_utf8(SQLWCHAR *str, SQLINTEGER *len);
SQLINTEGER utf8_as_sqlwchar(SQLWCHAR *out, SQLINTEGER out_max, SQLCHAR *in,
                            SQLINTEGER in_len);

SQLRETURN SQL_API
SQLColAttributeWImpl(SQLHSTMT hstmt, SQLUSMALLINT column,
                     SQLUSMALLINT field, SQLPOINTER char_attr,
                     SQLSMALLINT char_attr_max, SQLSMALLINT *char_attr_len,
                     SQLLEN *num_attr);
SQLRETURN SQL_API
SQLGetConnectAttrWImpl(SQLHDBC hdbc, SQLINTEGER attribute, SQLPOINTER value,
                       SQLINTEGER value_max, SQLINTEGER *value_len);
SQLRETURN SQL_API
SQLGetDiagRecWImpl(SQLSMALLINT handle_type, SQLHANDLE handle,
                   SQLSMALLINT record, SQLWCHAR *sqlstate,
                   SQLINTEGER *native_error, SQLWCHAR *message,
                   SQLSMALLINT message_max, SQLSMALLINT *message_len);
SQLRETURN SQL_API
SQLPrepareWImpl(SQLHSTMT hstmt, SQLWCHAR *str, SQLINTEGER str_len);

SQLRETURN SQL_API
SQLSetConnectAttrWImpl(SQLHDBC hdbc, SQLINTEGER attribute,
                       SQLPOINTER value, SQLINTEGER value_len);


/**
  Duplicate a SQLCHAR in the specified character set as a SQLWCHAR.

  @param[in]      charset_info  Character set to convert into
  @param[in]      str           String to convert
  @param[in,out]  len           Pointer to length of source (in bytes) or
                                destination string (in chars)
  @param[out]     errors        Pointer to count of errors in conversion

  @return  Pointer to a newly allocated SQLWCHAR, or @c NULL
*/
SQLWCHAR *sqlchar_as_sqlwchar(CHARSET_INFO *charset_info, SQLCHAR *str,
                              SQLINTEGER *len, uint *errors)
{
  SQLCHAR *str_end;
  SQLWCHAR *out;
  SQLINTEGER i, out_bytes;
  my_bool free_str= FALSE;

  if (*len == SQL_NTS)
    *len= strlen((char *)str);

  if (!str || *len == 0)
  {
    *len= 0;
    return NULL;
  }

  if (charset_info->number != 33) /* not utf-8 */
  {
    uint32 used_bytes, used_chars;
    size_t u8_max= (*len / charset_info->mbminlen *
                    utf8_charset_info->mbmaxlen + 1);
    SQLCHAR *u8= (SQLCHAR *)my_malloc(u8_max, MYF(0));

    if (!u8)
    {
      *len= -1;
      return NULL;
    }

    *len= copy_and_convert((char *)u8, u8_max, utf8_charset_info,
                           (char *)str, *len, charset_info,
                           &used_bytes, &used_chars, errors);
    str= u8;
    free_str= TRUE;
  }

  str_end= str + *len;

  out_bytes= (*len + 1) * sizeof(SQLWCHAR);

  out= (SQLWCHAR *)my_malloc(out_bytes, MYF(0));
  if (!out)
  {
    *len= -1;
    return NULL;
  }

  for (i= 0; *str && str < str_end; )
  {
    if (sizeof(SQLWCHAR) == 4)
    {
      str+= utf8toutf32(str, (UTF32 *)(out + i++));
    }
    else
    {
      UTF32 u32;
      str+= utf8toutf32(str, &u32);
      i+= utf32toutf16(u32, (UTF16 *)(out + i));
    }
  }

  *len= i;
  out[i]= 0;

  if (free_str)
    x_free(str);

  return out;
}


/**
  Duplicate a SQLWCHAR as a SQLCHAR in the specified character set.

  @param[in]      charset_info  Character set to convert into
  @param[in]      str           String to convert
  @param[in,out]  len           Pointer to length of source (in chars) or
                                destination string (in bytes)
  @param[out]     errors        Pointer to count of errors in conversion

  @return  Pointer to a newly allocated SQLCHAR, or @c NULL
*/
SQLCHAR *sqlwchar_as_sqlchar(CHARSET_INFO *charset_info, SQLWCHAR *str,
                             SQLINTEGER *len, uint *errors)
{
  SQLWCHAR *str_end;
  SQLCHAR *out;
  SQLINTEGER i, u8_len, out_bytes;
  UTF8 u8[7];
  uint32 used_bytes, used_chars;

  *errors= 0;

  if (charset_info->number == 33)
    return sqlwchar_as_utf8(str, len);

  if (*len == SQL_NTS)
    *len= sqlwchar_strlen(str);
  if (!str || *len == 0)
  {
    *len= 0;
    return NULL;
  }

  out_bytes= *len * charset_info->mbmaxlen * sizeof(SQLCHAR) + 1;
  out= (SQLCHAR *)my_malloc(out_bytes, MYF(0));
  if (!out)
  {
    *len= -1;
    return NULL;
  }

  str_end= str + *len;

  for (i= 0; str < str_end; )
  {
    if (sizeof(SQLWCHAR) == 4)
    {
      u8_len= utf32toutf8((UTF32)*str++, u8);
    }
    else
    {
      UTF32 u32;
      str+= utf16toutf32((UTF16 *)str, &u32);
      u8_len= utf32toutf8(u32, u8);
    }

    i+= copy_and_convert((char *)out + i, out_bytes - i, charset_info,
                         (char *)u8, u8_len, utf8_charset_info, &used_bytes,
                         &used_chars, errors);
  }

  *len= i;
  out[i]= '\0';
  return out;
}


/**
  Duplicate a SQLWCHAR as a SQLCHAR encoded as UTF-8.

  @param[in]      str           String to convert
  @param[in,out]  len           Pointer to length of source (in chars) or
                                destination string (in bytes)

  @return  Pointer to a newly allocated SQLCHAR, or @c NULL
*/
SQLCHAR *sqlwchar_as_utf8(SQLWCHAR *str, SQLINTEGER *len)
{
  SQLWCHAR *str_end;
  UTF8 *u8;
  SQLSMALLINT i;

  if (*len == SQL_NTS)
    *len= sqlwchar_strlen(str);
  if (!str || *len == 0)
  {
    *len= 0;
    return NULL;
  }

  u8= (UTF8 *)my_malloc(sizeof(UTF8) * 4 * *len + 1, MYF(0));
  if (!u8)
  {
    *len= -1;
    return NULL;
  }

  str_end= str + *len;

  if (sizeof(SQLWCHAR) == 4)
  {
    for (i= 0; str < str_end; )
      i+= utf32toutf8((UTF32)*str++, u8 + i);
  }
  else
  {
    for (i= 0; str < str_end; )
    {
      UTF32 u32;
      str+= utf16toutf32((UTF16 *)str, &u32);
      i+= utf32toutf8(u32, u8 + i);
    }
  }

  *len= i;
  u8[i]= '\0';
  return u8;
}


/**
  Convert a SQLCHAR encoded as UTF-8 into a SQLWCHAR.

  @param[out]     out           Pointer to SQLWCHAR buffer
  @param[in]      out_max       Length of @c out buffer
  @param[in]      in            Pointer to SQLCHAR string (utf-8 encoded)
  @param[in]      in_len        Length of @c in (in bytes)

  @return  Number of characters stored in the @c out buffer
*/
SQLINTEGER utf8_as_sqlwchar(SQLWCHAR *out, SQLINTEGER out_max, SQLCHAR *in,
                            SQLINTEGER in_len)
{
  SQLINTEGER i;
  SQLWCHAR *pos, *out_end;

  for (i= 0, pos= out, out_end= out + out_max; i < in_len && pos < out_end; )
  {
    if (sizeof(SQLWCHAR) == 4)
      i+= utf8toutf32(in + i, (UTF32 *)pos++);
    else
    {
      UTF32 u32;
      i+= utf8toutf32(in + i, &u32);
      pos+= utf32toutf16(u32, (UTF16 *)pos);
    }
  }

  *out= 0;
  return pos - out;
}


SQLRETURN SQL_API
SQLColAttributeW(SQLHSTMT hstmt, SQLUSMALLINT column,
                 SQLUSMALLINT field, SQLPOINTER char_attr,
                 SQLSMALLINT char_attr_max, SQLSMALLINT *char_attr_len,
#ifdef USE_SQLCOLATTRIBUTE_SQLLEN_PTR
                 SQLLEN *num_attr
#else
                 SQLPOINTER num_attr
#endif
               )
{
  return SQLColAttributeWImpl(hstmt, column, field, char_attr, char_attr_max,
                              char_attr_len, num_attr);
}


SQLRETURN SQL_API
SQLColAttributeWImpl(SQLHSTMT hstmt, SQLUSMALLINT column,
                     SQLUSMALLINT field, SQLPOINTER char_attr,
                     SQLSMALLINT char_attr_max, SQLSMALLINT *char_attr_len,
                     SQLLEN *num_attr)
{
  STMT *stmt= (STMT *)hstmt;
  SQLCHAR *value= NULL;
  SQLWCHAR *wvalue;
  SQLINTEGER len= SQL_NTS;
  uint errors;
  SQLRETURN rc= MySQLColAttribute(hstmt, column, field, &value, num_attr);

  if (value)
  {
    /* SQL_DESC_TYPE_NAME is the only one we need to clean up for now. */
    my_bool free_value= (field == SQL_DESC_TYPE_NAME);
    wvalue= sqlchar_as_sqlwchar(stmt->dbc->cxn_charset_info, value,
                                &len, &errors);

    if (len > char_attr_max - 1)
      rc= set_error(stmt, MYERR_01004, NULL, 0);

    if (char_attr_len)
      *char_attr_len= len;

    if (char_attr_max > 0)
    {
      len= min(len, char_attr_max - 1);
      (void)memcpy((char *)char_attr, (const char *)wvalue,
                   len * sizeof(SQLWCHAR));
      ((SQLWCHAR *)char_attr)[len]= 0;
    }

    if (free_value)
      x_free(value);
    x_free(wvalue);
  }

  return rc;
}


SQLRETURN SQL_API
SQLColAttributesW(SQLHSTMT hstmt, SQLUSMALLINT column, SQLUSMALLINT field,
                  SQLPOINTER char_attr, SQLSMALLINT char_attr_max,
                  SQLSMALLINT *char_attr_len, SQLLEN *num_attr)
{
  return SQLColAttributeWImpl(hstmt, column, field, char_attr, char_attr_max,
                              char_attr_len, num_attr);
}


SQLRETURN SQL_API
SQLColumnPrivilegesW(SQLHSTMT hstmt,
                     SQLWCHAR *catalog, SQLSMALLINT catalog_len,
                     SQLWCHAR *schema, SQLSMALLINT schema_len,
                     SQLWCHAR *table, SQLSMALLINT table_len,
                     SQLWCHAR *column, SQLSMALLINT column_len)
{
  SQLRETURN rc;
  SQLCHAR *catalog8, *schema8, *table8, *column8;
  DBC *dbc= ((STMT *)hstmt)->dbc;
  SQLINTEGER len= SQL_NTS;;
  uint errors= 0;

  catalog8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, catalog, &len, &errors);
  catalog_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  schema8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, schema, &len, &errors);
  schema_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  table8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, table, &len, &errors);
  table_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  column8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, column, &len, &errors);
  column_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  rc= MySQLColumnPrivileges(hstmt, catalog8, catalog_len, schema8, schema_len,
                            table8, table_len, column8, column_len);

  x_free(catalog8);
  x_free(schema8);
  x_free(table8);
  x_free(column8);

  return rc;
}


SQLRETURN SQL_API
SQLColumnsW(SQLHSTMT hstmt,
            SQLWCHAR *catalog, SQLSMALLINT catalog_len,
            SQLWCHAR *schema, SQLSMALLINT schema_len,
            SQLWCHAR *table, SQLSMALLINT table_len,
            SQLWCHAR *column, SQLSMALLINT column_len)
{
  SQLRETURN rc;
  SQLCHAR *catalog8, *schema8, *table8, *column8;
  DBC *dbc= ((STMT *)hstmt)->dbc;
  SQLINTEGER len= SQL_NTS;;
  uint errors= 0;

  catalog8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, catalog, &len, &errors);
  catalog_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  schema8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, schema, &len, &errors);
  schema_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  table8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, table, &len, &errors);
  table_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  column8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, column, &len, &errors);
  column_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  rc= MySQLColumns(hstmt, catalog8, catalog_len, schema8, schema_len,
                   table8, table_len, column8, column_len);

  x_free(catalog8);
  x_free(schema8);
  x_free(table8);
  x_free(column8);

  return rc;
}


SQLRETURN SQL_API
SQLConnectW(SQLHDBC hdbc, SQLWCHAR *dsn, SQLSMALLINT dsn_len_in,
            SQLWCHAR *user, SQLSMALLINT user_len_in,
            SQLWCHAR *auth, SQLSMALLINT auth_len_in)
{
  SQLRETURN rc;
  SQLINTEGER dsn_len= dsn_len_in, user_len= user_len_in, auth_len= auth_len_in;
  SQLCHAR *dsn8= sqlwchar_as_utf8(dsn, &dsn_len),
          *user8= sqlwchar_as_utf8(user, &user_len),
          *auth8= sqlwchar_as_utf8(auth, &auth_len);

  ((DBC *)hdbc)->unicode= TRUE; /* Hooray, a Unicode connection! */

  rc= MySQLConnect(hdbc, dsn8, dsn_len, user8, user_len, auth8, auth_len);

  x_free(dsn8);
  x_free(user8);
  x_free(auth8);

  return rc;
}


SQLRETURN SQL_API
SQLDriverConnectW(SQLHDBC hdbc, SQLHWND hwnd,
                  SQLWCHAR *in, SQLSMALLINT in_len_in,
                  SQLWCHAR *out, SQLSMALLINT out_max, SQLSMALLINT *out_len,
                  SQLUSMALLINT completion)
{
  SQLRETURN rc;
  SQLINTEGER in_len= in_len_in;
  SQLSMALLINT out8_max;
  SQLCHAR *out8, *in8= sqlwchar_as_utf8(in, &in_len);

  if (in_len == SQL_NTS)
    in_len= sqlwchar_strlen(in);

  out8_max= sizeof(SQLCHAR) * 4 * out_max;
  out8= (SQLCHAR *)my_malloc(out8_max + 1, MYF(0));
  if (!out8)
  {
    rc= set_dbc_error((DBC *)hdbc, "HY001", NULL, 0);
    goto error;
  }

  ((DBC *)hdbc)->unicode= TRUE; /* Hooray, a Unicode connection! */

  rc= MySQLDriverConnect(hdbc, hwnd, in8, in_len, out8, out8_max, out_len,
                         completion);

  /* Now we have to convert out8 back into a SQLWCHAR. */
  *out_len= utf8_as_sqlwchar(out, out_max, out8, *out_len);

error:
  x_free(out8);
  x_free(in8);

  return rc;
}


SQLRETURN SQL_API
SQLDescribeColW(SQLHSTMT hstmt, SQLUSMALLINT column,
                SQLWCHAR *name, SQLSMALLINT name_max, SQLSMALLINT *name_len,
                SQLSMALLINT *type, SQLULEN *size, SQLSMALLINT *scale,
                SQLSMALLINT *nullable)
{
  STMT *stmt= (STMT *)hstmt;
  SQLCHAR *value= NULL;
  SQLWCHAR *wvalue= NULL;
  SQLINTEGER len= SQL_NTS;
  SQLSMALLINT free_value;
  uint errors;

  SQLRETURN rc= MySQLDescribeCol(hstmt, column, &value, &free_value, type,
                                 size, scale, nullable);

  if (free_value == -1)
  {
    set_mem_error(&stmt->dbc->mysql);
    return handle_connection_error(stmt);
  }

  if (value)
  {
    wvalue= sqlchar_as_sqlwchar(stmt->dbc->cxn_charset_info, value, &len,
                                &errors);
    if (len == -1)
    {
      if (free_value)
        x_free(value);
      set_mem_error(&stmt->dbc->mysql);
      return handle_connection_error(stmt);
    }

    if (len > name_max - 1)
      rc= set_error(stmt, MYERR_01004, NULL, 0);

    if (name_len)
      *name_len= len;

    if (name_max > 0)
    {
      len= min(len, name_max - 1);
      (void)memcpy((char *)name, (const char *)wvalue,
                   len * sizeof(SQLWCHAR));
      ((SQLWCHAR *)name)[len]= 0;
    }

    if (free_value)
      x_free(value);
    x_free(wvalue);
  }

  return rc;
}


SQLRETURN SQL_API
SQLErrorW(SQLHENV henv, SQLHDBC hdbc, SQLHSTMT hstmt, SQLWCHAR *sqlstate,
          SQLINTEGER *native_error, SQLWCHAR *message, SQLSMALLINT message_max,
          SQLSMALLINT *message_len)
{
  SQLRETURN rc= SQL_INVALID_HANDLE;

  if (hstmt)
  {
    rc= SQLGetDiagRecWImpl(SQL_HANDLE_STMT, hstmt, 1, sqlstate, native_error,
                           message, message_max, message_len);
    if (rc == SQL_SUCCESS)
      CLEAR_STMT_ERROR(hstmt);
  }
  else if (hdbc)
  {
    rc= SQLGetDiagRecWImpl(SQL_HANDLE_DBC, hdbc, 1, sqlstate, native_error,
                           message, message_max, message_len);
    if (rc == SQL_SUCCESS)
      CLEAR_DBC_ERROR(hstmt);
  }
  else if (henv)
  {
    rc= SQLGetDiagRecWImpl(SQL_HANDLE_ENV, henv, 1, sqlstate, native_error,
                           message, message_max, message_len);
    if (rc == SQL_SUCCESS)
      CLEAR_ENV_ERROR(hstmt);
  }

  return rc;
}


SQLRETURN SQL_API
SQLExecDirectW(SQLHSTMT hstmt, SQLWCHAR *str, SQLINTEGER str_len)
{
  int error;

  if ((error= SQLPrepareWImpl(hstmt, str, str_len)))
    return error;
  error= my_SQLExecute((STMT *)hstmt);

  return error;
}


SQLRETURN SQL_API
SQLForeignKeysW(SQLHSTMT hstmt,
                SQLWCHAR *pk_catalog, SQLSMALLINT pk_catalog_len,
                SQLWCHAR *pk_schema, SQLSMALLINT pk_schema_len,
                SQLWCHAR *pk_table, SQLSMALLINT pk_table_len,
                SQLWCHAR *fk_catalog, SQLSMALLINT fk_catalog_len,
                SQLWCHAR *fk_schema, SQLSMALLINT fk_schema_len,
                SQLWCHAR *fk_table, SQLSMALLINT fk_table_len)
{
  SQLRETURN rc;
  SQLCHAR *pk_catalog8, *pk_schema8, *pk_table8,
          *fk_catalog8, *fk_schema8, *fk_table8;
  DBC *dbc= ((STMT *)hstmt)->dbc;
  SQLINTEGER len= SQL_NTS;;
  uint errors= 0;

  pk_catalog8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, pk_catalog, &len,
                                   &errors);
  pk_catalog_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  pk_schema8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, pk_schema, &len,
                                  &errors);
  pk_schema_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  pk_table8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, pk_table, &len,
                                 &errors);
  pk_table_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  fk_catalog8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, fk_catalog, &len,
                                   &errors);
  fk_catalog_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  fk_schema8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, fk_schema, &len,
                                  &errors);
  fk_schema_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  fk_table8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, fk_table, &len,
                                 &errors);
  fk_table_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  rc= MySQLForeignKeys(hstmt, pk_catalog8, pk_catalog_len,
                       pk_schema8, pk_schema_len, pk_table8, pk_table_len,
                       fk_catalog8, fk_catalog_len, fk_schema8, fk_schema_len,
                       fk_table8, fk_table_len);

  x_free(pk_catalog8);
  x_free(pk_schema8);
  x_free(pk_table8);
  x_free(fk_catalog8);
  x_free(fk_schema8);
  x_free(fk_table8);

  return rc;
}


SQLRETURN SQL_API
SQLGetConnectAttrW(SQLHDBC hdbc, SQLINTEGER attribute, SQLPOINTER value,
                   SQLINTEGER value_max, SQLINTEGER *value_len)
{
  return SQLGetConnectAttrWImpl(hdbc, attribute, value, value_max, value_len);
}


SQLRETURN SQL_API
SQLGetConnectAttrWImpl(SQLHDBC hdbc, SQLINTEGER attribute, SQLPOINTER value,
                       SQLINTEGER value_max, SQLINTEGER *value_len)
{
  DBC *dbc= (DBC *)hdbc;
  SQLCHAR *char_value= NULL;

  SQLRETURN rc= MySQLGetConnectAttr(hdbc, attribute, &char_value, value);

  if (char_value)
  {
    SQLWCHAR *wvalue;
    SQLINTEGER len= SQL_NTS;
    uint errors;

    wvalue= sqlchar_as_sqlwchar(dbc->cxn_charset_info, char_value,
                                &len, &errors);

    if (len > value_max - 1)
      rc= set_conn_error(dbc, MYERR_01004, NULL, 0);

    if (value_len)
      *value_len= len;

    if (value_max > 0)
    {
      len= min(len, value_max - 1);
      (void)memcpy((char *)value, (const char *)wvalue,
                   len * sizeof(SQLWCHAR));
      ((SQLWCHAR *)value)[len]= 0;
    }

    x_free(wvalue);
  }

  return rc;
}


SQLRETURN SQL_API
SQLGetConnectOptionW(SQLHDBC hdbc, SQLUSMALLINT option, SQLPOINTER param)
{
  return SQLGetConnectAttrWImpl(hdbc, option, param,
                                ((option == SQL_ATTR_CURRENT_CATALOG) ?
                                 SQL_MAX_OPTION_STRING_LENGTH : 0), NULL);
}


SQLRETURN SQL_API
SQLGetCursorNameW(SQLHSTMT hstmt, SQLWCHAR *cursor, SQLSMALLINT cursor_max,
                  SQLSMALLINT *cursor_len)
{
  SQLRETURN rc= SQL_SUCCESS;
  STMT *stmt= (STMT *)hstmt;
  SQLWCHAR *name;
  SQLINTEGER len= SQL_NTS;
  uint errors;

  CLEAR_STMT_ERROR(stmt);

  if (cursor_max < 0)
    return set_error(stmt, MYERR_S1090, NULL, 0);

  name= sqlchar_as_sqlwchar(stmt->dbc->cxn_charset_info,
                            MySQLGetCursorName(hstmt), &len, &errors);

  if (cursor_len)
    *cursor_len= len;

  /* Warn if name truncated */
  if (len > cursor_max - 1)
    rc= set_error(stmt, MYERR_01004, NULL, 0);

  if (cursor_max > 0)
  {
    len= min(len, cursor_max - 1);
    (void)memcpy((char *)cursor, (const char *)name, len * sizeof(SQLWCHAR));
    cursor[len]= 0;
  }

  x_free(name);

  return rc;
}


SQLRETURN SQL_API
SQLGetDiagFieldW(SQLSMALLINT handle_type, SQLHANDLE handle,
                 SQLSMALLINT record, SQLSMALLINT field,
                 SQLPOINTER info, SQLSMALLINT info_max,
                 SQLSMALLINT *info_len)
{
  DBC *dbc;
  SQLCHAR *value= NULL;
  SQLINTEGER len= SQL_NTS;

  SQLRETURN rc= MySQLGetDiagField(handle_type, handle, record, field,
                                  &value, info);

  switch (handle_type) {
  case SQL_HANDLE_DBC:
    dbc= (DBC *)handle;
    break;
  case SQL_HANDLE_STMT:
    dbc= ((STMT *)handle)->dbc;
    break;
  case SQL_HANDLE_ENV:
  default:
    dbc= NULL;
  }

  if (value)
  {
    uint errors;
    SQLWCHAR *wvalue= sqlchar_as_sqlwchar(dbc ? dbc->cxn_charset_info :
                                          default_charset_info,
                                          value, &len, &errors);

    if (len > info_max - 1)
      rc= set_conn_error(dbc, MYERR_01004, NULL, 0);

    if (info_len)
      *info_len= len;

    if (info_max > 0)
    {
      len= min(len, info_max - 1);
      (void)memcpy((char *)info, (const char *)wvalue,
                   len * sizeof(SQLWCHAR));
      ((SQLWCHAR *)info)[len]= 0;
    }

    x_free(wvalue);
  }

  return rc;
}


SQLRETURN SQL_API
SQLGetDiagRecW(SQLSMALLINT handle_type, SQLHANDLE handle,
               SQLSMALLINT record, SQLWCHAR *sqlstate,
               SQLINTEGER *native_error, SQLWCHAR *message,
               SQLSMALLINT message_max, SQLSMALLINT *message_len)
{
  return SQLGetDiagRecWImpl(handle_type, handle, record, sqlstate, native_error,
                            message, message_max, message_len);
}


SQLRETURN SQL_API
SQLGetDiagRecWImpl(SQLSMALLINT handle_type, SQLHANDLE handle,
                   SQLSMALLINT record, SQLWCHAR *sqlstate,
                   SQLINTEGER *native_error, SQLWCHAR *message,
                   SQLSMALLINT message_max, SQLSMALLINT *message_len)
{
  DBC *dbc;
  SQLCHAR *msg_value= NULL, *sqlstate_value= NULL;
  SQLINTEGER len= SQL_NTS;
  uint errors;

  switch (handle_type) {
  case SQL_HANDLE_DBC:
    dbc= (DBC *)handle;
    break;
  case SQL_HANDLE_STMT:
    dbc= ((STMT *)handle)->dbc;
    break;
  case SQL_HANDLE_ENV:
  default:
    dbc= NULL;
  }

  if (message_max < 0)
    return SQL_ERROR;

  SQLRETURN rc= MySQLGetDiagRec(handle_type, handle, record, &sqlstate_value,
                                native_error, &msg_value);

  if (msg_value)
  {
    SQLWCHAR *wvalue= sqlchar_as_sqlwchar(dbc ? dbc->cxn_charset_info :
                                          default_charset_info,
                                          msg_value, &len, &errors);

    if (len > message_max - 1)
      rc= set_conn_error(dbc, MYERR_01004, NULL, 0);

    if (message_len)
      *message_len= len;

    if (message_max > 0)
    {
      len= min(len, message_max - 1);
      (void)memcpy((char *)message, (const char *)wvalue,
                   len * sizeof(SQLWCHAR));
      ((SQLWCHAR *)message)[len]= 0;
    }

    x_free(wvalue);
  }

  if (sqlstate && sqlstate_value)
  {
    SQLWCHAR *wvalue= sqlchar_as_sqlwchar(dbc ? dbc->cxn_charset_info :
                                          default_charset_info,
                                          sqlstate_value, &len, &errors);

    (void)memcpy((char *)sqlstate, (const char *)wvalue,
                 5 * sizeof(SQLWCHAR));
    ((SQLWCHAR *)sqlstate)[5]= 0;

    x_free(wvalue);
  }

  return rc;
}


SQLRETURN SQL_API
SQLGetInfoW(SQLHDBC hdbc, SQLUSMALLINT type, SQLPOINTER value,
            SQLSMALLINT value_max, SQLSMALLINT *value_len)
{
  DBC *dbc= (DBC *)hdbc;
  SQLCHAR *char_value= NULL;
  SQLINTEGER len= SQL_NTS;
  uint errors;

  SQLRETURN rc= MySQLGetInfo(hdbc, type, &char_value, value);

  if (char_value)
  {
    SQLWCHAR *wvalue= sqlchar_as_sqlwchar(dbc->cxn_charset_info,
                                          char_value, &len, &errors);

    if (len > value_max - 1)
      rc= set_conn_error(dbc, MYERR_01004, NULL, 0);

    if (value_len)
      *value_len= len;

    if (value_max > 0)
    {
      len= min(len, value_max - 1);
      (void)memcpy((char *)value, (const char *)wvalue,
                   len * sizeof(SQLWCHAR));
      ((SQLWCHAR *)value)[len]= 0;
    }

    x_free(wvalue);
  }

  return rc;
}


SQLRETURN SQL_API
SQLGetStmtAttrW(SQLHSTMT hstmt, SQLINTEGER attribute, SQLPOINTER value,
                SQLINTEGER value_max, SQLINTEGER *value_len)
{
  return MySQLGetStmtAttr(hstmt, attribute, value, value_max, value_len);
}


/* This shouldn't be necessary, but iODBC expects it. */
SQLRETURN SQL_API
SQLGetTypeInfoW(SQLHSTMT hstmt, SQLSMALLINT type)
{
  return MySQLGetTypeInfo(hstmt, type);
}


SQLRETURN SQL_API
SQLNativeSqlW(SQLHDBC hdbc, SQLWCHAR *in, SQLINTEGER in_len,
              SQLWCHAR *out, SQLINTEGER out_max, SQLINTEGER *out_len)
{
  SQLRETURN rc= SQL_SUCCESS;

  if (in_len == SQL_NTS)
    in_len= sqlwchar_strlen(in);

  if (out)
    *out_len= in_len;

  if (in_len > out_max)
    rc= set_conn_error((DBC *)hdbc, MYERR_01004, NULL, 0);

  if (out_max > 0)
  {
    if (in_len > out_max - 1)
      in_len= out_max - 1;

    (void)memcpy((char *)out, (const char *)in, in_len * sizeof(SQLWCHAR));
    out[in_len]= 0;
  }

  return SQL_SUCCESS;
}


SQLRETURN SQL_API
SQLPrepareW(SQLHSTMT hstmt, SQLWCHAR *str, SQLINTEGER str_len)
{
  return SQLPrepareWImpl(hstmt, str, str_len);
}


SQLRETURN SQL_API
SQLPrepareWImpl(SQLHSTMT hstmt, SQLWCHAR *str, SQLINTEGER str_len)
{
  STMT *stmt= (STMT *)hstmt;
  uint errors;
  SQLCHAR *conv= sqlwchar_as_sqlchar(stmt->dbc->cxn_charset_info,
                                     str, &str_len, &errors);
  /* Character conversion problems are not tolerated. */
  if (errors)
  {
    x_free(conv);
    return set_stmt_error(stmt, "22018", NULL, 0);
  }

  return MySQLPrepare(hstmt, conv, str_len, TRUE);
}


SQLRETURN SQL_API
SQLPrimaryKeysW(SQLHSTMT hstmt,
                SQLWCHAR *catalog, SQLSMALLINT catalog_len,
                SQLWCHAR *schema, SQLSMALLINT schema_len,
                SQLWCHAR *table, SQLSMALLINT table_len)
{
  SQLRETURN rc;
  SQLCHAR *catalog8, *schema8, *table8;
  DBC *dbc= ((STMT *)hstmt)->dbc;
  SQLINTEGER len= SQL_NTS;;
  uint errors= 0;

  catalog8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, catalog, &len, &errors);
  catalog_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  schema8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, schema, &len, &errors);
  schema_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  table8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, table, &len, &errors);
  table_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  rc= MySQLPrimaryKeys(hstmt, catalog8, catalog_len, schema8, schema_len,
                       table8, table_len);

  x_free(catalog8);
  x_free(schema8);
  x_free(table8);

  return rc;
}


SQLRETURN SQL_API
SQLProceduresW(SQLHSTMT hstmt,
               SQLWCHAR *catalog, SQLSMALLINT catalog_len,
               SQLWCHAR *schema, SQLSMALLINT schema_len,
               SQLWCHAR *proc, SQLSMALLINT proc_len)
{
  SQLRETURN rc;
  SQLCHAR *catalog8, *schema8, *proc8;
  DBC *dbc= ((STMT *)hstmt)->dbc;
  SQLINTEGER len= SQL_NTS;;
  uint errors= 0;

  catalog8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, catalog, &len, &errors);
  catalog_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  schema8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, schema, &len, &errors);
  schema_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  proc8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, proc, &len, &errors);
  proc_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  rc= MySQLProcedures(hstmt, catalog8, catalog_len, schema8, schema_len,
                      proc8, proc_len);

  x_free(catalog8);
  x_free(schema8);
  x_free(proc8);

  return rc;
}


SQLRETURN SQL_API
SQLSetConnectAttrW(SQLHDBC hdbc, SQLINTEGER attribute,
                   SQLPOINTER value, SQLINTEGER value_len)
{
  return SQLSetConnectAttrWImpl(hdbc, attribute, value, value_len);
}


SQLRETURN SQL_API
SQLSetConnectAttrWImpl(SQLHDBC hdbc, SQLINTEGER attribute,
                       SQLPOINTER value, SQLINTEGER value_len)
{
  SQLRETURN rc;
  DBC *dbc= (DBC *)hdbc;
  my_bool free_value= FALSE;

  if (attribute == SQL_ATTR_CURRENT_CATALOG)
  {
    uint errors= 0;
    value= sqlwchar_as_sqlchar(dbc->cxn_charset_info,
                               value, &value_len, &errors);
    free_value= TRUE;
  }

  rc= MySQLSetConnectAttr(hdbc, attribute, value, value_len);

  if (free_value)
    x_free(value);

  return rc;
}


SQLRETURN SQL_API
SQLSetCursorNameW(SQLHSTMT hstmt, SQLWCHAR *name, SQLSMALLINT name_len)
{
  SQLRETURN rc;
  DBC *dbc= ((STMT *)hstmt)->dbc;
  SQLINTEGER len= name_len;
  uint errors= 0;
  SQLCHAR *name_char= sqlwchar_as_sqlchar(dbc->cxn_charset_info,
                                          name, &len, &errors);

  rc= MySQLSetCursorName(hstmt, name_char, len);

  x_free(name_char);

  /* Character conversion problems are not tolerated. */
  if (errors)
  {
    return set_stmt_error((STMT *)hstmt, "HY000",
                          "Cursor name included characters that could not "
                          "be converted to connection character set", 0);
  }

  return rc;
}


SQLRETURN SQL_API
SQLSetConnectOptionW(SQLHDBC hdbc, SQLUSMALLINT option, SQLULEN param)
{
  return SQLSetConnectAttrWImpl(hdbc, option, (SQLPOINTER)param,
                                ((option == SQL_ATTR_CURRENT_CATALOG) ?
                                 SQL_NTS : 0));
}


SQLRETURN SQL_API
SQLSetStmtAttrW(SQLHSTMT hstmt, SQLINTEGER attribute,
                SQLPOINTER value, SQLINTEGER value_len)
{
  /* Nothing special to do, since we don't have any string stmt attribs */
  return MySQLSetStmtAttr(hstmt, attribute, value, value_len);
}


SQLRETURN SQL_API
SQLSpecialColumnsW(SQLHSTMT hstmt, SQLUSMALLINT type,
                   SQLWCHAR *catalog, SQLSMALLINT catalog_len,
                   SQLWCHAR *schema, SQLSMALLINT schema_len,
                   SQLWCHAR *table, SQLSMALLINT table_len,
                   SQLUSMALLINT scope, SQLUSMALLINT nullable)
{
  SQLRETURN rc;
  SQLCHAR *catalog8, *schema8, *table8;
  DBC *dbc= ((STMT *)hstmt)->dbc;
  SQLINTEGER len= SQL_NTS;;
  uint errors= 0;

  catalog8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, catalog, &len, &errors);
  catalog_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  schema8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, schema, &len, &errors);
  schema_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  table8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, table, &len, &errors);
  table_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  rc= MySQLSpecialColumns(hstmt, type, catalog8, catalog_len,
                          schema8, schema_len, table8, table_len,
                          scope, nullable);

  x_free(catalog8);
  x_free(schema8);
  x_free(table8);

  return rc;
}


SQLRETURN SQL_API
SQLTablesW(SQLHSTMT hstmt,
           SQLWCHAR *catalog, SQLSMALLINT catalog_len,
           SQLWCHAR *schema, SQLSMALLINT schema_len,
           SQLWCHAR *table, SQLSMALLINT table_len,
           SQLWCHAR *type, SQLSMALLINT type_len)
{
  SQLRETURN rc;
  SQLCHAR *catalog8, *schema8, *table8, *type8;
  DBC *dbc= ((STMT *)hstmt)->dbc;
  SQLINTEGER len= SQL_NTS;;
  uint errors= 0;

  catalog8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, catalog, &len, &errors);
  catalog_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  schema8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, schema, &len, &errors);
  schema_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  table8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, table, &len, &errors);
  table_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  type8= sqlwchar_as_sqlchar(dbc->cxn_charset_info, type, &len, &errors);
  type_len= (SQLSMALLINT)len;
  len= SQL_NTS;

  rc= MySQLTables(hstmt, catalog8, catalog_len, schema8, schema_len,
                  table8, table_len, type8, type_len);

  x_free(catalog8);
  x_free(schema8);
  x_free(table8);
  x_free(type8);

  return rc;
}


#ifdef NOT_IMPLEMENTED_YET
SQLRETURN SQL_API
SQLBrowseConnectW(SQLHDBC hdbc, SQLWCHAR *in, SQLSMALLINT in_len,
                  SQLWCHAR *out, SQLSMALLINT out_max, SQLSMALLINT *out_len)
{
  NOT_IMPLEMENTED;
}


//SQLDataSourcesW


//SQLDriversW



SQLRETURN SQL_API
SQLGetDescFieldW(SQLHDESC hdesc, SQLSMALLINT record, SQLSMALLINT field,
                 SQLPOINTER value, SQLINTEGER value_max, SQLINTEGER *value_len)
{
  NOT_IMPLEMENTED;
}


SQLRETURN SQL_API
SQLGetDescRecW(SQLHDESC hdesc, SQLSMALLINT record, SQLWCHAR *name,
               SQLSMALLINT name_max, SQLSMALLINT *name_len, SQLSMALLINT *type,
               SQLSMALLINT *subtype, SQLLEN *length, SQLSMALLINT *precision,
               SQLSMALLINT *scale, SQLSMALLINT *nullable)
{
  NOT_IMPLEMENTED;
}


SQLRETURN SQL_API
SQLProcedureColumnsW(SQLHSTMT hstmt,
                     SQLWCHAR *catalog, SQLSMALLINT catalog_len,
                     SQLWCHAR *schema, SQLSMALLINT schema_len,
                     SQLWCHAR *proc, SQLSMALLINT proc_len,
                     SQLWCHAR *column, SQLSMALLINT column_len)
{
  NOT_IMPLEMENTED;
}


SQLRETURN SQL_API
SQLSetDescFieldW(SQLHDESC hdesc, SQLSMALLINT record, SQLSMALLINT field,
                 SQLPOINTER value, SQLINTEGER value_len)
{
  NOT_IMPLEMENTED;
}


SQLRETURN SQL_API
SQLStatisticsW(SQLHSTMT hstmt,
               SQLWCHAR *catalog, SQLSMALLINT catalog_len,
               SQLWCHAR *schema, SQLSMALLINT schema_len,
               SQLWCHAR *table, SQLSMALLINT table_len,
               SQLUSMALLINT unique, SQLUSMALLINT accuracy)
{
  NOT_IMPLEMENTED;
}


SQLRETURN SQL_API
SQLTablePrivilegesW(SQLHSTMT hstmt,
                    SQLWCHAR *catalog, SQLSMALLINT catalog_len,
                    SQLWCHAR *schema, SQLSMALLINT schema_len,
                    SQLWCHAR *table, SQLSMALLINT table_len)
{
  NOT_IMPLEMENTED;
}
#endif
