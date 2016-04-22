/*
 * Navit networking services
 *
 * (c) Alexander Vdolainen 2016 <avdolainen@zoho.com>
 *
 * navit is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * navit is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.";
 *
 */

/**
 * This backend use two tables to manage the database and store data:
 * 1. Objects stored information (psqlbe_objects)
 * sid - serial id of the object
 * oname[256] - object name
 * table[515] - table used to store object of such kind
 * attr(8bit) - attribute (1bit if set - empty table (no data definion saved), 2bit set - data definion already exists)
 * 2. Objects items data types
 * sid - id of the object connected to the item
 * idx - index of the item
 * type - type of the item
 * len - len of the item
 *
 * table creation -
 Creation of the table 

 CREATE TABLE rrr
 (
 eeeid bigserial NOT NULL, 
 attr smallint, 
 CONSTRAINT eeeid PRIMARY KEY (eeeid)
 ) 
 WITH (
 OIDS = TRUE
 )
 ;
 ALTER TABLE rrr OWNER TO etest;
 
 
 test for tables
 SELECT table_name from information_schema.tables where table_schema = 'public';
 
 insert
 INSERT INTO rrr (attr) values (0) returning oid;
 
 select by oid
 SELECT * from rrr where oid = 16394;
 
 template1=# CREATE USER tom WITH PASSWORD 'myPassword';
 
 Step #5: Add a database called jerry
 Type the following command (you need to type command highlighted with red color):
 template1=# CREATE DATABASE jerry;
 
 Now grant all privileges on database
 template1=# GRANT ALL PRIVILEGES ON DATABASE jerry to tom;
 
*/

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>

#include <tdata/usrtc.h>
#include <tdata/list.h>
#include <ydaemon/ydaemon.h>
#define DMUX_USE  1
#include <ydaemon/dataobject.h>

#include <libpq-fe.h>

#define MOD_PREFIX  psqlbe

/* limits */
#define __MAX_DATABASES  32
#define __DBMAXTNAME     64

/* name defines */
#define __BEPREFIX   "psqlbe"
#define __NUMPREFIX  "100"
#define __DBSCHEMA   "public"
/* backend tables names */
#define __DBOBJECTSTABLE      "psqlbe_objects"
#define __DBDESCRIPTIONTABLE  "psqlbe_objectsdesc"

static long __cmp_cstr(const void *a, const void *b)
{
  return (long)strcmp((const char *)a, (const char *)b);
}

struct psqldb {
  PGconn *dbc;
  char *connection_name;
  char *hostname, *dbname;
  char *login, *passwd;
  int attr;
  usrtc_node_t node;
};

#define DBOEEMPTY     (1 << 1)
#define DBOEALREADY   (1 << 2)
#define DBOCONNECTED  (1 << 3)
/* table related */
#define DBOOBJECTSEXISTS      (1 << 4)
#define DBODESCRIPTIONEXISTS  (1 << 5)

struct psqlbe_priv {
  struct psqldb *db;
  char *tablename;
  int attr;
};

enum {
  __DBHOST = 0,
  __DBNAME,
  __DBLOGIN,
  __DBPASSWD,
};

static int __connect_dbpg(struct psqldb *db)
{
  if(db->attr & DBOCONNECTED) return 0;
  if(db->dbc) {
  __test:
    if(PQstatus(db->dbc) == CONNECTION_BAD) return EIO;
    else db->attr |= DBOCONNECTED;
    return 0;
  }

  db->dbc = PQsetdbLogin(db->hostname, NULL, NULL, NULL, db->dbname,
                         db->login, db->passwd);
  goto __test;
}

static int __check_objects_table(struct psqldb *db)
{
  char *sql;
  PGresult *res;
  int r = 0;

  if(db->attr & DBOOBJECTSEXISTS) return 0;

  if(!(sql = malloc(sizeof(char)*2048))) return ENOMEM;

  /* form an SQL query */
  snprintf(sql, 2048, "select table_name from information_schema.tables where table_schema = '%s' and table_name = '%s';",
           __DBSCHEMA, __DBOBJECTSTABLE);

  res = PQexec(db->dbc, sql);
  if(!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
    r = EIO;
    goto __fail;
  }
  if(PQntuples(res) != 1) { /* doesn't exists */
    PQclear(res);

    /* form a query to create the required table */
    snprintf(sql, 2048, "CREATE TABLE %s (otid bigserial NOT NULL, tname character varying (%d), attr smallint, CONSTRAINT otid PRIMARY KEY (otid) )"
             "WITH ( OIDS = TRUE ) ; ALTER TABLE %s OWNER TO %s;", __DBOBJECTSTABLE, __DBMAXTNAME, __DBOBJECTSTABLE, db->login);
    res = PQexec(db->dbc, sql);
    if(!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
      r = EIO;
      goto __fail;
    }
  }

  /* mark exists */
  db->attr |= DBOOBJECTSEXISTS;

 __fail:
  if(sql) free(sql);
  if(res) PQclear(res);

  return r;
}

static int __check_objectdesc_table(struct psqldb *db)
{
  char *sql;
  PGresult *res;
  int r = 0;

  if(db->attr & DBODESCRIPTIONEXISTS) return 0;

  if(!(sql = malloc(sizeof(char)*2048))) return ENOMEM;

  /* form an SQL query */
  snprintf(sql, 2048, "select table_name from information_schema.tables where table_schema = '%s' and table_name = '%s';",
           __DBSCHEMA, __DBDESCRIPTIONTABLE);

  res = PQexec(db->dbc, sql);
  if(!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
    r = EIO;
    goto __fail;
  }
  if(PQntuples(res) != 1) { /* doesn't exists */
    PQclear(res);

    /* form a query to create the required table */
    snprintf(sql, 2048, "CREATE TABLE %s (otid bigint NOT NULL, name character varying (%d), "
             "idx smallint, type smallint, length integer )"
             "WITH ( OIDS = TRUE ) ; ALTER TABLE %s OWNER TO %s;", __DBDESCRIPTIONTABLE, MAX_ITEM_NAME,
             __DBDESCRIPTIONTABLE, db->login);
    res = PQexec(db->dbc, sql);
    if(!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
      r = EIO;
      goto __fail;
    }
  }

  /* mark exists */
  db->attr |= DBODESCRIPTIONEXISTS;

 __fail:
  if(sql) free(sql);
  if(res) PQclear(res);

  return r;
}

