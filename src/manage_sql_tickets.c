/* GVM
 * $Id$
 * Description: GVM management layer SQL: Tickets.
 *
 * Authors:
 * Matthew Mundell <matthew.mundell@greenbone.net>
 *
 * Copyright:
 * Copyright (C) 2018 Greenbone Networks GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file manage_sql_tickets.c
 * @brief GVM management layer: Ticket SQL
 *
 * The Ticket SQL for the GVM management layer.
 */

#include "manage_tickets.h"
#include "manage_acl.h"
#include "manage_sql_tickets.h"
#include "manage_sql.h"
#include "sql.h"

#include <string.h>

#undef G_LOG_DOMAIN
/**
 * @brief GLib log domain.
 */
#define G_LOG_DOMAIN "md manage"

/**
 * @brief Filter columns for ticket iterator.
 */
#define TICKET_ITERATOR_FILTER_COLUMNS                                         \
 { GET_ITERATOR_FILTER_COLUMNS, "host",                                        \
   NULL }

/**
 * @brief Ticket iterator columns.
 */
#define TICKET_ITERATOR_COLUMNS                             \
 {                                                          \
   GET_ITERATOR_COLUMNS (tickets),                          \
   { "host", NULL, KEYWORD_TYPE_STRING },                   \
   { NULL, NULL, KEYWORD_TYPE_UNKNOWN }                     \
 }

/**
 * @brief Ticket iterator columns for trash case.
 */
#define TICKET_ITERATOR_TRASH_COLUMNS                               \
 {                                                                  \
   GET_ITERATOR_COLUMNS (tickets_trash),                            \
   { "host", NULL, KEYWORD_TYPE_STRING },                           \
   { NULL, NULL, KEYWORD_TYPE_UNKNOWN }                             \
 }

/**
 * @brief Count number of tickets.
 *
 * @param[in]  get  GET params.
 *
 * @return Total number of tickets in filtered set.
 */
int
ticket_count (const get_data_t *get)
{
  static const char *extra_columns[] = TICKET_ITERATOR_FILTER_COLUMNS;
  static column_t columns[] = TICKET_ITERATOR_COLUMNS;
  static column_t trash_columns[] = TICKET_ITERATOR_TRASH_COLUMNS;

  return count ("ticket", get, columns, trash_columns, extra_columns, 0, 0, 0,
                TRUE);
}

/**
 * @brief Initialise a ticket iterator.
 *
 * @param[in]  iterator    Iterator.
 * @param[in]  get         GET data.
 *
 * @return 0 success, 1 failed to find ticket, 2 failed to find filter,
 *         -1 error.
 */
int
init_ticket_iterator (iterator_t *iterator, const get_data_t *get)
{
  static const char *filter_columns[] = TICKET_ITERATOR_FILTER_COLUMNS;
  static column_t columns[] = TICKET_ITERATOR_COLUMNS;
  static column_t trash_columns[] = TICKET_ITERATOR_TRASH_COLUMNS;

  return init_get_iterator (iterator,
                            "ticket",
                            get,
                            columns,
                            trash_columns,
                            filter_columns,
                            0,
                            NULL,
                            NULL,
                            TRUE);
}

/**
 * @brief Get the host from a ticket iterator.
 *
 * @param[in]  iterator  Iterator.
 *
 * @return Host of the ticket or NULL if iteration is complete.
 */
DEF_ACCESS (ticket_iterator_host, GET_ITERATOR_COLUMN_COUNT);

/**
 * @brief Return whether a ticket is in use.
 *
 * @param[in]  ticket  Ticket.
 *
 * @return 1 if in use, else 0.
 */
int
ticket_in_use (ticket_t ticket)
{
  return 0;
}

/**
 * @brief Return whether a trashcan ticket is in use.
 *
 * @param[in]  ticket  Ticket.
 *
 * @return 1 if in use, else 0.
 */
int
trash_ticket_in_use (ticket_t ticket)
{
  return 0;
}

/**
 * @brief Return whether a ticket is writable.
 *
 * @param[in]  ticket  Ticket.
 *
 * @return 1 if writable, else 0.
 */
int
ticket_writable (ticket_t ticket)
{
  return 1;
}

/**
 * @brief Return whether a trashcan ticket is writable.
 *
 * @param[in]  ticket  Ticket.
 *
 * @return 1 if writable, else 0.
 */
int
trash_ticket_writable (ticket_t ticket)
{
  return trash_ticket_in_use (ticket) == 0;
}

