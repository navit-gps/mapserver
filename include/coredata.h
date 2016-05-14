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

#ifndef __MAPSERVER_COREDATA_H__
#define __MAPSERVER_COREDATA_H__

/* this structure used for deal with x.509 certificates only,
 * it might looks like overenginered, but i guess it will be quite
 * useful in near future... for other networking features.
 */

/**
 * pem_t
 * C structure for x509 data
 */
typedef struct __pem_type {
  acc_right_t acc; /** < default access right */
  uint64_t pemid;  /** < serial number of x.509 */
  uint8_t attr;    /** < attribute mask */
  uint32_t uid;    /** < optional assignment to the user */
  char filename[256];  /** < filename of certificate on the file storage */
}__attribute__((packed)) pem_t;

/* attributes for pem */
#define PEM_BLOCKED  (1 << 1) /* access with this certificate blocked */
#define PEM_SYSTEM   (1 << 2) /* this is a system certificate */
#define PEM_USER     (1 << 3) /* user pinned certificate */
#define PEM_MASTER   (1 << 4) /* master i.e. root certificate */
#define PEM_DATA     (1 << 5) /* data, means certificate desired to be used for data services */

#define PEM_SET_SYSTEM_ATTR(n) \
  if(n & PEM_BLOCKED) { n = 0; n |= PEM_BLOCKED; n |= PEM_SYSTEM; } \
  else { n = 0; n |= PEM_SYSTEM; }

#define PEM_SET_USER_ATTR(n) \
  if(n & PEM_BLOCKED) { n = 0; n |= PEM_BLOCKED; n |= PEM_USER; } \
  else { n = 0; n |= PEM_USER; }

#define PEM_SET_MASTER_ATTR(n) \
  if(n & PEM_BLOCKED) { n = 0; n |= PEM_BLOCKED; n |= PEM_MASTER; } \
  else { n = 0; n |= PEM_MASTER; }

#define PEM_SET_DATA_ATTR(n) \
  if(n & PEM_BLOCKED) { n = 0; n |= PEM_BLOCKED; n |= PEM_DATA; } \
  else { n = 0; n |= PEM_DATA; }

typedef struct __user2_type {
  acc_right_t acc;
  uint32_t uid;
  uint32_t gid;
  uint8_t attr;
  uint8_t reserved;
  uint32_t rid;
  char login[32];
  char password[46];
  char salt[32];
  uint64_t expdate;
  uint64_t pemid;
  uint64_t spec;
  uint32_t gids[8];
}__attribute__((packed)) user_t;

typedef struct __group2_type {
  acc_right_t acc;
  uint32_t gid;
  uint32_t rid;
  char name[32];
  uint64_t spec;
}__attribute__((packed)) group_t;



#endif /* __MAPSERVER_COREDATA_H__ */