static int __get_objecttable(struct psqldb *db, const char *name, char **tname, dataobject_t *dobj)
{
  char *sql, *tbuf;
  PGresult *res;
  char *tblname;
  dataobject_t *eobj = NULL;
  list_node_t *iter, *siter;
  dataobject_item_t *ditem;
  uint64_t otid;
  char pgtype[64];
  int attr = 0;
  int r = 0, i;

  if(!name) return EINVAL;
  if(!(sql = malloc(sizeof(char)*2048))) return ENOMEM;

  /* form a query */
  snprintf(sql, 2048, "select otid, attr from %s where tname = '%s';", __DBOBJECTSTABLE, name);
  res = PQexec(db->dbc, sql);
  if(!res || PQresultStatus(res) != PGRES_TUPLES_OK) { r = EIO; goto __fail; }
  if(!PQntuples(res)) {
    PQclear(res);

    /* form a query */
    attr |= DBOEEMPTY;
    snprintf(sql, 2048, "INSERT INTO %s (tname, attr) values ('%s', %d) returning otid;",
             __DBOBJECTSTABLE, name, attr);
    res = PQexec(db->dbc, sql);
    if(!res || PQresultStatus(res) != PGRES_TUPLES_OK) { r = EIO; goto __fail; }
  } else if(PQntuples(res) > 1) {
    r = EEXIST; /* somewhat happend and this is incorrect */
    goto __fail;
  } else attr = atoi(PQgetvalue(res, 0, 1));

  /* get otid */
  otid = atoll(PQgetvalue(res, 0, 0));

  if(!(tblname = malloc(sizeof(char)*64))) { r = ENOMEM; goto __fail; }

  snprintf(tblname, sizeof(char)*64, "%s_%s_%lu", __BEPREFIX, __NUMPREFIX, otid);
  /* free willy ! */
  PQclear(res);

  /* now we need to check up data description within database */
  if(attr & DBOEEMPTY) {
  __create_description:
    /* we create a description */
    snprintf(sql, 2048, "BEGIN;\n");
    tbuf = sql + strlen(sql);
    /* now we need to add it all */
    list_for_each_safe(&dobj->description, iter, siter) {
      ditem = container_of(iter, dataobject_item_t, node);
      snprintf(tbuf, 2048 - strlen(sql),
               "insert into %s (otid, name, idx, type, length) values (%lu, '%s', %d, %d, %lu);\n",
               __DBDESCRIPTIONTABLE, otid, ditem->name, ditem->id, ditem->type, ditem->len);
      tbuf = sql + strlen(sql);
    }
    snprintf(tbuf, 2048 - strlen(sql), "COMMIT;\n");
    res = PQexec(db->dbc, sql);
    if(!res || PQresultStatus(res) != PGRES_COMMAND_OK) { free(tblname); r = EIO; goto __fail; }
    /* update not empty */
    PQclear(res);
    attr &= ~DBOEEMPTY;
    snprintf(sql, 2048, "update %s set attr = %d where otid = %lu;", __DBOBJECTSTABLE, attr, otid);
    res = PQexec(db->dbc, sql);
    if(!res || PQresultStatus(res) != PGRES_COMMAND_OK) { free(tblname); r = EIO; goto __fail; }
  } else {
    /* now we should to test existing data description */
    snprintf(sql, 2048, "select idx, type, length, name from %s where otid = %lu order by idx;",
             __DBDESCRIPTIONTABLE, otid);
    res = PQexec(db->dbc, sql);
    if(!res || PQresultStatus(res) != PGRES_TUPLES_OK) { printf("sql = %s\n", sql); free(tblname); r = EIO; goto __fail; }

    if(!PQntuples(res)) /* something goes wrong, might be program unexpected interruption ? */
      { PQclear(res); goto __create_description; }
    else {
      if(!(eobj = dotr_create(name))) { free(tblname); r = ENOMEM; goto __fail; }
      /* fill dataobject from description table */
      for(i = 0; i < PQntuples(res); i++) {
        if(dotr_set_item(eobj, atoi(PQgetvalue(res, i, 1)), atoi(PQgetvalue(res, i, 2)),
                         (const char *)PQgetvalue(res, i, 3))) {
          free(tblname);
          r = ENOMEM;
          goto __fail;
        }
      }
      /* now we need to check it */
      if(dtocmp(eobj, dobj)) { free(tblname); r = EINVAL; goto __fail; }
    }
  }

  /* check and create table itself */
  PQclear(res);
  snprintf(sql, 2048, "select table_name from information_schema.tables where "
           "table_schema = '%s' and table_name = '%s';",
           __DBSCHEMA, tblname);
  res = PQexec(db->dbc, sql);
  if(!res || PQresultStatus(res) != PGRES_TUPLES_OK) { free(tblname); r = EIO; goto __fail; }
  if(!PQntuples(res)) {
    /* create a table */
    snprintf(sql, 2048, "create table %s (", tblname);
    tbuf = sql + strlen(sql);
    list_for_each_safe(&dobj->description, iter, siter) {
      ditem = container_of(iter, dataobject_item_t, node);
      if(ditem->id) {
        snprintf(tbuf, 2048 - strlen(sql), ", ");
        tbuf = sql + strlen(sql);
      }
      /* convert ydaemon data type to pgsql */
      switch(ditem->type) {
      case U8:
      case S8:
      case U16:
      case S16:
        if(ditem->len > tltable[ditem->type].len)
          snprintf(pgtype, 64, "integer[]");
        else snprintf(pgtype, 64, "integer");
        break;
      case U32:
      case S32:
        if(ditem->len > tltable[ditem->type].len)
          snprintf(pgtype, 64, "integer[]");
        else snprintf(pgtype, 64, "integer");
        break;
      case U64:
      case S64:
        if(ditem->len > tltable[ditem->type].len)
          snprintf(pgtype, 64, "bigint[]");
        else snprintf(pgtype, 64, "bigint");
        break;
      case CSTR:
      case TBLOB:
        snprintf(pgtype, 64, "character varying (%lu)", ditem->len);
        break;
      case TACRT:
        snprintf(pgtype, 64, "integer[]");
        break;
      default:
        free(tblname);
        r = EINVAL; goto __fail;
        break;
      }

      snprintf(tbuf, 2048 - strlen(sql), "i%d %s", ditem->id, pgtype);
      tbuf = sql + strlen(sql);
    }
    /* finish query */
    snprintf(tbuf, 2048 - strlen(sql), " ) WITH (OIDS = TRUE ) ; ALTER TABLE %s OWNER TO %s;",
             tblname, db->login);
    PQclear(res); /* clear result */
    res = PQexec(db->dbc, sql);
    if(!res || PQresultStatus(res) != PGRES_COMMAND_OK) { free(tblname); r = EIO; goto __fail; }
  }

  /* guess all is fine */
  *tname = tblname;

 __fail:
  if(eobj) dotr_destroy(eobj);
  if(res) PQclear(res);
  if(sql) free(sql);
  return r;
}

