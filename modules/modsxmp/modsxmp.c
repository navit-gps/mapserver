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
#include <sxmp/limits.h>
#include <ydaemon/ydaemon.h>

#define MOD_PREFIX  sxmpd

static long __cmp_cstr(const void *a, const void *b);
static int __chack_get_stream(void *, sexp_t *);
static int __chack_get_stream_list(void *, sexp_t *);

struct sxmpd_ins {
  usrtc_t tree;
  int flags;
};

struct sxmpd_node {
  char *name;
  int port;
  char *rootca;
  char *daemonca;
  list_head_t pem_filter;
  list_head_t account_filter;
  list_head_t ondestroy_filter;
  list_head_t onpulse_filter;
  list_head_t rpc_filter;
  usrtc_t rpclist;
  usrtc_t chacks;
  sxhub_t *sys;
  pthread_t master;
  usrtc_node_t node;
};

typedef struct __filter_item filter_item_t;
struct __filter_item {
  void *obj;
  list_node_t node;
};

struct sxmpd_ins *glins = NULL;
usrtc_t *rpclist = NULL;
pthread_t master_thread;

/* channel ack proto implementation */
typedef struct __chack_stream_t {
  char *uuid;
  sxlink_t *co; /* pinned connection */
  usrtc_t *chnls;
  usrtc_node_t *cur;
  usrtc_node_t node;
} chack_stream_t;

