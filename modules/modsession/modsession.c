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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>

#include <tdata/usrtc.h>
#include <tdata/list.h>
#include <sxmp/sxmp.h>
#include <ydaemon/ydaemon.h>
#include <ydaemon/dataobject.h>

#include <coredata.h>

#define MOD_PREFIX  session

/* local structure used to store shared data */
typedef struct __provided_dobject {
  domx_t *odomx;
  list_node_t node;
} prov_object_t;

/* special functions */
static inline prov_object_t *__alloc_init_prov_object(domx_t *store)
{
  prov_object_t *shrobj = malloc(sizeof(prov_object_t));

  if(shrobj) {
    shrobj->odomx = store;
    list_init_node(&shrobj->node);
  }

  return shrobj;
}

/* a few globals */
static obj_store_t *ostore = NULL;
static domx_t *certificates = NULL;
static domx_t *users = NULL;
static domx_t *groups = NULL;
static domx_t *roles = NULL;
static yd_context_t *actx = NULL;
static list_head_t *_provided_dobjects_list = NULL;

#define __SALPREFIX  "SESSION"

/* syntax defines */
/**
 * Syntax: (<followed function> (object reference))
 */
#define MODSAUTHSYN_SET_CERTIFICATES_STORE  "session-set-certificates-store"
#define MODSAUTHSYN_SET_USERS_STORE         "session-set-users-store"
#define MODSAUTHSYN_SET_GROUPS_STORE        "session-set-groups-store"
#define MODSAUTHSYN_SET_ROLES_STORE         "session-set-roles-store"
/**
 * Syntax: (<followed function> (object references))
 */
#define MODSAUTHSYN_ADD_SHAREDDATAOBJECT    "session-set-shared-dataobject"

static scret_t __sauth_add_shareddataobject(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int idx;
  int r = EINVAL;
  domx_t *store = NULL;
  prov_object_t *shrobj;
  sexp_t *isx;
  scret_t rets, irets;
  list_head_t *objlist = (list_head_t *)priv;

  if(!objlist) goto __fini;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    switch(idx) {
    case 0:
      if(isx->ty == SEXP_LIST) goto __fini;
      if(isx->aty != SEXP_BASIC) goto __fini;

      /* determine function */
      if(strcmp(isx->val, MODSAUTHSYN_ADD_SHAREDDATAOBJECT)) goto __fini;
      break;
    default:
      if(isx->ty != SEXP_LIST) goto __fini;

      irets = yd_eval_sexp(ctx, isx);
      if(irets.type != SCOBJECTPTR) goto __fini;

      store = (domx_t *)irets.ret;
      if(!store) goto __fini;

      /* create a list entry */
      shrobj = __alloc_init_prov_object(store);
      if(!shrobj) { r = ENOMEM; goto __fini; }
      else list_add2tail(objlist, &shrobj->node);
      break;
    }
  }

 __fini:
  RETURN_SRET_IRES(rets, r);
}

static scret_t __sauth_set_store(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int idx;
  int r = EINVAL, str = -1;
  domx_t *store = NULL;
  sexp_t *isx;
  scret_t rets, irets;
  yd_idx_stream_t *stream = NULL;
  yd_idx_stream_win_t *swin = NULL;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    switch(idx) {
    case 0:
      if(isx->ty == SEXP_LIST) goto __fini;
      if(isx->aty != SEXP_BASIC) goto __fini;

      /* determine function */
      if(!strcmp(isx->val, MODSAUTHSYN_SET_CERTIFICATES_STORE)) str = 0;
      else if(!strcmp(isx->val, MODSAUTHSYN_SET_USERS_STORE)) str = 1;
      else if(!strcmp(isx->val, MODSAUTHSYN_SET_GROUPS_STORE)) str = 2;
      else if(!strcmp(isx->val, MODSAUTHSYN_SET_ROLES_STORE)) str = 3;
      else goto __fini;
      break;
    case 1:
      if(isx->ty != SEXP_LIST) goto __fini;

      irets = yd_eval_sexp(ctx, isx);
      if(irets.type != SCOBJECTPTR) goto __fini;

      store = (domx_t *)irets.ret;
      break;
    default: goto __fini;
    }
  }

  if(str == -1 || !store) goto __fini; /* check it up */
  else r = 0;

  /* set it */
  switch(str) {
  case 0: certificates = store;
    if(!(stream = domx_idxl_open(certificates, YDM_NO_FILTER, NULL, NULL))) {
      ydlog(ctx, YL_ERROR, "%s: Cannot create index stream to get the data.\n",
            __SALPREFIX);
      goto __fini;
    }
    if(!(swin = domx_idxl_read(certificates, stream))) {
    __notfound:
      ydlog(ctx, YL_WARN, "%s: No certificates objects found.\n",
            __SALPREFIX);
    } else {
      if(yd_idx_stream_win_size(swin) == 0) goto __notfound;
    }
    if(stream) domx_idxl_close(certificates, stream);
    break;
  case 1: users = store; break;
  case 2: groups = store; break;
  case 3: roles = store; break;
  default: r = EINVAL; break;
  }

 __fini:
  RETURN_SRET_IRES(rets, r);
}