static struct psqldb *__alloc_insert_db(usrtc_t *dbtree, const char *name)
{
  struct psqldb *db = NULL;
  char *connection_name = NULL;

  if(!name || !dbtree) return NULL;

  if(usrtc_count(dbtree) >= __MAX_DATABASES) return NULL;

  if(!(connection_name= strdup(name))) return NULL;
  else if(!(db = malloc(sizeof(struct psqldb)))) return NULL;

  memset(db, 0, sizeof(struct psqldb));
  db->connection_name = connection_name;
  usrtc_node_init(&db->node, db);

  /* insert */
  usrtc_insert(dbtree, &db->node, (void *)db->connection_name);

  return db;
}

static int __set_dbvariable(struct psqldb *db, const char *var, int varset)
{
  char *nval, *eval = NULL;

  if(!db || !var) return EINVAL;

  if(varset > __DBPASSWD || varset < __DBHOST) return EINVAL;
  else if(!(nval = strdup(var))) return ENOMEM;

  switch(varset) {
  case __DBHOST:
    eval = db->hostname;
    db->hostname = nval;
    break;
  case __DBNAME:
    eval = db->dbname;
    db->dbname = nval;
    break;
  case __DBLOGIN:
    eval = db->login;
    db->login = nval;
    break;
  case __DBPASSWD:
    eval = db->passwd;
    db->passwd = nval;
    break;
  default: break;
  }

  if(eval) free(eval);

  return 0;
}

/* a few globals */
static obj_store_t *ostore = NULL;
static usrtc_t *dbtree = NULL;
static struct be_ops psqlbe_ops;

/* syntax defines */
/* Syntax (psqlbe-db-add <dbname>) */
#define PSQLBESYN_ADDDB         "psqlbe-db-add"
#define PSQLBESYN_SETDB_HOST    "psqlbe-db-set-host"
#define PSQLBESYN_SETDB_NAME    "psqlbe-db-set-name"
#define PSQLBESYN_SETDB_USER    "psqlbe-db-set-user"
#define PSQLBESYN_SETDB_PASSWD  "psqlbe-db-set-password"

/* syntax functions */
static scret_t __psqlbe_set_db(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int idx;
  int r = EINVAL, vs = -1;
  sexp_t *isx;
  char *name = NULL, *value;
  ydc_conf_val_t *rval;
  usrtc_node_t *unode;
  struct psqldb *db = NULL;
  scret_t rets;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST) goto __fini;

    switch(idx) {
    case 0:
      if(!strcmp(isx->val, PSQLBESYN_SETDB_HOST)) vs = __DBHOST;
      else if(!strcmp(isx->val, PSQLBESYN_SETDB_NAME)) vs = __DBNAME;
      else if(!strcmp(isx->val, PSQLBESYN_SETDB_USER)) vs = __DBLOGIN;
      else if(!strcmp(isx->val, PSQLBESYN_SETDB_PASSWD)) vs = __DBPASSWD;
      else goto __fini;
      break;
    case 1:
      if(isx->aty == SEXP_DQUOTE) name = isx->val;
      else if(isx->aty == SEXP_BASIC && strchr(isx->val, '/')) {
        if((r = ydc_conf_get_val(ctx->values, (const char *)isx->val, &rval)))          goto __fini;
        else r = EINVAL; /* reset error code */

        if(rval->type != STRING) goto __fini;

        name = (char *)rval->value;
      } else goto __fini;

      /* find it */
      if(!(unode = usrtc_lookup((usrtc_t *)priv, (const void *)name))) {r = ENOENT; goto __fini;}
      else db = (struct psqldb *)usrtc_node_getdata(unode);

      if(!db) {r = ENOENT; goto __fini;}
      break;
    case 2:
      if(isx->aty == SEXP_DQUOTE) value = isx->val;
      else if(isx->aty == SEXP_BASIC && strchr(isx->val, '/')) {
        if((r = ydc_conf_get_val(ctx->values, (const char *)isx->val, &rval)))          goto __fini;
        else r = EINVAL; /* reset error code */

        if(rval->type != STRING) goto __fini;

        value = (char *)rval->value;
      } else goto __fini;

      if(!value) goto __fini;

      break;
    default: goto __fini; break;
    }
  }

  /* set it */
  r = __set_dbvariable(db, value, vs);

 __fini:
  RETURN_SRET_IRES(rets, r);
}

static scret_t __psqlbe_add_db(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int idx;
  int r = EINVAL;
  sexp_t *isx;
  char *name = NULL;
  ydc_conf_val_t *rval;
  scret_t rets;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST) goto __fini;

    switch(idx) {
    case 0:
      if(strcmp(isx->val, PSQLBESYN_ADDDB)) goto __fini;      break;
    case 1:
      if(isx->aty == SEXP_DQUOTE) name = isx->val;
      else if(isx->aty == SEXP_BASIC && strchr(isx->val, '/')) {
        if((r = ydc_conf_get_val(ctx->values, (const char *)isx->val, &rval)))          goto __fini;
        else r = EINVAL; /* reset error code */

        if(rval->type != STRING) goto __fini;

        name = (char *)rval->value;
      } else goto __fini;
      break;
    default: goto __fini; break;
    }
  }

  /* add it */
  if(name && __alloc_insert_db((usrtc_t *)priv, (const char *)name)) r = 0;

 __fini:
  RETURN_SRET_IRES(rets, r);
}

/* -=backend functions=- */

/* initialization part */
static int __psqlbe_init(domx_dsbe_t *domx, const char *key)
{
  usrtc_node_t *unode;
  struct psqldb *db = NULL;
  domx_t *domxs = domx->domx;
  struct psqlbe_priv *priv;
  char *tname = NULL;
  int r = 0;

  if(!(unode = usrtc_lookup(dbtree, (const void *)key))) {r = ENOENT; goto __fini;}
  else db = (struct psqldb *)usrtc_node_getdata(unode);

  if(!db) {r = ENOENT; goto __fini;}

  if((r = __connect_dbpg(db))) goto __fini;

  if((r = __check_objects_table(db))) goto __fini;

  if((r = __check_objectdesc_table(db))) goto __fini;

  if((r = __get_objecttable(db, domxs->name, &tname, domxs->dox))) goto __fini;

  /* test is done */
  if(!(priv = malloc(sizeof(struct psqlbe_priv)))) { r = ENOMEM; goto __fini; }
  domx->priv = priv;
  priv->tablename = tname;
  priv->db = db;
  priv->attr = 0;

 __fini:
  return r;
}