static int __openlistener(int port)
{
  int sd;
  struct sockaddr_in addr;

  sd = socket(PF_INET, SOCK_STREAM, 0);
  bzero(&addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if(bind(sd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ) {
    perror("can't bind port");
  } else if(listen(sd, 10) != 0 ) {
    perror("Can't configure listening port");
  }

  return sd;
}

static struct sxmpd_node *__init_instance(const char *name)
{
  struct sxmpd_node *nn = malloc(sizeof(struct sxmpd_node));
  char *nm = strdup(name);
  sxhub_t *sys = malloc(sizeof(sxhub_t));

  if(!nn) {
  __enomem:
    if(nn) free(nn);
    if(nm) free(nm);
    if(sys) free(sys);
    return NULL;
  } else memset(nn, 0, sizeof(struct sxmpd_node));

  if(!nm) goto __enomem;
  else nn->name = nm;

  if(!sys) goto __enomem;
  else {
    if(sxhub_init(sys)) goto __enomem;
    nn->sys = sys;
    sxhub_set_priv(nn->sys, nn); /* set instance */
  }

  /* init various data structures */
  usrtc_node_init(&nn->node, nn);
  list_init_head(&(nn->pem_filter));
  list_init_head(&(nn->account_filter));
  list_init_head(&(nn->rpc_filter));
  list_init_head(&(nn->ondestroy_filter));
  list_init_head(&(nn->onpulse_filter));
  /* rpc list */
  sxmp_rpclist_init(&(nn->rpclist));

  /* chacks init */
  usrtc_init(&nn->chacks, USRTC_SPLAY, 65535, __cmp_cstr);
  /* rpc add TODO: add results */
  sxmp_rpclist_add(&(nn->rpclist), 2, "PC002", NULL);
  sxmp_rpclist_add_function(&(nn->rpclist), 2, "channel-ack-get-stream",
                            __chack_get_stream);
  sxmp_rpclist_add_function(&(nn->rpclist), 2, "channel-ack-get-stream-list",
                            __chack_get_stream_list);

  return nn;
}

/* syntax defines */
#define SXMPDSYN_INTANCE_ADD          "sxmpd-instance-add"
#define SXMPDSYN_SET_PORT             "sxmpd-set-port"
#define SXMPDSYN_SET_ROOTCA           "sxmpd-set-rootca"
#define SXMPDSYN_SET_DAEMONCA         "sxmpd-set-daemonca"
#define SXMPDSYN_PEM_FILTERADD        "sxmpd-pem-filter-add"
#define SXMPDSYN_ACCOUNT_FILTERADD    "sxmpd-account-filter-add"
#define SXMPDSYN_RPC_FILTERADD        "sxmpd-rpc-filter-add"
#define SXMPDSYN_ONDESTROY_FILTERADD  "sxmpd-ondestroy-filter-add"
#define SXMPDSYN_ONPULSE_FILTERADD    "sxmpd-onpulse-filter-add"
#define SXMPDSYN_RPC_CHANNELADD       "sxmpd-rpc-channel-add" /* { */
#define SXMPDSYN_CHANNELID            "id"
#define SXMPDSYN_CHANNELDESC          "desc" /* } */
#define SXMPDSYN_RPC_ADD              "sxmpd-rpc-add"

typedef int (*rpc_func_t)(void *, sexp_t *);

static scret_t __sxmpd_rpc_add(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int state = 0;
  register int idx;
  int chid = -1, r = EINVAL;;
  usrtc_node_t *node;
  struct sxmpd_node *nn;
  sexp_t *isx;
  char *rpcname = NULL, *name = NULL;
  ydc_conf_val_t *rval;
  void *refobj = NULL;
  scret_t rets, refret;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST && state < 4) { RETURN_SRET_IRES(rets, r); }

    switch(state) {
    case 0:
      if(strcmp(isx->val, SXMPDSYN_RPC_ADD)) { RETURN_SRET_IRES(rets, r); }
      else state++;
      break;
    case 1:
      if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }
      name = isx->val;
      state++;
      break;
    case 2:
      if(isx->aty != SEXP_BASIC) { RETURN_SRET_IRES(rets, r); }
      if(strchr(isx->val, '/')) {
        r = ydc_conf_get_val(ctx->values, (const char *)isx->val, &rval);
        if(r) { RETURN_SRET_IRES(rets, r); }
        r = EINVAL;
        /* check value */
        if(rval->type != INT || rval->type != UINT) { RETURN_SRET_IRES(rets, r); }

        chid = *(int *)rval->value;
      } else chid = atoi(isx->val);

      state++;
      break;
    case 3:
      if(strchr(isx->val, '/') && isx->aty == SEXP_BASIC) {
        r = ydc_conf_get_val(ctx->values, (const char *)isx->val, &rval);
        if(r) { RETURN_SRET_IRES(rets, r); }
        r = EINVAL;
        /* check value */
        if(rval->type != STRING) { RETURN_SRET_IRES(rets, r); }

        rpcname = (char *)rval->value;
      } else if(isx->aty == SEXP_DQUOTE) rpcname = isx->val;
      else { RETURN_SRET_IRES(rets, r); }

      state++;
      break;
    case 4:
      if(isx->ty != SEXP_LIST) { RETURN_SRET_IRES(rets, r); }

      refret = yd_eval_sexp(ctx, isx);
      if(refret.type != SCOBJECTPTR) { RETURN_SRET_IRES(rets, r); }
      else refobj = refret.ret;

      state++;
      break;
    default:
      RETURN_SRET_IRES(rets, r);
      break;
    }
  }

  if(chid == -1 || !name) goto __finish;
  if(!rpcname || !refobj) goto __finish;

  node = usrtc_lookup(&(glins->tree), (const void *)name);
  if(node) {
    nn = (struct sxmpd_node *)usrtc_node_getdata(node);

    r = sxmp_rpclist_add_function(&(nn->rpclist), chid, (const char *)rpcname, (rpc_func_t)refobj);
  } else r = ENOENT;

 __finish:
  RETURN_SRET_IRES(rets, r);
}

