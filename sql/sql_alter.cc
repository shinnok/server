/* Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
   Copyright (c) 2016, MariaDB Corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "mariadb.h"
#include "sql_parse.h"                       // check_access
#include "sql_table.h"                       // mysql_alter_table,
                                             // mysql_exchange_partition
#include "sql_alter.h"
#include "wsrep_mysqld.h"

Alter_info::Alter_info(const Alter_info &rhs, MEM_ROOT *mem_root)
  :drop_list(rhs.drop_list, mem_root),
  alter_list(rhs.alter_list, mem_root),
  key_list(rhs.key_list, mem_root),
  create_list(rhs.create_list, mem_root),
  check_constraint_list(rhs.check_constraint_list, mem_root),
  flags(rhs.flags), partition_flags(rhs.partition_flags),
  keys_onoff(rhs.keys_onoff),
  partition_names(rhs.partition_names, mem_root),
  num_parts(rhs.num_parts),
  requested_algorithm(rhs.requested_algorithm),
  requested_lock(rhs.requested_lock)
{
  /*
    Make deep copies of used objects.
    This is not a fully deep copy - clone() implementations
    of Alter_drop, Alter_column, Key, foreign_key, Key_part_spec
    do not copy string constants. At the same length the only
    reason we make a copy currently is that ALTER/CREATE TABLE
    code changes input Alter_info definitions, but string
    constants never change.
  */
  list_copy_and_replace_each_value(drop_list, mem_root);
  list_copy_and_replace_each_value(alter_list, mem_root);
  list_copy_and_replace_each_value(key_list, mem_root);
  list_copy_and_replace_each_value(create_list, mem_root);
  /* partition_names are not deeply copied currently */
}


bool Alter_info::set_requested_algorithm(const LEX_CSTRING *str)
{
  // To avoid adding new keywords to the grammar, we match strings here.
  if (!my_strcasecmp(system_charset_info, str->str, "INPLACE"))
    requested_algorithm= ALTER_TABLE_ALGORITHM_INPLACE;
  else if (!my_strcasecmp(system_charset_info, str->str, "COPY"))
    requested_algorithm= ALTER_TABLE_ALGORITHM_COPY;
  else if (!my_strcasecmp(system_charset_info, str->str, "DEFAULT"))
    requested_algorithm= ALTER_TABLE_ALGORITHM_DEFAULT;
  else if (!my_strcasecmp(system_charset_info, str->str, "NOCOPY"))
    requested_algorithm= ALTER_TABLE_ALGORITHM_NOCOPY;
  else if (!my_strcasecmp(system_charset_info, str->str, "INSTANT"))
    requested_algorithm= ALTER_TABLE_ALGORITHM_INSTANT;
  else
    return true;
  return false;
}


bool Alter_info::set_requested_lock(const LEX_CSTRING *str)
{
  // To avoid adding new keywords to the grammar, we match strings here.
  if (!my_strcasecmp(system_charset_info, str->str, "NONE"))
    requested_lock= ALTER_TABLE_LOCK_NONE;
  else if (!my_strcasecmp(system_charset_info, str->str, "SHARED"))
    requested_lock= ALTER_TABLE_LOCK_SHARED;
  else if (!my_strcasecmp(system_charset_info, str->str, "EXCLUSIVE"))
    requested_lock= ALTER_TABLE_LOCK_EXCLUSIVE;
  else if (!my_strcasecmp(system_charset_info, str->str, "DEFAULT"))
    requested_lock= ALTER_TABLE_LOCK_DEFAULT;
  else
    return true;
  return false;
}

const char* Alter_info::algorithm() const
{
  switch (requested_algorithm) {
  case ALTER_TABLE_ALGORITHM_INPLACE:
    return "ALGORITHM=INPLACE";
  case ALTER_TABLE_ALGORITHM_COPY:
    return "ALGORITHM=COPY";
  case ALTER_TABLE_ALGORITHM_DEFAULT:
    return "ALGORITHM=DEFAULT";
  case ALTER_TABLE_ALGORITHM_NOCOPY:
    return "ALGORITHM=NOCOPY";
  case ALTER_TABLE_ALGORITHM_INSTANT:
    return "ALGORITHM=INSTANT";
  default:
    return NULL;
  }

  return NULL;
}

const char* Alter_info::lock() const
{
  switch (requested_lock) {
  case ALTER_TABLE_LOCK_SHARED:
    return "LOCK=SHARED";
  case ALTER_TABLE_LOCK_NONE:
    return "LOCK=NONE";
  case ALTER_TABLE_LOCK_DEFAULT:
    return "LOCK=DEFAULT";
  case ALTER_TABLE_LOCK_EXCLUSIVE:
    return "LOCK=EXCLUSIVE";
  default:
    return NULL;
  }
  return NULL;
}