/* data manipulation */
static inline void __cstr_array2data(char *cstrdata, void *data, uint8_t type, uint64_t len)
{
  char *tbuf = cstrdata;
  char *cp = data, *cptr;
  long long int ival;
  int i = 0;

  /* skip the first value */
  if(tbuf[0] != '{') return;
  else tbuf += sizeof(char);

  while(i < len) {
    ival = strtoll(tbuf, &cptr, 10);
    if((ival != LLONG_MIN) && (ival != LLONG_MAX)) {
      switch(type) {
      case U8: *(uint8_t *)cp = (uint8_t)ival; cp += sizeof(uint8_t); break;
      case S8: *(int8_t *)cp = (int8_t)ival; cp += sizeof(int8_t); break;
      case U16: *(uint16_t *)cp = (uint16_t)ival; cp += sizeof(uint16_t); break;
      case S16: *(int16_t *)cp = (int16_t)ival; cp += sizeof(int16_t); break;
      case U32: *(uint32_t *)cp = (uint32_t)ival; cp += sizeof(uint32_t); break;
      case S32: *(int32_t *)cp = (int32_t)ival; cp += sizeof(int32_t); break;
      case U64: *(uint64_t *)cp = (uint64_t)ival; cp += sizeof(uint64_t); break;
      case S64: *(int64_t *)cp = (int64_t)ival; cp += sizeof(int64_t); break;
      default: break;
      }
    }
    /* check for the end */
    if(*cptr == '}') break;
    i++; tbuf = cptr + sizeof(char);
  }


  return;
}

static inline void __cstr_array2acc(char *cstrdata, void *data)
{
  acc_right_t *ac = (acc_right_t *)data;
  char *tbuf = cstrdata, *cptr;
  long long int ival;
  int i;

  /* skip the first value */
  if(tbuf[0] != '{') return;
  else tbuf += sizeof(char);

  for(i = 0; i < 6; i++) {
    ival = strtoll(tbuf, &cptr, 10);
    if((ival != LLONG_MIN) && (ival != LLONG_MAX)) {
      switch(i) {
      case 0: ac->ouid = (uint32_t)ival; break;
      case 1: ac->ogid = (uint32_t)ival; break;
      case 2: ac->domainid = (uint8_t)ival; break;
      case 3: ac->sal = (uint8_t)ival; break;
      case 4: ac->amask = (uint8_t)ival; break;
      case 5: ac->reserve = (uint8_t)ival; break;
      default: break;
      }
    }

    /* check for the end */
    if(*cptr == '}') break;
    tbuf = cptr + sizeof(char);
  }

  return;
}

static int __psqlbe_get(domx_dsbe_t *domx, oid_t oid, void *data)
{
  register int i;
  struct psqldb *db = NULL;
  domx_t *domxs = domx->domx;
  struct psqlbe_priv *priv = domx->priv;
  dataobject_t *dobj;
  char *tbuf;
  PGresult *res;
  list_node_t *iter, *siter;
  dataobject_item_t *ditem;
  char sql[2048];
  int r = 0;

  /* check values */
  if(!domxs || !priv) return EINVAL;
  if(!(db = priv->db)) return EINVAL;

  dobj = domxs->dox;

  /* ok now we need to form a query */
  snprintf(sql, 2048, "select ");
  tbuf = sql + strlen(sql);
  for(i = 0; i < usrtc_count((usrtc_t *)&dobj->id_index); i++) {
    if(i) { snprintf(tbuf, 2048 - strlen(sql), ", "); tbuf = sql + strlen(sql); }
    snprintf(tbuf, 2048 - strlen(sql), "i%d", i);
    tbuf = sql + strlen(sql);
  }
  snprintf(tbuf, 2048 - strlen(sql), " from %s where oid = %lu;", priv->tablename, oid);
  /* perform a query */
  res = PQexec(db->dbc, sql);
  if(!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
    r = EIO;
    goto __fail;
  }
  if(!PQntuples(res)) { r = ENOENT; goto __fail; }

  /* now we need to fill the data */
  tbuf = (char *)data;
  list_for_each_safe(&dobj->description, iter, siter) {
    ditem = container_of(iter, dataobject_item_t, node);
    switch(ditem->type) {
    case U8:
    case S8:
      if(ditem->len == tltable[ditem->type].len) *(uint8_t *)tbuf = atoi(PQgetvalue(res, 0, ditem->id));
      else __cstr_array2data(PQgetvalue(res, 0, ditem->id), (void *)tbuf,
                             ditem->type, ditem->len/tltable[ditem->type].len);
      break;
    case U16:
    case S16:
      if(ditem->len == tltable[ditem->type].len) *(uint16_t *)tbuf = atoi(PQgetvalue(res, 0, ditem->id));
      else __cstr_array2data(PQgetvalue(res, 0, ditem->id), (void *)tbuf,
                             ditem->type, ditem->len/tltable[ditem->type].len);
      break;
    case U32:
    case S32:
      if(ditem->len == tltable[ditem->type].len) *(uint32_t *)tbuf = atoi(PQgetvalue(res, 0, ditem->id));
      else __cstr_array2data(PQgetvalue(res, 0, ditem->id), (void *)tbuf,
                             ditem->type, ditem->len/tltable[ditem->type].len);
      break;
    case U64:
    case S64:
      if(ditem->len == tltable[ditem->type].len) *(uint64_t *)tbuf = atoll(PQgetvalue(res, 0, ditem->id));
      else __cstr_array2data(PQgetvalue(res, 0, ditem->id), (void *)tbuf,
                             ditem->type, ditem->len/tltable[ditem->type].len);
      break;
    case CSTR:
    case TBLOB:
      memset(tbuf, 0, ditem->len);
      strncpy(tbuf, PQgetvalue(res, 0, ditem->id), ditem->len - sizeof(char));
      break;
    case TACRT:
      __cstr_array2acc(PQgetvalue(res, 0, ditem->id), (void *)tbuf);
      break;
    }
    tbuf += ditem->len;
  }

 __fail:
  if(res) PQclear(res);

  return r;
}