static scret_t __sxmpd_rpc_channel_add(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int state = 0, vpr = 0;
  register int idx, pidx;
  int chid = -1, r = EINVAL;;
  usrtc_node_t *node;
  struct sxmpd_node *nn;
  sexp_t *isx, *cisx;
  char *desc = NULL, *name = NULL;
  ydc_conf_val_t *rval;
  scret_t rets;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST && state < 2) { RETURN_SRET_IRES(rets, r); }
    switch(state) {
    case 0:
      if(strcmp(isx->val, SXMPDSYN_RPC_CHANNELADD)) { RETURN_SRET_IRES(rets, r); }
      else state++;
      break;
    case 1:
      if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }
      name = isx->val;
      state++;
      break;
    case 2:
      if(isx->ty != SEXP_LIST) { RETURN_SRET_IRES(rets, r); }
      pidx = 0;
      SEXP_ITERATE_LIST(isx, cisx, pidx) {
        if(cisx->ty == SEXP_LIST) { RETURN_SRET_IRES(rets, r); }
        if(!pidx) {
          if(cisx->aty != SEXP_BASIC || cisx->val[0] != ':') { RETURN_SRET_IRES(rets, r); }
          desc = cisx->val + sizeof(char);

          if(!strcmp(desc, SXMPDSYN_CHANNELID)) vpr = 1;
          else if(!strcmp(desc, SXMPDSYN_CHANNELDESC)) vpr = 2;
          else { RETURN_SRET_IRES(rets, r); }

          desc = NULL;
        } else {
          if(vpr > 2) { RETURN_SRET_IRES(rets, r); }

          if(strchr(cisx->val, '/')) { /* we got a variable */
            if(cisx->aty != SEXP_BASIC) { RETURN_SRET_IRES(rets, r); }

            /* check out variable */
            r = ydc_conf_get_val(ctx->values, (const char *)cisx->val, &rval);
            if(r) { RETURN_SRET_IRES(rets, r); }

            /* ok reset error state and assign variable state */
            r = EINVAL;
            vpr += 2;
          } else if(vpr == 1 && cisx->aty != SEXP_BASIC) { RETURN_SRET_IRES(rets, r); } /* syntax check */
          else if(vpr == 2 && cisx->aty != SEXP_DQUOTE) { RETURN_SRET_IRES(rets, r); }

          switch(vpr) {
          case 1: /* directly assign of the channel */
            chid = atoi(cisx->val);
            break;
          case 2: /* directly assign of the channel description */
            desc = cisx->val;
            break;
          case 3: /* id was given via variable */
            if(rval->type != INT || rval->type != UINT) {
              if(desc) free(desc);
              RETURN_SRET_IRES(rets, r);
            }

            chid = *(int *)rval->value;
            break;
          case 4:
            if(rval->type != STRING) { RETURN_SRET_IRES(rets, r); }

            desc = (char *)rval->value; /* just to be safe */
            break;
          }

          vpr++;
        }

      }
    }
  }

  if(!desc || chid == -1) r = EINVAL;
  else r = 0;

  if(!r && !name) r = EINVAL; /* check if name is pointed */

  if(!r) {
    node = usrtc_lookup(&(glins->tree), (const void *)name);
    if(node) {
      nn = (struct sxmpd_node *)usrtc_node_getdata(node);

      r = sxmp_rpclist_add(&(nn->rpclist), chid, (const char *)desc, NULL);
    } else r = ENOENT;
  }

  RETURN_SRET_IRES(rets, r);
}

static scret_t __sxmpd_filter_add(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int state = 0;
  register int idx;
  int filter = -1; /* 0 for pem, 1 for account 2 for rpc 3 for on destroy 4 for on pulse*/
  int r = EINVAL;
  usrtc_node_t *node;
  char *name = NULL;
  void *refobj = NULL;
  sexp_t *isx;
  struct sxmpd_node *nn;
  list_head_t *lhead;
  filter_item_t *nitem;
  scret_t rets, refret;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST && state < 2) { RETURN_SRET_IRES(rets, r); }
    if(!state) {
      if(!strcmp(isx->val, SXMPDSYN_PEM_FILTERADD)) filter = 0;
      else if(!strcmp(isx->val, SXMPDSYN_ACCOUNT_FILTERADD)) filter = 1;
      else if(!strcmp(isx->val, SXMPDSYN_RPC_FILTERADD)) filter = 2;
      else if(!strcmp(isx->val, SXMPDSYN_ONDESTROY_FILTERADD)) filter = 3;
      else if(!strcmp(isx->val, SXMPDSYN_ONPULSE_FILTERADD)) filter = 4;
      else { RETURN_SRET_IRES(rets, r); }

      state++;
    } else {
      switch(state) {
      case 1:
        if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }
        name = isx->val;
        state++;
        break;
      case 2:
        if(isx->ty != SEXP_LIST) { RETURN_SRET_IRES(rets, r); }
        refret = yd_eval_sexp(ctx, isx);
        if(refret.type != SCOBJECTPTR) { RETURN_SRET_IRES(rets, r); }
        else refobj = refret.ret;
        break;
      default:
        RETURN_SRET_IRES(rets, r);
        break;
      }
    }
  }

  if(!refobj || !name) { RETURN_SRET_IRES(rets, r); }

  /* ok here we go */
  node = usrtc_lookup(&(glins->tree), (const void *)name);
  if(!node) r = ENOENT;
  else {
    nn = (struct sxmpd_node *)usrtc_node_getdata(node);

    /* decide the filter */
    switch(filter) {
    case 0: lhead = &(nn->pem_filter); break;
    case 1: lhead = &(nn->account_filter); break;
    case 2: lhead = &(nn->rpc_filter); break;
    case 3: lhead = &(nn->ondestroy_filter); break;
    case 4: lhead = &(nn->onpulse_filter); break;
    }

    if(!(nitem = malloc(sizeof(filter_item_t))))
      r = ENOMEM;
    else { /* let's a deal */
      r = 0;
      nitem->obj = refobj;
      list_init_node(&nitem->node);
      list_add2tail(lhead, &nitem->node);
    }
  }

  RETURN_SRET_IRES(rets, r);
}