/**
 * @brief Delete a ticket.
 *
 * @param[in]  ticket_id  UUID of ticket.
 * @param[in]  ultimate   Whether to remove entirely, or to trashcan.
 *
 * @return 0 success, 1 fail because ticket is in use, 2 failed to find ticket,
 *         3 predefined ticket, 99 permission denied, -1 error.
 */
int
delete_ticket (const char *ticket_id, int ultimate)
{
  ticket_t ticket = 0;

  sql_begin_immediate ();

  if (acl_user_may ("delete_ticket") == 0)
    {
      sql_rollback ();
      return 99;
    }

  if (find_resource_with_permission ("ticket", ticket_id, &ticket,
                                     "delete_ticket", 0))
    {
      sql_rollback ();
      return -1;
    }

  if (ticket == 0)
    {
      if (find_trash ("ticket", ticket_id, &ticket))
        {
          sql_rollback ();
          return -1;
        }
      if (ticket == 0)
        {
          sql_rollback ();
          return 2;
        }
      if (ultimate == 0)
        {
          /* It's already in the trashcan. */
          sql_commit ();
          return 0;
        }

      tags_remove_resource ("ticket", ticket, LOCATION_TRASH);

      sql ("DELETE FROM tickets_trash WHERE id = %llu;", ticket);
      sql_commit ();
      return 0;
    }

  if (ultimate == 0)
    {
      ticket_t trash_ticket;

      sql ("INSERT INTO tickets_trash"
           " (uuid, owner, name, comment, task, report, severity, host,"
           "  location, solution_type, assigned_to, status, open_time,"
           "  solved_time, solved_comment, confirmed_time, confirmed_result,"
           "  closed_time, closed_rationale, orphaned_time, creation_time,"
           "  modification_time)"
           " SELECT uuid, owner, name, comment, task, report, severity, host,"
           "        location, solution_type, assigned_to, status, open_time,"
           "        solved_time, solved_comment, confirmed_time,"
           "        confirmed_result, closed_time, closed_rationale,"
           "        orphaned_time, creation_time, modification_time"
           " FROM tickets WHERE id = %llu;",
           ticket);

      trash_ticket = sql_last_insert_id ();

      permissions_set_locations ("ticket", ticket, trash_ticket,
                                 LOCATION_TRASH);
      tags_set_locations ("ticket", ticket, trash_ticket,
                          LOCATION_TRASH);
    }
  else
    {
      permissions_set_orphans ("ticket", ticket, LOCATION_TABLE);
      tags_remove_resource ("ticket", ticket, LOCATION_TABLE);
    }

  sql ("DELETE FROM tickets WHERE id = %llu;", ticket);

  sql_commit ();
  return 0;
}

/**
 * @brief Try restore a ticket.
 *
 * Ends transaction for caller before exiting.
 *
 * @param[in]  ticket_id  UUID of resource.
 *
 * @return 0 success, 1 fail because ticket is in use, 2 failed to find ticket,
 *         3 predefined ticket, -1 error.
 */
int
restore_ticket (const char *ticket_id)
{
  ticket_t ticket;

  if (find_trash ("ticket", ticket_id, &ticket))
    {
      sql_rollback ();
      return -1;
    }

  if (ticket)
    {
      if (sql_int ("SELECT count(*) FROM tickets"
                   " WHERE name ="
                   " (SELECT name FROM tickets_trash WHERE id = %llu)"
                   " AND " ACL_USER_OWNS () ";",
                   ticket,
                   current_credentials.uuid))
        {
          sql_rollback ();
          return 3;
        }

      sql ("INSERT INTO tickets"
           " (uuid, owner, name, comment, task, report, severity, host,"
           "  location, solution_type, assigned_to, status, open_time,"
           "  solved_time, solved_comment, confirmed_time, confirmed_result,"
           "  closed_time, closed_rationale, orphaned_time, creation_time,"
           "  modification_time)"
           " SELECT uuid, owner, name, comment, task, report, severity, host,"
           "        location, solution_type, assigned_to, status, open_time,"
           "        solved_time, solved_comment, confirmed_time,"
           "        confirmed_result, closed_time, closed_rationale,"
           "        orphaned_time, creation_time, modification_time"
           " FROM tickets_trash WHERE id = %llu;",
           ticket);

      permissions_set_locations ("ticket", ticket,
                                 sql_last_insert_id (),
                                 LOCATION_TABLE);
      tags_set_locations ("ticket", ticket,
                          sql_last_insert_id (),
                          LOCATION_TABLE);

      sql ("DELETE FROM tickets_trash WHERE id = %llu;", ticket);
      sql_commit ();
      return 0;
    }

  return 2;
}

