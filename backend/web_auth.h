#ifndef AQUA_WEB_AUTH_H
#define AQUA_WEB_AUTH_H
#include "web_support.h"
int web_authenticate(Database *db,const char *cookie,WebUser *user);
/* Auth route response optionally sets/revokes cookie. Must be at least 256 bytes. */
int web_auth_route(Database *db,const WebUser *user,const WebRequest *request,
                   const char *cookie,WebResult *result,char set_cookie[256]);
#endif