static scret_t __sxmpd_set_ca(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int state = 0;
  register int idx;
  scret_t rets;
  sexp_t *isx;
  char *isname = NULL, *tt = NULL, *ca = NULL;
  usrtc_node_t *node;
  int r = EINVAL, cc = 0;
  struct sxmpd_node *nn;
  ydc_conf_val_t *cval;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST) { RETURN_SRET_IRES(rets, r); }
    if(!state) {
      if(!strcmp(isx->val, SXMPDSYN_SET_ROOTCA)) cc = 1;
      else if(!strcmp(isx->val, SXMPDSYN_SET_DAEMONCA)) cc = 2;
      else { RETURN_SRET_IRES(rets, r); }

      state++;
    } else {
      switch(state) {
      case 1:
        if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }
        isname = isx->val;
        state++;
        break;
      case 2:
        if(isx->aty == SEXP_BASIC) {
          if(!strchr(isx->val, '/')) { RETURN_SRET_IRES(rets, r); }

          tt = isx->val;
        } else if(isx->aty == SEXP_DQUOTE) ca = strdup(isx->val);
        else { RETURN_SRET_IRES(rets, r); }
        break;
      default:
        if(ca) free(ca);
        RETURN_SRET_IRES(rets, r);
        break;
      }
    }
  }

  if(!ca && tt) { /* lookup for variable */
    r = ydc_conf_get_val(yd_ctx_values(ctx), tt, &cval);
    if(!r) {
      if(cval->type != STRING) r = EINVAL;
      else {
        ca = (char *)cval->value;
        r = 0;
      }
    }
  } else if(!ca) r = ENOMEM;
  else r = 0;

  if(!r) {
    node = usrtc_lookup(&(glins->tree), (const void *)isname);
    if(!node) r = ENOENT;
    else {
      nn = (struct sxmpd_node *)usrtc_node_getdata(node);
      if(cc == 1) nn->rootca = ca;
      else if(cc == 2) nn->daemonca = ca;
    }
  }

  if(r && ca) free(ca);

  RETURN_SRET_IRES(rets, r);
}

static scret_t __sxmpd_set_port(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int state = 0;
  register int idx;
  int port = 0;
  scret_t rets;
  sexp_t *isx;
  char *isname = NULL, *tt = NULL;
  usrtc_node_t *node;
  int r = EINVAL;
  struct sxmpd_node *nn;
  ydc_conf_val_t *cval;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST) { RETURN_SRET_IRES(rets, r); }
    if(!state) {
      if(strcmp(isx->val, SXMPDSYN_SET_PORT)) { RETURN_SRET_IRES(rets, r); }
      else state++;
    } else {
      switch(state) {
      case 1:
        if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }
        isname = isx->val;
        state++;
        break;
      case 2:
        if(isx->aty != SEXP_BASIC) { RETURN_SRET_IRES(rets, r); }
        if(!strchr(isx->val, '/')) port = atoi(isx->val);
        else tt = isx->val;
        break;
      default:
        RETURN_SRET_IRES(rets, r);
        break;
      }
    }
  }

  if(!port && tt) { /* lookup for variable */
    r = ydc_conf_get_val(yd_ctx_values(ctx), tt, &cval);
    if(!r) {
      if((cval->type != INT) || (cval->type != UINT)) r = EINVAL;
      else {
        port = *(int *)cval->value;
        r = 0;
      }
    }
  } else r = 0;

  if(!r) {
    node = usrtc_lookup(&(glins->tree), (const void *)isname);
    if(!node) r = ENOENT;
    else {
      nn = (struct sxmpd_node *)usrtc_node_getdata(node);
      nn->port = port;
    }
  }

  RETURN_SRET_IRES(rets, r);
}