/**
 * @brief Create a ticket.
 *
 * @param[in]   name            Name of ticket.
 * @param[in]   comment         Comment on ticket.
 * @param[out]  ticket          Created ticket.
 *
 * @return 0 success, 1 ticket exists already, 2 error in host specification,
 *         99 permission denied, -1 error.
 */
int
create_ticket (const char *name, const char *comment,
               ticket_t *ticket)
{
  gchar *quoted_name, *quoted_comment;
  ticket_t new_ticket;

  assert (current_credentials.uuid);

  sql_begin_immediate ();

  if (acl_user_may ("create_ticket") == 0)
    {
      sql_rollback ();
      return 99;
    }

  if (resource_with_name_exists (name, "ticket", 0))
    {
      sql_rollback ();
      return 1;
    }

  quoted_name = sql_quote (name);

  if (comment)
    quoted_comment = sql_quote (comment);
  else
    quoted_comment = sql_quote ("");

  sql ("INSERT INTO tickets"
       " (uuid, name, owner, comment,"
       "  creation_time, modification_time)"
       " VALUES (make_uuid (), '%s',"
       " (SELECT id FROM users WHERE users.uuid = '%s'),"
       " '%s',"
       " m_now (), m_now ());",
        quoted_name, current_credentials.uuid,
        quoted_comment);

  new_ticket = sql_last_insert_id ();
  if (ticket)
    *ticket = new_ticket;

  g_free (quoted_comment);
  g_free (quoted_name);

  sql_commit ();

  return 0;
}

/**
 * @brief Create a ticket from an existing ticket.
 *
 * @param[in]  name        Name of new ticket.  NULL to copy from existing.
 * @param[in]  comment     Comment on new ticket.  NULL to copy from existing.
 * @param[in]  ticket_id   UUID of existing ticket.
 * @param[out] new_ticket  New ticket.
 *
 * @return 0 success, 1 ticket exists already, 2 failed to find existing
 *         ticket, 99 permission denied, -1 error.
 */
int
copy_ticket (const char *name, const char *comment, const char *ticket_id,
             ticket_t *new_ticket)
{
  int ret;
  ticket_t old_ticket;

  assert (new_ticket);

  ret = copy_resource ("ticket", name, comment, ticket_id,
                       "task, report, severity, host, location, solution_type,"
                       " assigned_to, status, open_time, solved_time,"
                       " solved_comment, confirmed_time, confirmed_result,"
                       " closed_time, closed_rationale, orphaned_time",
                       1, new_ticket, &old_ticket);
  if (ret)
    return ret;

  return 0;
}

/**
 * @brief Return the UUID of a ticket.
 *
 * @param[in]  ticket  Ticket.
 *
 * @return Newly allocated UUID if available, else NULL.
 */
char*
ticket_uuid (ticket_t ticket)
{
  return sql_string ("SELECT uuid FROM tickets WHERE id = %llu;",
                     ticket);
}

/**
 * @brief Modify a ticket.
 *
 * @param[in]   ticket_id       UUID of ticket.
 * @param[in]   name            Name of ticket.
 * @param[in]   comment         Comment on ticket.
 *
 * @return 0 success, 1 ticket exists already, 2 failed to find ticket,
 *         3 zero length name, 99 permission denied, -1 error.
 */
int
modify_ticket (const char *ticket_id, const char *name, const char *comment)
{
  ticket_t ticket;

  assert (ticket_id);

  sql_begin_immediate ();

  assert (current_credentials.uuid);

  if (acl_user_may ("modify_ticket") == 0)
    {
      sql_rollback ();
      return 99;
    }

  ticket = 0;
  if (find_resource_with_permission ("ticket", ticket_id, &ticket,
                                     "modify_ticket", 0))
    {
      sql_rollback ();
      return -1;
    }

  if (ticket == 0)
    {
      sql_rollback ();
      return 9;
    }

  if (name)
    {
      gchar *quoted_name;

      if (strlen (name) == 0)
        {
          sql_rollback ();
          return 11;
        }
      if (resource_with_name_exists (name, "ticket", ticket))
        {
          sql_rollback ();
          return 1;
        }

      quoted_name = sql_quote (name);
      sql ("UPDATE tickets SET"
           " name = '%s',"
           " modification_time = m_now ()"
           " WHERE id = %llu;",
           quoted_name,
           ticket);
      g_free (quoted_name);
    }

  if (comment)
    {
      gchar *quoted_comment;

      quoted_comment = sql_quote (comment);
      sql ("UPDATE tickets SET"
           " comment = '%s',"
           " modification_time = m_now ()"
           " WHERE id = %llu;",
           quoted_comment,
           ticket);
      g_free (quoted_comment);
    }

  sql_commit ();

  return 0;
}