bool Alter_info::supports_algorithm(enum_alter_inplace_result result,
                                    const Alter_inplace_info &ha_alter_info)
{
  switch (result) {
  case HA_ALTER_INPLACE_EXCLUSIVE_LOCK:
  case HA_ALTER_INPLACE_SHARED_LOCK:
  case HA_ALTER_INPLACE_NO_LOCK:
  case HA_ALTER_INPLACE_INSTANT:
     return false;
  case HA_ALTER_INPLACE_COPY_NO_LOCK:
  case HA_ALTER_INPLACE_COPY_LOCK:
    if (requested_algorithm >= Alter_info::ALTER_TABLE_ALGORITHM_NOCOPY)
    {
      ha_alter_info.report_unsupported_error(algorithm(),
                                             "ALGORITHM=INPLACE");
      return true;
    }
    break;
  case HA_ALTER_INPLACE_NOCOPY_NO_LOCK:
  case HA_ALTER_INPLACE_NOCOPY_LOCK:
    if (requested_algorithm == Alter_info::ALTER_TABLE_ALGORITHM_INSTANT)
    {
      ha_alter_info.report_unsupported_error("ALGORITHM=INSTANT",
                                             "ALGORITHM=NOCOPY");
      return true;
    }
    break;
  case HA_ALTER_INPLACE_NOT_SUPPORTED:
    if (requested_algorithm >= Alter_info::ALTER_TABLE_ALGORITHM_INPLACE)
    {
      ha_alter_info.report_unsupported_error(algorithm(),
					     "ALGORITHM=COPY");
      return true;
    }
    break;
  case HA_ALTER_ERROR:
    return true;
  default:
    DBUG_ASSERT(0);
    break;
  }

  return false;
}

bool Alter_info::supports_lock(enum_alter_inplace_result result,
                               const Alter_inplace_info &ha_alter_info)
{
  switch (result) {
  case HA_ALTER_INPLACE_EXCLUSIVE_LOCK:
    // If SHARED lock and no particular algorithm was requested, use COPY.
    if (requested_lock == Alter_info::ALTER_TABLE_LOCK_SHARED &&
        requested_algorithm == Alter_info::ALTER_TABLE_ALGORITHM_DEFAULT)
    {
         return true;
    }

    if (requested_lock == Alter_info::ALTER_TABLE_LOCK_SHARED ||
        requested_lock == Alter_info::ALTER_TABLE_LOCK_NONE)
    {
      ha_alter_info.report_unsupported_error(lock(), "LOCK=EXCLUSIVE");
      return true;
    }
    break;
  case HA_ALTER_INPLACE_NO_LOCK:
  case HA_ALTER_INPLACE_INSTANT:
  case HA_ALTER_INPLACE_COPY_NO_LOCK:
  case HA_ALTER_INPLACE_NOCOPY_NO_LOCK:
    return false;
  case HA_ALTER_INPLACE_COPY_LOCK:
  case HA_ALTER_INPLACE_NOCOPY_LOCK:
  case HA_ALTER_INPLACE_NOT_SUPPORTED:
  case HA_ALTER_INPLACE_SHARED_LOCK:
    if (requested_lock == Alter_info::ALTER_TABLE_LOCK_NONE)
    {
      ha_alter_info.report_unsupported_error("LOCK=NONE", "LOCK=SHARED");
      return true;
    }
    break;
  default:
    DBUG_ASSERT(0);
    break;
  }

  return false;
}

Alter_table_ctx::Alter_table_ctx()
  : datetime_field(NULL), error_if_not_empty(false),
    tables_opened(0),
    db(null_clex_str), table_name(null_clex_str), alias(null_clex_str),
    new_db(null_clex_str), new_name(null_clex_str), new_alias(null_clex_str),
    fk_error_if_delete_row(false), fk_error_id(NULL),
    fk_error_table(NULL)
#ifdef DBUG_ASSERT_EXISTS
    , tmp_table(false)
#endif
{
}

/*
  TODO: new_name_arg changes if lower case table names.
  Should be copied or converted before call
*/

Alter_table_ctx::Alter_table_ctx(THD *thd, TABLE_LIST *table_list,
                                 uint tables_opened_arg,
                                 const LEX_CSTRING *new_db_arg,
                                 const LEX_CSTRING *new_name_arg)
  : datetime_field(NULL), error_if_not_empty(false),
    tables_opened(tables_opened_arg),
    new_db(*new_db_arg), new_name(*new_name_arg),
    fk_error_if_delete_row(false), fk_error_id(NULL),
    fk_error_table(NULL)
#ifdef DBUG_ASSERT_EXISTS
    , tmp_table(false)