static inline int __data2cstrarr(char *cstr, size_t climit, uint8_t dtype, char *data, int nitems)
{
  register int i;
  int sz = 0;
  char *tc, *dtr = data;

  sz += snprintf(cstr, climit, "'{");
  tc = cstr + sz;
  for(i = 0; i < nitems; i++) {
    if(i) {
      sz += snprintf(tc, climit - sz, ",");
      tc = cstr + sz;
    }

    switch(dtype) {
    case U8:
      sz += snprintf(tc, climit - sz, "%hhu", *(uint8_t *)dtr); dtr += sizeof(uint8_t); break;
    case S8:
      sz += snprintf(tc, climit - sz, "%hhd", *(int8_t *)dtr); dtr += sizeof(int8_t); break;
    case U16:
      sz += snprintf(tc, climit - sz, "%hu", *(uint16_t *)dtr); dtr += sizeof(uint16_t); break;
    case S16:
      sz += snprintf(tc, climit - sz, "%hd", *(int16_t *)dtr); dtr += sizeof(int16_t); break;
    case U32:
      sz += snprintf(tc, climit - sz, "%u", *(uint32_t *)dtr); dtr += sizeof(uint32_t); break;
    case S32:
      sz += snprintf(tc, climit - sz, "%d", *(int32_t *)dtr); dtr += sizeof(int32_t); break;
    case U64:
      sz += snprintf(tc, climit - sz, "%ld", *(int64_t *)dtr); dtr += sizeof(int64_t); break;
    case S64:
      sz += snprintf(tc, climit - sz, "%ld", *(int64_t *)dtr); dtr += sizeof(uint64_t); break;
    }

    tc = cstr + sz;
  }

  sz += snprintf(tc, climit - sz, "}'");

  return sz;
}

static inline int __data2cstr(char *cstr, uint8_t dtype, char *data, size_t climit, size_t len)
{
  int sz = 0;
  acc_right_t *a;

  switch(dtype) {
  case U8:
    if(len == tltable[dtype].len) sz = snprintf(cstr, climit, "%hhu", *(uint8_t *)data);
    else sz = __data2cstrarr(cstr, climit, dtype, data, len/sizeof(uint8_t));
    break;
  case S8:
    if(len == tltable[dtype].len) sz = snprintf(cstr, climit, "%hhd", *(int8_t *)data);
    else sz = __data2cstrarr(cstr, climit, dtype, data, len/sizeof(int8_t));
    break;
  case U16:
    if(len == tltable[dtype].len) sz = snprintf(cstr, climit, "%hu", *(uint16_t *)data);
    else sz = __data2cstrarr(cstr, climit, dtype, data, len/sizeof(uint16_t));
    break;
  case S16:
    if(len == tltable[dtype].len) sz = snprintf(cstr, climit, "%hd", *(int16_t *)data);
    else sz = __data2cstrarr(cstr, climit, dtype, data, len/sizeof(int16_t));
    break;
  case U32:
    if(len == tltable[dtype].len) sz = snprintf(cstr, climit, "%u", *(uint32_t *)data);
    else sz = __data2cstrarr(cstr, climit, dtype, data, len/sizeof(uint32_t));
    break;
  case S32:
    if(len == tltable[dtype].len) sz = snprintf(cstr, climit, "%d", *(int32_t *)data);
    else sz = __data2cstrarr(cstr, climit, dtype, data, len/sizeof(int32_t));
    break;
  case U64:
    if(len == tltable[dtype].len) sz = snprintf(cstr, climit, "%ld", *(int64_t *)data);
    else sz = __data2cstrarr(cstr, climit, dtype, data, len/sizeof(uint64_t));
    break;
  case S64:
    if(len == tltable[dtype].len) sz = snprintf(cstr, climit, "%ld", *(int64_t *)data);
    else sz = __data2cstrarr(cstr, climit, dtype, data, len/sizeof(int64_t));
    break;
  case CSTR:
  case TBLOB:
    sz = snprintf(cstr, climit, "'%s'", (char *)data);
    break;
  case TACRT:
    a = (acc_right_t *)data;
    sz = snprintf(cstr, climit, "'{%u,%u,%hhu,%hhu,%hhu,%hhu}'", a->ouid, a->ogid,
                  a->domainid, a->sal, a->amask, a->reserve);
    break;
  }

  return sz;
}

static int __psqlbe_set(domx_dsbe_t *domx, oid_t oid, void *data)
{
  register int i;
  struct psqldb *db = NULL;
  domx_t *domxs = domx->domx;
  struct psqlbe_priv *priv = domx->priv;
  dataobject_t *dobj;
  char *tbuf, *tdata = data;
  PGresult *res;
  list_node_t *iter, *siter;
  dataobject_item_t *ditem;
  char sql[2048];
  int r = 0;

  /* check values */
  if(!domxs || !priv) return EINVAL;
  if(!(db = priv->db) || !data) return EINVAL;

  dobj = domxs->dox;

  /* ok now we need to form a query */
  snprintf(sql, 2048, "update %s set (", priv->tablename);
  tbuf = sql + strlen(sql);
  for(i = 0; i < usrtc_count((usrtc_t *)&dobj->id_index); i++) {
    if(i) { snprintf(tbuf, 2048 - strlen(sql), ", "); tbuf = sql + strlen(sql); }
    snprintf(tbuf, 2048 - strlen(sql), "i%d", i);
    tbuf = sql + strlen(sql);
  }
  snprintf(tbuf, 2048 - strlen(sql), ") = (");
  tbuf = sql + strlen(sql);

  /* fill the data */
  r = 0;
  list_for_each_safe(&dobj->description, iter, siter) {
    if(r) { snprintf(tbuf, 2048 - strlen(sql), ", "); tbuf = sql + strlen(sql); }
    ditem = container_of(iter, dataobject_item_t, node);
    tbuf += __data2cstr(tbuf, ditem->type, tdata, 2048 - strlen(sql), ditem->len);
    tdata += ditem->len; r++;
  }
  tbuf = sql + strlen(sql);
  snprintf(tbuf, 2048 - strlen(sql), ") where oid = %lu;", oid);

  res = PQexec(db->dbc, sql);
  if(!res || PQresultStatus(res) != PGRES_COMMAND_OK)     r = EIO;
  else r = 0;

  if(res) PQclear(res);

  return r;
}