static scret_t __sxmpd_instance_add(yd_context_t *ctx, sexp_t *sx, void *priv)
{
  register int state = 0;
  register int idx;
  scret_t rets;
  sexp_t *isx;
  char *isname = NULL;
  usrtc_node_t *node;
  int r = EINVAL;
  struct sxmpd_node *nn;

  SEXP_ITERATE_LIST(sx, isx, idx) {
    if(isx->ty == SEXP_LIST) { RETURN_SRET_IRES(rets, r); }
    if(!state) {
      if(strcmp(isx->val, SXMPDSYN_INTANCE_ADD)) { RETURN_SRET_IRES(rets, r); }
      else state++;
    } else {
      if(state > 1) { RETURN_SRET_IRES(rets, r); }
      if(isx->aty != SEXP_SQUOTE) { RETURN_SRET_IRES(rets, r); }
      isname = isx->val;
      state++;
    }
  }

  r = 0;

  node = usrtc_lookup(&(glins->tree), (const void *)isname);
  if(node) r = EEXIST;
  else {
    nn = __init_instance((const char *)isname);
    if(!nn) r = ENOMEM;
  }

  if(!r) usrtc_insert(&(glins->tree), &(nn->node), (const void *)nn->name);

  RETURN_SRET_IRES(rets, r);
}

static void *__sxmpd_listen(void *arg)
{
  struct sxmpd_node *in = (struct sxmpd_node *)arg;
  sxlink_t *co;
  int srv;

  if(!in) return NULL;

  srv = __openlistener(in->port);

  while(1) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    int client = accept(srv, (struct sockaddr*)&addr, &len); /* accept connection as usual */
    co = sxlink_master_accept(in->sys, client, (struct in_addr *)&addr); /* create
                                                                              connection,
                                                                              that's all */
    if(!co) {
      fprintf(stderr, "Cannot accept link (%d)\n", errno);
    }
  }

  return NULL;
}

/* function types */
typedef int(*creds_check_t)(sxlink_t *);
typedef void(*ondestroy_t)(sxlink_t *);
typedef int(*onpulse_t)(sxlink_t *, sexp_t *);

static void __on_pulse(sxlink_t *co, sexp_t *sx)
{
  struct sxmpd_node *nn = (struct sxmpd_node *)sxhub_get_priv(co->hub);
  list_head_t *lhead = &nn->onpulse_filter;
  list_node_t *iter, *siter;
  filter_item_t *filter;
  onpulse_t func;

  /* pass our filter */
  list_for_each_safe(lhead, iter, siter) {
    filter = container_of(iter, filter_item_t, node);
    func = (onpulse_t)filter->obj;
    if(func(co, sx)) break; /* we will stop here */
  }

  return;
}

static int __validate_sslpem(sxlink_t *co)
{
  unsigned long pemserial = co->pctx->certid;
  struct sxmpd_node *nn = (struct sxmpd_node *)sxhub_get_priv(co->hub);
  list_head_t *lhead = &nn->pem_filter;
  list_node_t *iter, *siter;
  int r = 0;
  filter_item_t *filter;
  creds_check_t func;

  if(!pemserial) return 1; /* no pem (don't know any cases ... - failed)*/

  /* pass our filter */
  list_for_each_safe(lhead, iter, siter) {
    filter = container_of(iter, filter_item_t, node);
    func = (creds_check_t)filter->obj;
    r = func(co);
    if(r) return 1;
  }

  return r;
}