#endif
{
  /*
    Assign members db, table_name, new_db and new_name
    to simplify further comparisions: we want to see if it's a RENAME
    later just by comparing the pointers, avoiding the need for strcmp.
  */
  db= table_list->db;
  table_name= table_list->table_name;
  alias= (lower_case_table_names == 2) ? table_list->alias : table_name;

  if (!new_db.str || !my_strcasecmp(table_alias_charset, new_db.str, db.str))
    new_db= db;

  if (new_name.str)
  {
    DBUG_PRINT("info", ("new_db.new_name: '%s'.'%s'", new_db.str, new_name.str));

    if (lower_case_table_names == 1) // Convert new_name/new_alias to lower
    {
      new_name.length= my_casedn_str(files_charset_info, (char*) new_name.str);
      new_alias= new_name;
    }
    else if (lower_case_table_names == 2) // Convert new_name to lower case
    {
      new_alias.str=    new_alias_buff;
      new_alias.length= new_name.length;
      strmov(new_alias_buff, new_name.str);
      new_name.length= my_casedn_str(files_charset_info, (char*) new_name.str);

    }
    else
      new_alias= new_name; // LCTN=0 => case sensitive + case preserving

    if (!is_database_changed() &&
        !my_strcasecmp(table_alias_charset, new_name.str, table_name.str))
    {
      /*
        Source and destination table names are equal:
        make is_table_renamed() more efficient.
      */
      new_alias= table_name;
      new_name= table_name;
    }
  }
  else
  {
    new_alias= alias;
    new_name= table_name;
  }

  tmp_name.str= tmp_name_buff;
  tmp_name.length= my_snprintf(tmp_name_buff, sizeof(tmp_name_buff), "%s-%lx_%llx",
                               tmp_file_prefix, current_pid, thd->thread_id);
  /* Safety fix for InnoDB */
  if (lower_case_table_names)
    tmp_name.length= my_casedn_str(files_charset_info, tmp_name_buff);

  if (table_list->table->s->tmp_table == NO_TMP_TABLE)
  {
    build_table_filename(path, sizeof(path) - 1, db.str, table_name.str, "", 0);

    build_table_filename(new_path, sizeof(new_path) - 1, new_db.str, new_name.str, "", 0);

    build_table_filename(new_filename, sizeof(new_filename) - 1,
                         new_db.str, new_name.str, reg_ext, 0);

    build_table_filename(tmp_path, sizeof(tmp_path) - 1, new_db.str, tmp_name.str, "",
                         FN_IS_TMP);
  }
  else
  {
    /*
      We are not filling path, new_path and new_filename members if
      we are altering temporary table as these members are not used in
      this case. This fact is enforced with assert.
    */
    build_tmptable_filename(thd, tmp_path, sizeof(tmp_path));
#ifdef DBUG_ASSERT_EXISTS
    tmp_table= true;
#endif
  }
}