static oid_t __psqlbe_creat(domx_dsbe_t *domx, const void *data)
{
  register int i;
  struct psqldb *db = NULL;
  domx_t *domxs = domx->domx;
  struct psqlbe_priv *priv = domx->priv;
  dataobject_t *dobj;
  char *tbuf, *tdata = (char *)data;
  PGresult *res;
  list_node_t *iter, *siter;
  dataobject_item_t *ditem;
  char sql[2048];
  int ln = 0, tln = 0;
  oid_t r = 0;

  /* check values */
  if(!domxs || !priv) { errno = EINVAL; goto __fini; }
  if(!(db = priv->db) || !data) { errno = EINVAL; goto __fini; }

  dobj = domxs->dox;

  /* form the query */
  tbuf = sql;
  ln = snprintf(tbuf, 2048 - tln, "insert into %s (", priv->tablename);
  tbuf += ln; tln += ln;
  for(i = 0; i < usrtc_count((usrtc_t *)&dobj->id_index); i++) {
    if(i) { ln = snprintf(tbuf, 2048 - tln, ", "); tbuf += ln; tln += ln; }
    ln = snprintf(tbuf, 2048 - tln, "i%d", i);
    tbuf += ln; tln += ln;
  }
  ln = snprintf(tbuf, 2048 - tln, ") values (");
  tbuf += ln; tln += ln;
  /* fill the data */
  ln = 0;
  list_for_each_safe(&dobj->description, iter, siter) {
    if(ln) { ln = snprintf(tbuf, 2048 - tln, ", "); tbuf += ln; tln += ln; }
    ditem = container_of(iter, dataobject_item_t, node);
    ln = __data2cstr(tbuf, ditem->type, tdata, 2048 - strlen(sql), ditem->len);
    tbuf += ln; tln += ln;
    tdata += ditem->len;
  }
  ln = snprintf(tbuf, 2048 - tln, ") returning oid;");

  /* call postgres */
  res = PQexec(db->dbc, sql);
  if(!res || PQresultStatus(res) != PGRES_TUPLES_OK)
    errno = EIO;
  else {
    errno = 0;
    r = atoll(PQgetvalue(res, 0, 0));
  }

  if(res) PQclear(res);

 __fini:
  return r;
}

static int __psqlbe_remove(domx_dsbe_t *domx, oid_t oid)
{
  struct psqldb *db = NULL;
  domx_t *domxs = domx->domx;
  struct psqlbe_priv *priv = domx->priv;
  PGresult *res;
  char sql[2048];
  int r = 0;

  /* check values */
  if(!domxs || !priv) return EINVAL;
  if(!(db = priv->db)) return EINVAL;

  /* form a request */
  snprintf(sql, 2048, "delete from %s where oid = %lu;\n", priv->tablename, oid);

  /* call postgres */
  res = PQexec(db->dbc, sql);
  if(!res || PQresultStatus(res) != PGRES_COMMAND_OK)    r = EIO;

  if(res) PQclear(res);

  return r;
}

/* list requesting */

struct __distream_priv { /* private for backend structure to have a deal with stream */
  char *sqprefix;
  domx_dsbe_t *domx;
  int initial;
};

/* internal ops to get the list */

static inline yd_wlist_node_t *__oidetalloc(oid_t oid)
{
  yd_wlist_node_t *_node = malloc(sizeof(yd_wlist_node_t));

  if(!_node) return NULL;
  else _node->oid = oid;

  list_init_node(&_node->node);

  return _node;
}

static inline void __iterate_list(yd_idx_stream_t *stream, int initial)
{
  register int i;
  struct __distream_priv *_priv = yd_index_stream_getpriv(stream);
  domx_dsbe_t *domx = _priv->domx;
  struct psqldb *db = NULL;
  struct psqlbe_priv *priv = domx->priv;
  PGresult *res;
  yd_wlist_node_t *oidnode;
  char sql[3072];

  if(!(db = priv->db)) return;

  if(!initial && !stream->offset) {
  __eos:
    stream->amount = 0;
    return; /* end-of-stream */
  }

  /* empty list - might be filled from previous request */
  yd_index_stream_emptylist(stream);

  /* form sql query */
  snprintf(sql, 3072, "%s %u;", _priv->sqprefix, stream->offset);
  res = PQexec(db->dbc, sql);
  if(!res || PQresultStatus(res) != PGRES_TUPLES_OK)    goto __eos; /* shit */

  stream->amount = PQntuples(res);
  if(!stream->amount) goto __eos; /* end of stream */

  if(stream->amount < YD_CHUNK_AMOUNT) stream->offset = 0;
  else stream->offset += stream->amount;

  for(i = 0; i < stream->amount; i++) {
    oidnode = __oidetalloc(atoll(PQgetvalue(res, i, 0)));
    if(!oidnode) { PQclear(res); goto __eos; }
    else list_add2tail(&stream->entries_wlist, &oidnode->node);
  }

  if(res) PQclear(res);

  return;
}