/* shared objects typedefs */
typedef int (*rpc_func_t)(void *, sexp_t *);
typedef int(*creds_check_t)(sxlink_t *);
typedef usrtc_t *(*rpclist_check_t)(sxlink_t *, usrtc_t *, usrtc_t *);

//    rpclist_actual = func(c, rpclist_orig, rpclist_actual);

/* permission check up */
static int pem_check(sxlink_t *co)
{
  yd_filter_t *filter = NULL;
  yd_idx_stream_t *stream = NULL;
  yd_idx_stream_win_t *swin = NULL;
  yd_wlist_node_t *wlent = NULL;
  void *data;
  pem_t *pem_item = NULL;
  svsession_t *session = NULL;
  oid_t pemoid = 0;
  int r = 0;

  if(!certificates) {
    ydlog(actx, YL_WARN, "%s: Storage for certificates isn't set.\n",
          __SALPREFIX);
    goto __fini;
  }

  /* create a filter */
  filter = yd_filter_create(certificates);
  if(!filter) {
    ydlog(actx, YL_ERROR, "%s: Cannot create filter to get the data.\n",
          __SALPREFIX);
    return 1;
  }
  if((r = yd_filter_add_sf(filter, "pemid", YDEQUAL, (uint64_t)co->pctx->certid, 0))) {
    ydlog(actx, YL_ERROR, "%s: Cannot create filter rule to get the data (%d).\n",
          __SALPREFIX, r);
  __err:
    yd_filter_destroy(filter);
    return 1;
  }

  /* create a stream as a system, since we haven't a user context */
  if(!(stream = domx_idxl_open(certificates, YDM_NO_FILTER, NULL, filter))) {
    ydlog(actx, YL_ERROR, "%s: Cannot create index stream to get the data.\n",
          __SALPREFIX);
    goto __err;
  }
  if(!(swin = domx_idxl_read(certificates, stream))) {
  __notfound:
    ydlog(actx, YL_WARN, "%s: Cannot find certificate with pemid> %lu.\n",
          __SALPREFIX, co->pctx->certid);
    /* FIXME: reject session */
  } else {
    if(yd_idx_stream_win_size(swin) != 1) goto __notfound;

    wlent = container_of(list_node_first(swin->wlist), yd_wlist_node_t, node);
    pemoid = wlent->oid;

    if((r = domx_get(certificates, pemoid, &data))) {
      goto __notfound;
    }

    pem_item = (pem_t *)data; /* FIXME: make a copy */
    if(pem_item->attr & PEM_BLOCKED) { /* blocked */
      ydlog(actx, YL_WARN, "%s: Certificate with pemid> %lu is blocked, rejecting.\n",
            __SALPREFIX, co->pctx->certid);
      /* finish up */
      if(filter) yd_filter_destroy(filter);
      if(stream) domx_idxl_close(certificates, stream);
      return 1;
    }

    /* clean up data io */
    if(filter) yd_filter_destroy(filter);
    if(stream) domx_idxl_close(certificates, stream);
    filter = NULL;
    stream = NULL;

    /* now we're ready to allocate a session */
    session = malloc(sizeof(svsession_t));
    if(!session) {
      ydlog(actx, YL_WARN, "%s: Failed to create session, not enough memory.\n", __SALPREFIX);
      return 1;
    } else memset(session, 0, sizeof(svsession_t));
    /* copy data now */
    memcpy(&session->certificate, pem_item, sizeof(pem_t));
  }

  /* now we need to assign session */
  sxlink_setpriv(co, (void *)session);

 __fini:
  if(filter) yd_filter_destroy(filter);
  if(stream) domx_idxl_close(certificates, stream);
  return 0;
}

