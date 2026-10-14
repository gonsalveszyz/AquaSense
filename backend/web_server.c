#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "web_auth.h"
#include <openssl/crypto.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define HEADER_LIMIT 16384
#define BODY_LIMIT 16384
typedef struct {
    char method[8],path[2049],query[2049],cookie[2049],csrf[129],origin[256],content_type[128];
    unsigned char body[BODY_LIMIT+1];size_t body_length;
} HttpRequest;
static volatile sig_atomic_t stopping=0;
static int listener=-1;
static int cleanup_failed=0;
static unsigned port=8080;
static char frontend[PATH_MAX];
static void stop_server(int signal_number) {(void)signal_number;stopping=1;if(listener>=0) close(listener);listener=-1;}
static int close_database(Database *db) {
    if(db_close(db)||db_close(db)) return 1;
    fprintf(stderr,"Liberação ODBC não confirmada após duas tentativas; servidor encerrado para preservar os recursos.\n");
    cleanup_failed=1;stop_server(0);return 0;
}
static int64_t milliseconds(void) {
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now)) return 0;
    return (int64_t)now.tv_sec*1000+now.tv_nsec/1000000;
}
static ssize_t receive_before(int fd,void *data,size_t capacity,int64_t deadline) {
    for(;;) {
        int64_t remaining=deadline-milliseconds();if(remaining<=0) {errno=EAGAIN;return -1;}
        struct pollfd item={fd,POLLIN,0};int ready=poll(&item,1,(int)remaining);
        if(ready<0&&errno==EINTR) continue;if(ready<=0) {errno=EAGAIN;return -1;}
        ssize_t count=recv(fd,data,capacity,0);if(count<0&&errno==EINTR) continue;return count;
    }
}
static int send_all(int fd,const void *data,size_t length) {
    const unsigned char *p=data;
    while(length) {ssize_t n=send(fd,p,length,0);if(n<0&&errno==EINTR) continue;if(n<=0) return 0;p+=n;length-=(size_t)n;}
    return 1;
}
static const char *reason(int status) {
    switch(status) {
        case 200:return "OK";case 201:return "Created";case 400:return "Bad Request";
        case 401:return "Unauthorized";case 403:return "Forbidden";case 404:return "Not Found";
        case 405:return "Method Not Allowed";case 408:return "Request Timeout";case 409:return "Conflict";
        case 413:return "Content Too Large";case 414:return "URI Too Long";case 415:return "Unsupported Media Type";
        case 422:return "Unprocessable Content";
        case 429:return "Too Many Requests";case 431:return "Request Header Fields Too Large";
        case 503:return "Service Unavailable";default:return "Internal Server Error";
    }
}
static void respond(int fd,int status,const char *type,const char *body,size_t length,const char *cookie,int head) {
    char headers[2048];
    int n=snprintf(headers,sizeof headers,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'\r\n"
        "%s%s%s\r\n",status,reason(status),type,length,
        cookie&&*cookie?"Set-Cookie: ":"",cookie&&*cookie?cookie:"",cookie&&*cookie?"\r\n":"");
    if(n<=0||(size_t)n>=sizeof headers) return;
    if(send_all(fd,headers,(size_t)n)&&!head&&length) (void)send_all(fd,body,length);
}
static void json_response(int fd,WebResult *r,const char *cookie) {
    json_object *envelope=json_object_new_object();
    if(!r->status) web_error(r,500,"ERRO_INTERNO","Não foi possível concluir a solicitação.");
    json_object_object_add(envelope,"data",r->data);r->data=NULL;
    json_object *error=NULL;
    if(r->code) {
        error=json_object_new_object();json_object_object_add(error,"code",json_object_new_string(r->code));
        json_object_object_add(error,"message",json_object_new_string(r->message));
    }
    json_object_object_add(envelope,"error",error);
    const char *body=json_object_to_json_string_ext(envelope,JSON_C_TO_STRING_PLAIN);
    respond(fd,r->status,"application/json; charset=utf-8",body,strlen(body),cookie,0);json_object_put(envelope);
}
static void http_error(int fd,int status,const char *message) {
    WebResult r={0};web_error(&r,status,"REQUISICAO_INVALIDA",message);json_response(fd,&r,NULL);
}
static int copy_header(char *out,size_t capacity,const char *value) {
    size_t n=strlen(value);if(n>=capacity) return 0;memcpy(out,value,n+1);return 1;
}
static int allowed_host(const char *host) {
    char a[64],b[64];snprintf(a,sizeof a,"localhost:%u",port);snprintf(b,sizeof b,"127.0.0.1:%u",port);
    return !strcasecmp(host,a)||!strcmp(host,b);
}
static int allowed_origin(const char *origin) {
    return !strncmp(origin,"http://",7)&&allowed_host(origin+7);
}
static int read_request(int fd,HttpRequest *r) {
    unsigned char buffer[HEADER_LIMIT+BODY_LIMIT+1];size_t received=0,header_length=0;
    int64_t deadline=milliseconds()+5000;
    memset(r,0,sizeof *r);
    while(!header_length) {
        if(received>=HEADER_LIMIT) return 431;
        ssize_t n=receive_before(fd,buffer+received,sizeof buffer-1-received,deadline);
        if(n<0&&errno==EINTR) continue;
        if(n<=0) return 408;
        received+=(size_t)n;buffer[received]=0;
        unsigned char *end=(unsigned char *)strstr((char *)buffer,"\r\n\r\n");
        if(end) header_length=(size_t)(end-buffer)+4;
        if(memchr(buffer,0,header_length?header_length:received)) return 400;
    }
    if(header_length>HEADER_LIMIT) return 431;
    char headers[HEADER_LIMIT+1];memcpy(headers,buffer,header_length);headers[header_length]=0;
    char *line_end=strstr(headers,"\r\n");if(!line_end) return 400;*line_end=0;
    char target[4098],version[16],extra;
    if(sscanf(headers,"%7s %4097s %15s %c",r->method,target,version,&extra)!=3||strcmp(version,"HTTP/1.1")) return 400;
    if(strcmp(r->method,"GET")&&strcmp(r->method,"HEAD")&&strcmp(r->method,"POST")&&strcmp(r->method,"PATCH")) return 405;
    if(target[0]!='/'||strchr(target,'#')) return 400;
    for(const unsigned char *p=(const unsigned char *)target;*p;p++) if(*p<=32||*p>=127) return 400;
    char *query=strchr(target,'?');if(query) *query++=0;
    if(strlen(target)>=sizeof r->path||(query&&strlen(query)>=sizeof r->query)) return 414;
    strcpy(r->path,target);if(query) strcpy(r->query,query);
    int host_seen=0,length_seen=0,type_seen=0,cookie_seen=0,csrf_seen=0,origin_seen=0;
    size_t content_length=0;
    for(char *line=line_end+2;*line;line=line_end+2) {
        line_end=strstr(line,"\r\n");if(!line_end) return 400;if(line_end==line) break;*line_end=0;
        if(*line==' '||*line=='\t') return 400;
        char *colon=strchr(line,':');if(!colon||colon==line) return 400;*colon=0;
        for(const unsigned char *p=(const unsigned char *)line;*p;p++) if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-')) return 400;
        char *value=colon+1;while(*value==' '||*value=='\t') value++;
        char *tail=value+strlen(value);while(tail>value&&(tail[-1]==' '||tail[-1]=='\t')) *--tail=0;
        for(const unsigned char *p=(const unsigned char *)value;*p;p++) if((*p<32&&*p!='\t')||*p==127) return 400;
        if(!strcasecmp(line,"Host")) {if(host_seen++||!allowed_host(value)) return 400;}
        else if(!strcasecmp(line,"Transfer-Encoding")||!strcasecmp(line,"Expect")) return 400;
        else if(!strcasecmp(line,"Content-Length")) {
            if(length_seen++||!*value) return 400;
            for(const char *p=value;*p;p++) {if(*p<'0'||*p>'9') return 400;if(content_length>BODY_LIMIT) return 413;content_length=content_length*10+(size_t)(*p-'0');}
            if(content_length>BODY_LIMIT) return 413;
        } else if(!strcasecmp(line,"Content-Type")) {if(type_seen++||!copy_header(r->content_type,sizeof r->content_type,value)) return 400;}
        else if(!strcasecmp(line,"Cookie")) {if(cookie_seen++||!copy_header(r->cookie,sizeof r->cookie,value)) return 400;}
        else if(!strcasecmp(line,"X-CSRF-Token")) {if(csrf_seen++||!copy_header(r->csrf,sizeof r->csrf,value)) return 400;}
        else if(!strcasecmp(line,"Origin")) {if(origin_seen++||!copy_header(r->origin,sizeof r->origin,value)) return 400;}
    }
    if(!host_seen) return 400;
    int mutation=!strcmp(r->method,"POST")||!strcmp(r->method,"PATCH");
    if((mutation&&!origin_seen)||(origin_seen&&!allowed_origin(r->origin))) return 403;
    if(mutation&&!length_seen) return 400;
    if(!mutation&&content_length) return 400;
    if(mutation&&strcasecmp(r->content_type,"application/json")&&strcasecmp(r->content_type,"application/json; charset=utf-8")) return 415;
    size_t body_received=received-header_length;if(body_received>content_length) return 400;
    if(body_received) memcpy(r->body,buffer+header_length,body_received);
    while(body_received<content_length) {
        ssize_t n=receive_before(fd,r->body+body_received,content_length-body_received,deadline);
        if(n<0&&errno==EINTR) continue;if(n<=0) return 408;body_received+=(size_t)n;
    }
    r->body[content_length]=0;r->body_length=content_length;
    if(memchr(r->body,0,content_length)) return 400;
    return 200;
}
static int unique_members(const unsigned char *source,size_t length) {
    json_object *seen[17]={0};int depth=0,ok=1;
    for(size_t i=0;ok&&i<length;i++) {
        unsigned char ch=source[i];
        if(ch=='{'&&depth<16) {seen[++depth]=json_object_new_object();if(!seen[depth]) ok=0;}
        else if(ch=='{'||ch=='['||ch==']') ok=0;
        else if(ch=='}') {if(depth<=0) ok=0;else {json_object_put(seen[depth]);seen[depth--]=NULL;}}
        else if(ch=='"') {
            size_t start=i;int escape=0,closed=0;
            while(++i<length) {if(escape) escape=0;else if(source[i]=='\\') escape=1;else if(source[i]=='"') {closed=1;break;}}
            if(!closed) {ok=0;break;}
            size_t next=i+1;while(next<length&&(source[next]==' '||source[next]=='\t'||source[next]=='\r'||source[next]=='\n')) next++;
            json_tokener *tok=json_tokener_new();
            json_object *value=tok?json_tokener_parse_ex(tok,(const char *)source+start,(int)(i-start+1)):NULL;
            if(!value||!json_object_is_type(value,json_type_string)) ok=0;
            const char *text=value?json_object_get_string(value):NULL;
            if(text&&(size_t)json_object_get_string_len(value)!=strlen(text)) ok=0;
            if(ok&&next<length&&source[next]==':') {
                json_object *existing=NULL;
                if(depth<=0||json_object_object_get_ex(seen[depth],text,&existing)) ok=0;
                else json_object_object_add(seen[depth],text,json_object_new_boolean(1));
            }
            if(value) json_object_put(value);if(tok) json_tokener_free(tok);
        }
    }
    for(int i=1;i<=16;i++) if(seen[i]) json_object_put(seen[i]);
    return ok&&depth==0;
}
static int four_hex(const unsigned char *text,unsigned *out) {
    unsigned value=0;
    for(int i=0;i<4;i++) {
        unsigned ch=text[i],digit;
        if(ch>='0'&&ch<='9') digit=ch-'0';else if(ch>='a'&&ch<='f') digit=ch-'a'+10;else if(ch>='A'&&ch<='F') digit=ch-'A'+10;else return 0;
        value=value*16+digit;
    }
    *out=value;return 1;
}
static int valid_unicode_escapes(const unsigned char *source,size_t length) {
    int quoted=0;
    for(size_t i=0;i<length;i++) {
        if(source[i]=='"') quoted=!quoted;
        else if(quoted&&source[i]=='\\') {
            if(++i>=length) return 0;
            if(source[i]=='u') {
                unsigned code=0;if(i+4>=length||!four_hex(source+i+1,&code)) return 0;i+=4;
                if(code>=0xdc00&&code<=0xdfff) return 0;
                if(code>=0xd800&&code<=0xdbff) {
                    unsigned low=0;
                    if(i+6>=length||source[i+1]!='\\'||source[i+2]!='u'||!four_hex(source+i+3,&low)||low<0xdc00||low>0xdfff) return 0;
                    i+=6;
                }
            }
        }
    }
    return !quoted;
}
static json_object *parse_body(const HttpRequest *r) {
    if(!r->body_length||!web_utf8_valid(r->body,r->body_length)||!valid_unicode_escapes(r->body,r->body_length)) return NULL;
    json_tokener *tok=json_tokener_new_ex(16);if(!tok) return NULL;
    json_tokener_set_flags(tok,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);
    json_object *body=json_tokener_parse_ex(tok,(const char *)r->body,(int)r->body_length);
    int ok=json_tokener_get_error(tok)==json_tokener_success&&body&&json_object_is_type(body,json_type_object);
    size_t end=json_tokener_get_parse_end(tok);
    for(size_t i=end;ok&&i<r->body_length;i++) if(r->body[i]!=' '&&r->body[i]!='\t'&&r->body[i]!='\r'&&r->body[i]!='\n') ok=0;
    json_tokener_free(tok);
    /* Settings use nested objects. Reject repeated decoded keys at every level,
     * embedded NUL strings and arrays (none of the mutation DTOs use arrays). */
    if(ok&&!unique_members(r->body,r->body_length)) ok=0;
    if(!ok) {if(body) json_object_put(body);return NULL;}return body;
}
static int static_file(int fd,const HttpRequest *request) {
    const char *name=NULL,*type=NULL;
    if(!strcmp(request->path,"/")||!strcmp(request->path,"/index.html")) {name="index.html";type="text/html; charset=utf-8";}
    else if(!strcmp(request->path,"/styles.css")) {name="styles.css";type="text/css; charset=utf-8";}
    else if(!strcmp(request->path,"/app.js")) {name="app.js";type="text/javascript; charset=utf-8";}
    else if(!strcmp(request->path,"/favicon.svg")) {name="favicon.svg";type="image/svg+xml";}
    else return 0;
    if(strcmp(request->method,"GET")&&strcmp(request->method,"HEAD")) {http_error(fd,405,"Use GET para arquivos da interface.");return 1;}
    char path[PATH_MAX];int n=snprintf(path,sizeof path,"%s/%s",frontend,name);
    if(n<0||(size_t)n>=sizeof path) {http_error(fd,404,"Arquivo não encontrado.");return 1;}
    int file=open(path,O_RDONLY|O_NOFOLLOW);struct stat info;
    if(file<0||fstat(file,&info)!=0||!S_ISREG(info.st_mode)||info.st_size<0||info.st_size>2*1024*1024) {if(file>=0) close(file);http_error(fd,404,"Arquivo não encontrado.");return 1;}
    size_t length=(size_t)info.st_size;char *body=malloc(length+1);size_t used=0;
    if(!body) {close(file);http_error(fd,503,"Interface indisponível.");return 1;}
    while(used<length) {ssize_t count=read(file,body+used,length-used);if(count<0&&errno==EINTR) continue;if(count<=0) break;used+=(size_t)count;}
    close(file);if(used==length) respond(fd,200,type,body,length,NULL,!strcmp(request->method,"HEAD"));else http_error(fd,503,"Interface indisponível.");free(body);return 1;
}
static void handle_client(int fd) {
    struct timeval timeout={5,0};(void)setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);(void)setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
    HttpRequest http;int status=read_request(fd,&http);
    if(status!=200) {http_error(fd,status,"Solicitação HTTP fora do formato aceito pelo servidor local.");return;}
    if(static_file(fd,&http)) return;
    if(strncmp(http.path,"/api/",5)) {http_error(fd,404,"Rota não encontrada.");return;}
    if(!strcmp(http.method,"HEAD")) {http_error(fd,405,"Use GET para consultas da API.");return;}
    json_object *body=NULL;
    if(!strcmp(http.method,"POST")||!strcmp(http.method,"PATCH")) {
        body=parse_body(&http);if(!body) {http_error(fd,400,"Envie um objeto JSON válido, sem campos repetidos, arrays ou caracteres nulos.");return;}
    }
    WebRequest request={http.method,http.path,http.query,body};WebResult result={0};WebUser user={0};char set_cookie[256]={0};
    Database db=DB_INITIALIZER;
    if(!db_open_from_env(&db)) {web_error(&result,503,"BANCO_INDISPONIVEL","SQL Server indisponível. Verifique o servidor local.");goto done;}
    if(!strcmp(http.path,"/api/v1/health")&&!strcmp(http.method,"GET")) {
        json_object *names=web_query(&db,"SELECT DB_NAME() AS database_name",NULL,0),*name=NULL;
        if(!names||json_object_array_length(names)!=1||!json_object_object_get_ex(json_object_array_get_idx(names,0),"database_name",&name)) {
            if(names) json_object_put(names);web_error(&result,503,"BANCO_INDISPONIVEL","Não foi possível identificar o banco.");goto done;
        }
        json_object *data=json_object_new_object();json_object_object_add(data,"servico",json_object_new_string("AquaSense"));
        json_object_object_add(data,"database",json_object_get(name));json_object_put(names);
        json_object_object_add(data,"banco",json_object_new_string("conectado"));json_object_object_add(data,"esquema_cli",json_object_new_int(3));json_object_object_add(data,"esquema_web",json_object_new_int(1));web_ok(&result,200,data);goto done;
    }
    int public_route=(!strcmp(http.path,"/api/v1/auth/setup")&&!strcmp(http.method,"GET"))||((!strcmp(http.path,"/api/v1/auth/login")||!strcmp(http.path,"/api/v1/auth/register"))&&!strcmp(http.method,"POST"));
    if(!public_route) {
        int authenticated=web_authenticate(&db,http.cookie,&user);
        if(authenticated<0) {web_error(&result,503,"BANCO_INDISPONIVEL","Não foi possível verificar a sessão.");goto done;}
        if(!authenticated) {web_error(&result,401,"AUTENTICACAO_NECESSARIA","Entre na sua conta para continuar.");goto done;}
        if(body&&(strlen(http.csrf)!=64||CRYPTO_memcmp(http.csrf,user.csrf,64))) {web_error(&result,403,"CSRF_INVALIDO","Recarregue a página antes de salvar.");goto done;}
    }
    if(!web_auth_route(&db,&user,&request,http.cookie,&result,set_cookie)&&!web_domain_route(&db,&user,&request,&result)) web_error(&result,404,"ROTA_NAO_ENCONTRADA","Rota não encontrada.");