static int __secure_check(sxlink_t *co)
{
  char *login = co->pctx->login;
  char *passwd = co->pctx->passwd;
  struct sxmpd_node *nn = (struct sxmpd_node *)sxhub_get_priv(co->hub);
  list_head_t *lhead = &nn->account_filter;
  list_node_t *iter, *siter;
  int r = SXE_SUCCESS;
  filter_item_t *filter;
  creds_check_t func;

  if(!login || !passwd) return 1;

  /* pass our filter */
  list_for_each_safe(lhead, iter, siter) {
    filter = container_of(iter, filter_item_t, node);
    func = (creds_check_t)filter->obj;
    r = func(co);
    if(r) return SXE_FAILED;
  }

  return r;
}

static void __ondestroy(sxlink_t *co)
{
  struct sxmpd_node *nn = (struct sxmpd_node *)sxhub_get_priv(co->hub);
  list_head_t *lhead = &nn->ondestroy_filter;
  list_node_t *iter, *siter;
  filter_item_t *filter;
  ondestroy_t func;

  /* pass our filter */
  list_for_each_safe(lhead, iter, siter) {
    filter = container_of(iter, filter_item_t, node);
    func = (ondestroy_t)filter->obj;
    func(co);
  }

  return;
}

typedef usrtc_t *(*rpclist_check_t)(sxlink_t *, usrtc_t *, usrtc_t *);

static usrtc_t *__rpcvalidity(sxlink_t *c)
{
  struct sxmpd_node *nn = (struct sxmpd_node *)sxhub_get_priv(c->hub);
  list_head_t *lhead = &nn->rpc_filter;
  list_node_t *iter, *siter;
  filter_item_t *filter;
  usrtc_t *rpclist_orig = &(nn->rpclist);
  usrtc_t *rpclist_actual = NULL;
  rpclist_check_t func;

  if(list_is_empty(lhead)) return rpclist_orig;

  /* pass our filter */
  list_for_each_safe(lhead, iter, siter) {
    filter = container_of(iter, filter_item_t, node);
    func = (rpclist_check_t)filter->obj;
    rpclist_actual = func(c, rpclist_orig, rpclist_actual);
  }

  return rpclist_actual;
}

static long __cmp_cstr(const void *a, const void *b)
{
  return strcmp((const char *)a, (const char *)b);
}

def_shutdown(sxmpd) {
  if(glins) free(glins);
  return 0;
}

def_preinit(sxmpd) {
  if(!glins) {
    glins = malloc(sizeof(struct sxmpd_ins));
    if(!glins) return ENOMEM;
    else memset(glins, 0, sizeof(struct sxmpd_ins));

    usrtc_init(&glins->tree, USRTC_REDBLACK, 16, __cmp_cstr);

    sxmp_init();

    /* add special functions */
    scm_func_tree_insert(ctx, SXMPDSYN_INTANCE_ADD, __sxmpd_instance_add, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_SET_PORT, __sxmpd_set_port, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_SET_ROOTCA, __sxmpd_set_ca, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_SET_DAEMONCA, __sxmpd_set_ca, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_PEM_FILTERADD, __sxmpd_filter_add, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_RPC_FILTERADD, __sxmpd_filter_add, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_ONDESTROY_FILTERADD, __sxmpd_filter_add, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_ONPULSE_FILTERADD, __sxmpd_filter_add, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_ACCOUNT_FILTERADD, __sxmpd_filter_add, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_RPC_CHANNELADD, __sxmpd_rpc_channel_add, NULL);
    scm_func_tree_insert(ctx, SXMPDSYN_RPC_ADD, __sxmpd_rpc_add, NULL);
  }

  return 0;
}

static int __instance_init(struct sxmpd_node *nn)
{
  int r = 0;

  r = sxhub_setsslserts(nn->sys, nn->rootca, nn->daemonca, nn->daemonca);
  if(r) goto __fini;

  /* security related functions */
  sxhub_set_authcheck(nn->sys, __secure_check);
  sxhub_set_sslvalidate(nn->sys, __validate_sslpem);

  /* setup special list */
  sxhub_set_rpcvalidator(nn->sys, __rpcvalidity);

  /* on destroy callback, when connection will be dead we will call it */
  sxhub_set_ondestroy(nn->sys, __ondestroy);

  /* on pulse callback */
  sxhub_set_onpulse(nn->sys, __on_pulse);

 __fini:
  return r;
}