bool Sql_cmd_alter_table::execute(THD *thd)
{
  LEX *lex= thd->lex;
  /* first SELECT_LEX (have special meaning for many of non-SELECTcommands) */
  SELECT_LEX *select_lex= &lex->select_lex;
  /* first table of first SELECT_LEX */
  TABLE_LIST *first_table= (TABLE_LIST*) select_lex->table_list.first;
  /*
    Code in mysql_alter_table() may modify its HA_CREATE_INFO argument,
    so we have to use a copy of this structure to make execution
    prepared statement- safe. A shallow copy is enough as no memory
    referenced from this structure will be modified.
    @todo move these into constructor...
  */
  HA_CREATE_INFO create_info(lex->create_info);
  Alter_info alter_info(lex->alter_info, thd->mem_root);
  ulong priv=0;
  ulong priv_needed= ALTER_ACL;
  bool result;

  DBUG_ENTER("Sql_cmd_alter_table::execute");

  if (thd->is_fatal_error) /* out of memory creating a copy of alter_info */
    DBUG_RETURN(TRUE);
  /*
    We also require DROP priv for ALTER TABLE ... DROP PARTITION, as well
    as for RENAME TO, as being done by SQLCOM_RENAME_TABLE
  */
  if ((alter_info.partition_flags & ALTER_PARTITION_DROP) ||
      (alter_info.flags & ALTER_RENAME))
    priv_needed|= DROP_ACL;

  /* Must be set in the parser */
  DBUG_ASSERT(select_lex->db.str);
  DBUG_ASSERT(!(alter_info.partition_flags & ALTER_PARTITION_EXCHANGE));
  DBUG_ASSERT(!(alter_info.partition_flags & ALTER_PARTITION_ADMIN));
  if (check_access(thd, priv_needed, first_table->db.str,
                   &first_table->grant.privilege,
                   &first_table->grant.m_internal,
                   0, 0) ||
      check_access(thd, INSERT_ACL | CREATE_ACL, select_lex->db.str,
                   &priv,
                   NULL, /* Don't use first_tab->grant with sel_lex->db */
                   0, 0))
    DBUG_RETURN(TRUE);                  /* purecov: inspected */

  /* If it is a merge table, check privileges for merge children. */
  if (create_info.merge_list.first)
  {
    /*
      The user must have (SELECT_ACL | UPDATE_ACL | DELETE_ACL) on the
      underlying base tables, even if there are temporary tables with the same
      names.

      From user's point of view, it might look as if the user must have these
      privileges on temporary tables to create a merge table over them. This is
      one of two cases when a set of privileges is required for operations on
      temporary tables (see also CREATE TABLE).

      The reason for this behavior stems from the following facts:

        - For merge tables, the underlying table privileges are checked only
          at CREATE TABLE / ALTER TABLE time.

          In other words, once a merge table is created, the privileges of
          the underlying tables can be revoked, but the user will still have
          access to the merge table (provided that the user has privileges on
          the merge table itself). 

        - Temporary tables shadow base tables.

          I.e. there might be temporary and base tables with the same name, and
          the temporary table takes the precedence in all operations.

        - For temporary MERGE tables we do not track if their child tables are
          base or temporary. As result we can't guarantee that privilege check
          which was done in presence of temporary child will stay relevant later
          as this temporary table might be removed.

      If SELECT_ACL | UPDATE_ACL | DELETE_ACL privileges were not checked for
      the underlying *base* tables, it would create a security breach as in
      Bug#12771903.
    */

    if (check_table_access(thd, SELECT_ACL | UPDATE_ACL | DELETE_ACL,
                           create_info.merge_list.first, FALSE, UINT_MAX, FALSE))
      DBUG_RETURN(TRUE);
  }

  if (check_grant(thd, priv_needed, first_table, FALSE, UINT_MAX, FALSE))
    DBUG_RETURN(TRUE);                  /* purecov: inspected */

  if (lex->name.str && !test_all_bits(priv, INSERT_ACL | CREATE_ACL))
  {
    // Rename of table
    TABLE_LIST tmp_table;
    memset(&tmp_table, 0, sizeof(tmp_table));
    tmp_table.table_name= lex->name;
    tmp_table.db= select_lex->db;
    tmp_table.grant.privilege= priv;
    if (check_grant(thd, INSERT_ACL | CREATE_ACL, &tmp_table, FALSE,
                    UINT_MAX, FALSE))
      DBUG_RETURN(TRUE);                  /* purecov: inspected */
  }

  /* Don't yet allow changing of symlinks with ALTER TABLE */
  if (create_info.data_file_name)
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        WARN_OPTION_IGNORED, ER_THD(thd, WARN_OPTION_IGNORED),
                        "DATA DIRECTORY");
  if (create_info.index_file_name)
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        WARN_OPTION_IGNORED, ER_THD(thd, WARN_OPTION_IGNORED),
                        "INDEX DIRECTORY");
  create_info.data_file_name= create_info.index_file_name= NULL;

  thd->prepare_logs_for_admin_command();

#ifdef WITH_WSREP
  if ((!thd->is_current_stmt_binlog_format_row() ||
       !thd->find_temporary_table(first_table)))
  {
    WSREP_TO_ISOLATION_BEGIN(((lex->name.str) ? select_lex->db.str : NULL),
                             ((lex->name.str) ? lex->name.str : NULL),
                             first_table);
  }
#endif /* WITH_WSREP */

  result= mysql_alter_table(thd, &select_lex->db, &lex->name,
                            &create_info,
                            first_table,
                            &alter_info,
                            select_lex->order_list.elements,
                            select_lex->order_list.first,
                            lex->ignore);

  DBUG_RETURN(result);

#ifdef WITH_WSREP
error:
  WSREP_WARN("ALTER TABLE isolation failure");
  DBUG_RETURN(TRUE);
#endif /* WITH_WSREP */
}

bool Sql_cmd_discard_import_tablespace::execute(THD *thd)
{
  /* first SELECT_LEX (have special meaning for many of non-SELECTcommands) */
  SELECT_LEX *select_lex= &thd->lex->select_lex;
  /* first table of first SELECT_LEX */
  TABLE_LIST *table_list= (TABLE_LIST*) select_lex->table_list.first;

  if (check_access(thd, ALTER_ACL, table_list->db.str,
                   &table_list->grant.privilege,
                   &table_list->grant.m_internal,
                   0, 0))
    return true;

  if (check_grant(thd, ALTER_ACL, table_list, false, UINT_MAX, false))
    return true;

  thd->prepare_logs_for_admin_command();

  /*
    Check if we attempt to alter mysql.slow_log or
    mysql.general_log table and return an error if
    it is the case.
    TODO: this design is obsolete and will be removed.
  */
  if (check_if_log_table(table_list, TRUE, "ALTER"))
    return true;

  return
    mysql_discard_or_import_tablespace(thd, table_list,
                                       m_tablespace_op == DISCARD_TABLESPACE);
}