done:
    if(!close_database(&db)) {
        if(result.data) json_object_put(result.data);result.data=NULL;set_cookie[0]=0;
        web_error(&result,503,"LIMPEZA_INCOMPLETA","A operação não pôde ser confirmada. Consulte o histórico antes de reenviar; o servidor precisa ser reiniciado.");
    } else if(body&&result.status==503) {
        snprintf(result.message,sizeof result.message,"A operação não pôde ser confirmada. Consulte o histórico ou tente entrar na conta antes de reenviar os dados.");
    }
    if(body) json_object_put(body);json_response(fd,&result,set_cookie);
}
int main(int argc,char **argv) {
    const char *directory="frontend";int check=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--frontend")&&i+1<argc) directory=argv[++i];
        else if(!strcmp(argv[i],"--port")&&i+1<argc) {
            char *end=NULL;errno=0;unsigned long value=strtoul(argv[++i],&end,10);
            if(errno||!end||*end||value<1024||value>65535) {fprintf(stderr,"Porta inválida (1024..65535).\n");return 2;}port=(unsigned)value;
        } else if(!strcmp(argv[i],"--check")) check=1;
        else if(!strcmp(argv[i],"--help")) {puts("Uso: aquasense_api [--frontend caminho] [--port 8080] [--check]\nServidor de desenvolvimento limitado a 127.0.0.1; conexão SQL Server ODBC/TLS pelo ambiente.");return 0;}
        else {fprintf(stderr,"Argumento inválido. Use --help.\n");return 2;}
    }
    if(!realpath(directory,frontend)) {fprintf(stderr,"Diretório da interface ausente.\n");return 1;}
    Database db=DB_INITIALIZER;
    if(!db_open_from_env(&db)) {(void)close_database(&db);return 1;}
    json_object *rows=web_query(&db,"SELECT MAX(version) AS version FROM dbo.WebSchemaVersions",NULL,0);
    json_object *version=NULL;
    int ready=rows&&json_object_array_length(rows)==1&&json_object_object_get_ex(json_object_array_get_idx(rows,0),"version",&version)&&version&&json_object_get_int(version)==1;
    if(rows) json_object_put(rows);if(!close_database(&db)) return 1;
    if(!ready) {fprintf(stderr,"Extensão web v1 ausente. Execute preparar_web_local.py antes de iniciar.\n");return 1;}
    if(check) {puts("PASS: API C11 conectou por ODBC/TLS; esquema CLI v3 e web v1 disponíveis.");return 0;}
    signal(SIGINT,stop_server);signal(SIGTERM,stop_server);signal(SIGPIPE,SIG_IGN);
    listener=socket(AF_INET,SOCK_STREAM,0);if(listener<0) {perror("socket");return 1;}
    int reuse=1;(void)setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons((uint16_t)port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(listener,(struct sockaddr *)&address,sizeof address)<0||listen(listener,16)<0) {perror("Servidor local");close(listener);return 1;}
    printf("AquaSense disponível em http://localhost:%u — API C11 e SQL Server local.\n",port);fflush(stdout);
    while(!stopping&&listener>=0) {
        int client=accept(listener,NULL,NULL);
        if(client<0) {if(errno==EINTR) continue;if(stopping) break;perror("accept");break;}
        handle_client(client);close(client);
    }
    if(listener>=0) close(listener);return cleanup_failed?1:0;
}