static int auth_check(sxlink_t *co)
{
  char *user = co->pctx->login;
  char *password = co->pctx->passwd;
  svsession_t *session = sxlink_getpriv(co);
  yd_filter_t *filter = NULL;
  yd_idx_stream_t *stream = NULL;
  yd_idx_stream_win_t *swin = NULL;
  yd_wlist_node_t *wlent = NULL;
  void *data;
  user_t *user_item = NULL;
  uint64_t useroid;
  int r = SXE_EPERM;

  /* if no user or password given - deny access in any cases */
  if(!user || !password) return SXE_EPERM;
  /* little paranoia */
  if(!session) return SXE_IGNORED;

  /* ok we should try to find this user in any case */
  if(!users) {
    ydlog(actx, YL_WARN, "%s: Storage for users isn't set.\n",
          __SALPREFIX);
    return SXE_FAILED;
  }

  /* create a filter */
  filter = yd_filter_create(users);
  if(!filter) {
    ydlog(actx, YL_ERROR, "%s: Cannot create filter to get the data.\n",
          __SALPREFIX);
    return SXE_FAILED;
  }
  if((r = yd_filter_add_sf(filter, "pemid", YDEQUAL, (uint64_t)co->pctx->certid, 0))) {
    ydlog(actx, YL_ERROR, "%s: Cannot create filter rule to get the data (%d).\n",
          __SALPREFIX, r);
  __err:
    yd_filter_destroy(filter);
    return SXE_FAILED;
  }
  if((r = yd_filter_add_str(filter, "login", user, 1))) {
    ydlog(actx, YL_ERROR, "%s: Cannot create filter rule to get the data (%d).\n",
          __SALPREFIX, r);
    goto __err;
  }

  /* create a stream */
  if(!(stream = domx_idxl_open(users, YDM_NO_FILTER, NULL, filter))) {
    ydlog(actx, YL_ERROR, "%s: Cannot create index stream to get the data.\n",
          __SALPREFIX);
    goto __err;
  }
  if(!(swin = domx_idxl_read(users, stream))) {
  __notfound:
    ydlog(actx, YL_WARN, "%s: Cannot find user linked to pemid> %lu.\n",
          __SALPREFIX, co->pctx->certid);
    r = SXE_EPERM;
    goto __fini;
  } else {
    if(yd_idx_stream_win_size(swin) != 1) {
    __notfound2:
      domx_idxl_close(users, stream);
      yd_filter_destroy(filter);
      return SXE_FAILED;
    }

    wlent = container_of(list_node_first(swin->wlist), yd_wlist_node_t, node);
    useroid = wlent->oid;

    if((r = domx_get(users, useroid, &data))) {
      goto __notfound;
    }
  }

  return SXE_SUCCESS;
 __fini:
  return r;
}

static usrtc_t *rpclist_filter(sxlink_t *co, usrtc_t *fulllist, usrtc_t *filtered)
{
  return fulllist;
}

#undef __SALPREFIX

def_shutdown(session) {
  return 0;
}

def_preinit(session) {
  int r = 0;

  /* init local object storage */
  if(!(ostore = malloc(sizeof(obj_store_t)))) return ENOMEM;
  else r = obj_store_init(ostore);

  _provided_dobjects_list = malloc(sizeof(list_head_t));
  if(!_provided_dobjects_list) return ENOMEM;
  else list_init_head(_provided_dobjects_list);

  /* set global context for local use */
  actx = ctx;

  /* add syntax */
  scm_func_tree_insert(ctx, MODSAUTHSYN_SET_CERTIFICATES_STORE, __sauth_set_store, NULL);
  scm_func_tree_insert(ctx, MODSAUTHSYN_SET_USERS_STORE, __sauth_set_store, NULL);
  scm_func_tree_insert(ctx, MODSAUTHSYN_SET_GROUPS_STORE, __sauth_set_store, NULL);
  scm_func_tree_insert(ctx, MODSAUTHSYN_SET_ROLES_STORE, __sauth_set_store, NULL);
  scm_func_tree_insert(ctx, MODSAUTHSYN_ADD_SHAREDDATAOBJECT, __sauth_add_shareddataobject,
                       (void *)_provided_dobjects_list);

  return r;
}


def_init(session) {
  /* store objects */
  obj_store_set(ostore, "pem-check", (void *)pem_check);
  obj_store_set(ostore, "account-auth", (void *)auth_check);
  obj_store_set(ostore, "rpclist-filter", (void *)rpclist_filter);

  return 0;
}

def_run(session) {
  return 0;
}

def_getobject(session) {
  void *obj = NULL;
  int r = obj_store_get(ostore, oname, &obj);

  if(r)
    return NULL;
  else return obj;
}