static int __instance_run(struct sxmpd_node *nn)
{
  int r = 0;

  r = pthread_create(&nn->master, NULL, __sxmpd_listen, nn);

  pthread_detach(nn->master);

  return r;
}

def_init(sxmpd) {
  OpenSSL_add_all_digests();

  return 0;
}

def_run(sxmpd) {
  usrtc_t *tree = &(glins->tree);
  usrtc_node_t *node = NULL;
  struct sxmpd_node *nn = NULL;

  for(node = usrtc_first(tree); node != NULL; node = usrtc_next(tree, node)) {
    nn = (struct sxmpd_node *)usrtc_node_getdata(node);
    if(!__instance_init(nn)) __instance_run(nn);
  }

  return 0;
}

def_getobject(sxmpd) {
  return NULL;
}

/* channel ack implementation */
static int __chack_get_stream(void *_m, sexp_t *sx)
{
  sxmsg_t *msg = (sxmsg_t *)_m;
  int r = 0;
  usrtc_t *_acstree;
  sxlink_t *co = msg->pch->link;
  struct sxmpd_node *snde = sxhub_get_priv(co->hub);
  usrtc_node_t *node = NULL;
  chack_stream_t *chack;

  if(!snde) { r = EIO; goto __fini; }

  _acstree = &snde->chacks;

  if(!(node = usrtc_lookup(_acstree, (void *)co->uuid))) {
    /* we should create it then */
    if(!(chack = malloc(sizeof(chack_stream_t)))) { r = ENOMEM; goto __fini; }
    /* init chackyy */
    chack->uuid = co->uuid;
    chack->co = co;
    chack->cur = NULL;
    chack->chnls = &snde->rpclist;
    usrtc_node_init(&chack->node, chack);

    /* ok, insert it */
    usrtc_insert(_acstree, &chack->node, (const void *)chack->uuid);
  }

 __fini:
  return sxmsg_return(msg, r);
}

#define _CTRL_EOL  0xabcd0104

static int __chack_get_stream_list(void *_m, sexp_t *sx)
{
  sxmsg_t *msg = (sxmsg_t *)_m;
  int r = 0, i = 0;
  usrtc_t *_acstree;
  sxlink_t *co = msg->pch->link;
  struct sxmpd_node *snde = sxhub_get_priv(co->hub);
  usrtc_node_t *node = NULL;
  chack_stream_t *chack;
  void *_cp = (void *)_CTRL_EOL;
  rpc_typed_list_t *lst;
  char *tbuf = sxmsg_rapidbuf(msg);
  size_t ln = 0;

  if(!snde) { r = EIO; goto __fini; }

  _acstree = &snde->chacks;

  if(!(node = usrtc_lookup(_acstree, (void *)co->uuid))) {
    r = ENOENT; goto __fini;
  } else chack = usrtc_node_getdata(node);

  if(!chack->cur) node = usrtc_first(chack->chnls);
  else if(chack->cur == (usrtc_node_t *)_cp) {
    chack->cur = NULL;
    r = 0; goto __fini; /* end of list */
  }

  ln += snprintf(tbuf + ln, MAX_RBBUF_LEN - ln, "(channel-list ");
  for(;node != NULL; node = usrtc_next(chack->chnls, node)) {
    lst = (rpc_typed_list_t *)usrtc_node_getdata(node);
    ln += snprintf(tbuf + ln, MAX_RBBUF_LEN - ln, "((:tid %d)(:desc \"%s\"))",
                   lst->type_id, lst->description);
    i++;    if(i == 100) break;
  }
  ln += snprintf(tbuf + ln, MAX_RBBUF_LEN - ln, ")");

  if(!node) chack->cur = _cp;

  return sxmsg_rreply(msg, ln);

 __fini:
  return sxmsg_return(msg, r);
}

#undef _CTRL_EOL