static yd_idx_stream_t *__psqlbe_create_idx_stream(domx_dsbe_t *domx, ydm_access_filter_t accfilter,
                                                   dataacc_pemctx_t *permobj, yd_filter_t *filter)
{
  register int i, a;
  struct psqldb *db = NULL;
  domx_t *domxs = domx->domx;
  struct psqlbe_priv *priv = domx->priv;
  dataobject_t *dobj;
  char *tbuf;
  list_node_t *iter, *siter, *_iter, *_siter;
  yd_inlist_item_t *inlistitem;
  char *sql = NULL;
  int ln = 0, tln = 0;
  uint64_t datalen = 0;
  uint8_t dtype, has_filter = 0, iil = 0;
  yd_filter_item_t *fitem;
  yd_idx_stream_t *out;
  struct __distream_priv *pristruct;

  /* check values */
  if(!domxs || !priv) goto __einval;
  if(!(db = priv->db)) goto __einval;

  dobj = domxs->dox;

  /* check for correct filtering in case of access one */
  if(accfilter != YDM_NO_FILTER) {
    if(!permobj) {
    __einval:
      errno = EINVAL;
      return NULL;
    }
    if(!(dtype = dotr_item_type(dobj, "acc", &datalen)) && !datalen) goto __einval;
    if(dtype != TACRT) goto __einval;
  }

  /* check up for filtering */
  if(accfilter != YDM_NO_FILTER) has_filter++;
  if(filter) has_filter++;

  if(!(sql = malloc(2048))) {
    errno = ENOMEM;
    return NULL;
  } else memset(sql, 0, 2048);

  ln = snprintf(sql, 2048 - tln, "select oid from %s ", priv->tablename);
  tbuf = sql + ln; tln += ln;

  if(has_filter) {
    ln = snprintf(tbuf, 2048 - tln, "where ");
    tbuf += ln; tln += ln;

    if(accfilter > YDM_NO_FILTER) { /* by domain */
      i = dotr_item_nameidx(dobj, "acc");

      ln = snprintf(tbuf, 2048 - tln, "i%d[2] = %hhu ", i, permobj->uobj->domainid);
      tbuf += ln; tln += ln;
    }
    if(accfilter > YDM_DOMAIN_FILTER) { /* user */
      ln = snprintf(tbuf, 2048 - tln, "and i%d[0] = %u and i%d[1] in (%u,", i, permobj->uobj->ouid,
                    i, permobj->uobj->ogid);
      tbuf += ln; tln += ln;
      for(a = 0; a < 16; a++) {
        if(permobj->gids[a] != 0) {
          if(has_filter)
            ln = snprintf(tbuf, 2048 - tln, "%u", permobj->gids[a]);
          else {
            ln = snprintf(tbuf, 2048 - tln, ",%u", permobj->gids[a]);
            has_filter = 0;
          }
          tbuf += ln; tln += ln;
        }
      }
      ln = snprintf(tbuf, 2048 - tln, ") ");
      tbuf += ln; tln += ln;
    }
    if(accfilter > YDM_USERDAC_FILTER) { /* full access filter */
      ln = snprintf(tbuf, 2048 - tln, "and i%d[3] >= %hhu ", i, permobj->uobj->sal);
      tbuf += ln; tln += ln;
    }

    /* ok, data filter, we will not take a check here, since it tested already and valid */
    if(filter) { /* we have a data filter */
      list_for_each_safe(&filter->filter, iter, siter) {
        fitem = container_of(iter, yd_filter_item_t, node);
        /* get index of the object */
        i = dotr_item_nameidx(dobj, fitem->name);
        dtype = dotr_item_type(dobj, fitem->name, &datalen);
        if(accfilter != YDM_NO_FILTER) iil++;

        if(iil)  ln = snprintf(tbuf, 2048 - tln, "and i%d ", i);
        else ln = snprintf(tbuf, 2048 - tln, "i%d ", i);

        tbuf += ln; tln += ln;
        iil++;

        switch(fitem->ftype) {
        case YDEQUAL:
          ln = snprintf(tbuf, 2048 - tln, "= ");
          tbuf += ln; tln += ln;
          break;
        case YDNOTEQUAL:
          ln = snprintf(tbuf, 2048 - tln, "!= ");
          tbuf += ln; tln += ln;
          break;
        case YDLESS:
          ln = snprintf(tbuf, 2048 - tln, "< ");
          tbuf += ln; tln += ln;
          break;
        case YDGREATER:
          ln = snprintf(tbuf, 2048 - tln, "> ");
          tbuf += ln; tln += ln;
          break;
        case YDEQOLESS:
          ln = snprintf(tbuf, 2048 - tln, "=< ");
          tbuf += ln; tln += ln;
          break;
        case YDEQOGREATER:
          ln = snprintf(tbuf, 2048 - tln, "=> ");
          tbuf += ln; tln += ln;
          break;
        case YDINRANGE:
          ln = snprintf(tbuf, 2048 - tln, "between ");
          tbuf += ln; tln += ln;
          break;
        case YDINLIST:
          ln = snprintf(tbuf, 2048 - tln, "in (");
          tbuf += ln; tln += ln;
          break;
        }

        if(fitem->ftype < YDINRANGE) { /* simple cases */
          switch(dtype) {
          case U8:
            ln = snprintf(tbuf, 2048 - tln, "%hhu ", (uint8_t)fitem->vf);
            tbuf += ln; tln += ln;
            break;
          case S8:
            ln = snprintf(tbuf, 2048 - tln, "%hhd ", (int8_t)fitem->vf);
            tbuf += ln; tln += ln;
            break;
          case U16:
            ln = snprintf(tbuf, 2048 - tln, "%hu ", (uint16_t)fitem->vf);
            tbuf += ln; tln += ln;
            break;
          case S16:
            ln = snprintf(tbuf, 2048 - tln, "%hd ", (int16_t)fitem->vf);
            tbuf += ln; tln += ln;
            break;
          case U32:
            ln = snprintf(tbuf, 2048 - tln, "%u ", (uint32_t)fitem->vf);
            tbuf += ln; tln += ln;
            break;
          case S32:
            ln = snprintf(tbuf, 2048 - tln, "%d ", (int32_t)fitem->vf);
            tbuf += ln; tln += ln;
            break;
          case U64:
            ln = snprintf(tbuf, 2048 - tln, "%ld ", (int64_t)fitem->vf);
            tbuf += ln; tln += ln;
            break;
          case S64:
            ln = snprintf(tbuf, 2048 - tln, "%ld ", (int64_t)fitem->vf);
            tbuf += ln; tln += ln;
            break;
          case CSTR:
          case TBLOB:
            ln = snprintf(tbuf, 2048 - tln, "'%s' ", fitem->cstr);
            tbuf += ln; tln += ln;
            break;
          default: break;
          }
        } else if(fitem->ftype == YDINRANGE) {
          switch(dtype) {
          case U8:
            ln = snprintf(tbuf, 2048 - tln, "%hhu and %hhu", (uint8_t)fitem->vf, (uint8_t)fitem->vc);
            tbuf += ln; tln += ln;
            break;
          case S8:
            ln = snprintf(tbuf, 2048 - tln, "%hhd and %hhd", (int8_t)fitem->vf, (int8_t)fitem->vc);
            tbuf += ln; tln += ln;
            break;
          case U16:
            ln = snprintf(tbuf, 2048 - tln, "%hu and %hu", (uint16_t)fitem->vf, (uint16_t)fitem->vc);
            tbuf += ln; tln += ln;
            break;
          case S16:
            ln = snprintf(tbuf, 2048 - tln, "%hd and %hd", (int16_t)fitem->vf, (int16_t)fitem->vc);
            tbuf += ln; tln += ln;
            break;
          case U32:
            ln = snprintf(tbuf, 2048 - tln, "%u and %u", (uint32_t)fitem->vf, (uint32_t)fitem->vc);
            tbuf += ln; tln += ln;
            break;
          case S32:
            ln = snprintf(tbuf, 2048 - tln, "%d and %d", (int32_t)fitem->vf, (int32_t)fitem->vc);
            tbuf += ln; tln += ln;
            break;
          case U64:
            ln = snprintf(tbuf, 2048 - tln, "%ld and %ld", (int64_t)fitem->vf, (int64_t)fitem->vc);
            tbuf += ln; tln += ln;
            break;
          case S64:
            ln = snprintf(tbuf, 2048 - tln, "%ld and %ld", (int64_t)fitem->vf, (int64_t)fitem->vc);
            tbuf += ln; tln += ln;
            break;
          default: break;
          }
        } else { /* list of values */
          ln = 0;
          list_for_each_safe(fitem->inlist, _iter, _siter) {
            inlistitem = container_of(_iter, yd_inlist_item_t, node);
            if(ln) { /* trailing comma */
              ln = snprintf(tbuf, 2048 - tln, ",");
              tbuf += ln; tln += ln;
            }
            /* switch - types */
            switch(dtype) {
            case U8:
              ln = snprintf(tbuf, 2048 - tln, "%hhu ", (uint8_t)inlistitem->val);
              tbuf += ln; tln += ln;
              break;
            case S8:
              ln = snprintf(tbuf, 2048 - tln, "%hhd ", (int8_t)inlistitem->val);
              tbuf += ln; tln += ln;
              break;
            case U16:
              ln = snprintf(tbuf, 2048 - tln, "%hu ", (uint16_t)inlistitem->val);
              tbuf += ln; tln += ln;
              break;
            case S16:
              ln = snprintf(tbuf, 2048 - tln, "%hd ", (int16_t)inlistitem->val);
              tbuf += ln; tln += ln;
              break;
            case U32:
              ln = snprintf(tbuf, 2048 - tln, "%u ", (uint32_t)inlistitem->val);
              tbuf += ln; tln += ln;
              break;
            case S32:
              ln = snprintf(tbuf, 2048 - tln, "%d ", (int32_t)inlistitem->val);
              tbuf += ln; tln += ln;
              break;
            case U64:
              ln = snprintf(tbuf, 2048 - tln, "%ld ", (int64_t)inlistitem->val);
              tbuf += ln; tln += ln;
              break;
            case S64:
              ln = snprintf(tbuf, 2048 - tln, "%ld ", (int64_t)inlistitem->val);
              tbuf += ln; tln += ln;
              break;
            case CSTR:
            case TBLOB:
              ln = snprintf(tbuf, 2048 - tln, "'%s' ", inlistitem->dta);
              tbuf += ln; tln += ln;
              break;
            default: break;
            }
          }
          /* close bracelet */
          ln = snprintf(tbuf, 2048 - tln, ") ");
          tbuf += ln; tln += ln;
        }

      }
    }
  }

  ln = snprintf(tbuf, 2048 - tln, "order by oid limit %d offset ", YD_CHUNK_AMOUNT);

  /* now we're ready to initialize stream */
  if(!(pristruct = malloc(sizeof(struct __distream_priv)))) goto __eno2; /* init private data */
  else {
    pristruct->sqprefix = sql;
    pristruct->domx = domx;
    pristruct->initial = 1;
  }

  if(!(out = yd_index_stream_init())) {
    free(pristruct);
  __eno2:
    free(sql);
    errno = ENOMEM;
    return NULL;
  } else {
    yd_index_stream_setfilter(out, filter);
    yd_index_stream_setpriv(out, pristruct);
  }

  /* get the initial list */
  __iterate_list(out, 1);

  return out;
}

static void __psqlbe_destroy_idx_stream(yd_idx_stream_t *distream)
{
  struct __distream_priv *_priv = yd_index_stream_getpriv(distream);

  if(_priv) {
    free(_priv->sqprefix);
    free(_priv);
  }

  yd_index_stream_destroy(distream);

  return;
}

static yd_idx_stream_win_t *__psqlbe_getportion_idx_stream(yd_idx_stream_t *distream)
{
  yd_idx_stream_win_t *zdswin = malloc(sizeof(yd_idx_stream_win_t));
  struct __distream_priv *_priv = yd_index_stream_getpriv(distream);

  if(!zdswin) return NULL;

  if(!_priv->initial) __iterate_list(distream, 0);
  else _priv->initial = 0;

  if(!distream->amount) { free(zdswin); return NULL; }

  zdswin->amount = distream->amount;
  zdswin->wlist = &distream->entries_wlist;

  return zdswin;
}

/* shared objects typedefs */
typedef int (*rpc_func_t)(void *, sexp_t *);

def_shutdown(psqlbe) {
  /* we don't care about shutdown, since datamux must deinit all backends */
  return 0;
}

def_preinit(psqlbe) {
  int r = 0;

  if(!(ostore = malloc(sizeof(obj_store_t)))) return ENOMEM;
  else r = obj_store_init(ostore);

  if(!(dbtree = malloc(sizeof(usrtc_t)))) return ENOMEM;
  else usrtc_init(dbtree, USRTC_AVL, __MAX_DATABASES, __cmp_cstr);

  /* add special functions */
  scm_func_tree_insert(ctx, PSQLBESYN_ADDDB, __psqlbe_add_db,
                       (void *)dbtree);
  scm_func_tree_insert(ctx, PSQLBESYN_SETDB_HOST, __psqlbe_set_db,
                       (void *)dbtree);
  scm_func_tree_insert(ctx, PSQLBESYN_SETDB_NAME, __psqlbe_set_db,
                       (void *)dbtree);
  scm_func_tree_insert(ctx, PSQLBESYN_SETDB_USER, __psqlbe_set_db,
                       (void *)dbtree);
  scm_func_tree_insert(ctx, PSQLBESYN_SETDB_PASSWD, __psqlbe_set_db,
                       (void *)dbtree);

  return r;
}

def_init(psqlbe) {

  /* init ops for backend */
  memset(&psqlbe_ops, 0, sizeof(struct be_ops));
  psqlbe_ops.be_magic = BEMAGIC;
  psqlbe_ops.init = __psqlbe_init;
  psqlbe_ops.set = __psqlbe_set;
  psqlbe_ops.get = __psqlbe_get;
  psqlbe_ops.creat = __psqlbe_creat;
  psqlbe_ops.remove = __psqlbe_remove;
  psqlbe_ops.create_idx_stream = __psqlbe_create_idx_stream;
  psqlbe_ops.destroy_idx_stream = __psqlbe_destroy_idx_stream;
  psqlbe_ops.getportion_idx_stream = __psqlbe_getportion_idx_stream;

  /* store objects */
  obj_store_set(ostore, "psqlbe-ops", (void *)&psqlbe_ops);

  return 0;
}

def_run(psqlbe) {
  /* we're don't care about it too, since backend is going init while setting it for object via datamux */
  return 0;
}

def_getobject(psqlbe) {
  void *obj = NULL;
  int r = obj_store_get(ostore, oname, &obj);

  if(r)
    return NULL;
  else return obj;
}

