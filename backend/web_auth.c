#include "web_auth.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PASSWORD_ITERATIONS 600000
#define SESSION_SECONDS 3600

static void hex(const unsigned char *bytes,size_t n,char *out) {
    static const char alphabet[]="0123456789abcdef";
    for(size_t i=0;i<n;i++) {out[2*i]=alphabet[bytes[i]>>4];out[2*i+1]=alphabet[bytes[i]&15];}
    out[2*n]=0;
}
static int from_hex(const char *s,unsigned char *out,size_t n) {
    if(!s||strlen(s)!=2*n) return 0;
    for(size_t i=0;i<n;i++) {
        unsigned a=(unsigned char)s[2*i],b=(unsigned char)s[2*i+1];
        if(!((a>='0'&&a<='9')||(a>='a'&&a<='f'))||!((b>='0'&&b<='9')||(b>='a'&&b<='f'))) return 0;
        out[i]=(unsigned char)((a<='9'?a-'0':a-'a'+10)*16+(b<='9'?b-'0':b-'a'+10));
    }
    return 1;
}
static int random_hex(char *out,size_t n) {
    unsigned char bytes[32];
    if(n>sizeof bytes||RAND_bytes(bytes,(int)n)!=1) return 0;
    hex(bytes,n,out);OPENSSL_cleanse(bytes,sizeof bytes);return 1;
}
static int digest(const char *s,char out[65]) {
    unsigned char bytes[EVP_MAX_MD_SIZE];unsigned n=0;
    if(EVP_Digest(s,strlen(s),bytes,&n,EVP_sha256(),NULL)!=1||n!=32) return 0;
    hex(bytes,32,out);OPENSSL_cleanse(bytes,sizeof bytes);return 1;
}
static int password_hash(const char *password,const char *salt_hex,int iterations,char out[65]) {
    unsigned char salt[16],bytes[32];
    if(!password||!from_hex(salt_hex,salt,16)||iterations<600000||iterations>2000000) return 0;
    int ok=PKCS5_PBKDF2_HMAC(password,(int)strlen(password),salt,16,iterations,EVP_sha256(),32,bytes)==1;
    if(ok) hex(bytes,32,out);
    OPENSSL_cleanse(bytes,sizeof bytes);OPENSSL_cleanse(salt,sizeof salt);return ok;
}
static int token_from_cookie(const char *cookie,char token[65]) {
    if(!cookie) return 0;
    int found=0;
    for(const char *p=cookie;*p;) {
        while(*p==' '||*p=='\t'||*p==';') p++;
        const char *end=strchr(p,';');if(!end) end=p+strlen(p);
        const char prefix[]="aqua_session=";
        if((size_t)(end-p)>=sizeof prefix-1&&!memcmp(p,prefix,sizeof prefix-1)) {
            if(found||(size_t)(end-p)!=sizeof prefix-1+64) return 0;
            memcpy(token,p+sizeof prefix-1,64);token[64]=0;
            unsigned char check[32];if(!from_hex(token,check,32)) return 0;
            found=1;
        }
        p=*end?end+1:end;
    }
    return found;
}
static int64_t integer(json_object *row,const char *key) {
    json_object *v=NULL;return json_object_object_get_ex(row,key,&v)?json_object_get_int64(v):0;
}
static const char *string(json_object *row,const char *key) {
    json_object *v=NULL;return json_object_object_get_ex(row,key,&v)&&v?json_object_get_string(v):NULL;
}
static int fill_user(json_object *row,WebUser *user) {
    const char *name=string(row,"nome"),*email=string(row,"email");
    if(!name||!email||strlen(name)>=sizeof user->nome||strlen(email)>=sizeof user->email) return 0;
    memset(user,0,sizeof *user);user->id=integer(row,"id");user->administrador=integer(row,"administrador")!=0;
    snprintf(user->nome,sizeof user->nome,"%s",name);snprintf(user->email,sizeof user->email,"%s",email);
    const char *csrf=string(row,"csrf_token");
    if(csrf) {if(strlen(csrf)!=64) return 0;memcpy(user->csrf,csrf,65);}
    return user->id>0;
}
static json_object *user_payload(const WebUser *user) {
    json_object *data=json_object_new_object(),*u=json_object_new_object();
    json_object_object_add(u,"id",json_object_new_int64(user->id));
    json_object_object_add(u,"nome",json_object_new_string(user->nome));
    json_object_object_add(u,"email",json_object_new_string(user->email));
    json_object_object_add(u,"administrador",json_object_new_boolean(user->administrador));
    json_object_object_add(data,"usuario",u);
    json_object_object_add(data,"csrf_token",json_object_new_string(user->csrf));return data;
}
int web_authenticate(Database *db,const char *cookie,WebUser *user) {
    char token[65],hash[65];
    if(!token_from_cookie(cookie,token)||!digest(token,hash)) return 0;
    OPENSSL_cleanse(token,sizeof token);
    WebParam p=web_text(hash);
    json_object *rows=web_query(db,"SELECT u.id,u.nome,u.email,u.administrador,s.csrf_token FROM dbo.WebSessoes s JOIN dbo.WebUsuarios u ON u.id=s.usuario_id WHERE s.token_hash=? AND s.expira_em_utc>SYSUTCDATETIME() AND u.ativo=1",&p,1);
    OPENSSL_cleanse(hash,sizeof hash);
    if(!rows) return -1;
    int ok=json_object_array_length(rows)==1&&fill_user(json_object_array_get_idx(rows,0),user);
    json_object_put(rows);return ok;
}
static int new_session(Database *db,WebUser *user,char cookie[256]) {
    char token[65],hash[65];
    if(!random_hex(token,32)||!digest(token,hash)||!random_hex(user->csrf,32)) return 0;
    WebParam p[]={web_text(hash),web_integer(user->id),web_text(user->csrf)};
    json_object *rows=web_query(db,"DELETE FROM dbo.WebSessoes WHERE expira_em_utc<=SYSUTCDATETIME(); INSERT dbo.WebSessoes(token_hash,usuario_id,csrf_token,expira_em_utc) VALUES(?,?,?,DATEADD(second,3600,SYSUTCDATETIME()));",p,3);
    OPENSSL_cleanse(hash,sizeof hash);
    if(!rows) {OPENSSL_cleanse(token,sizeof token);return 0;}
    json_object_put(rows);
    /* This executable binds loopback HTTP only. HTTPS deployment must terminate
     * TLS and issue Secure cookies; it is intentionally not a public server. */
    snprintf(cookie,256,"aqua_session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d",token,SESSION_SECONDS);
    OPENSSL_cleanse(token,sizeof token);return 1;
}
static size_t utf8_characters(const char *s) {
    size_t n=0;for(const unsigned char *p=(const unsigned char *)s;*p;p++) if((*p&0xc0)!=0x80) n++;return n;
}
static int valid_name(const char *s) {
    if(!s||!strlen(s)||strlen(s)>400||!web_utf8_valid((const unsigned char *)s,strlen(s))) return 0;
    int visible=0;size_t units=0;
    for(const unsigned char *p=(const unsigned char *)s;*p;p++) {
        if(*p<32||*p==127) return 0;if(*p!=' ') visible=1;
        if((*p&0xc0)!=0x80) units+=*p>=0xf0?2U:1U;
    }
    if(units>100) return 0;
    return visible;
}
static int normalize_email(const char *s,char out[255]) {
    if(!s||strlen(s)<3||strlen(s)>254) return 0;
    int at=0;size_t i=0;
    for(;s[i];i++) {
        unsigned ch=(unsigned char)s[i];
        if(ch<=32||ch>=127) return 0;
        if(ch=='@') {if(at||i==0||!s[i+1]) return 0;at=1;}
        else if(!(isalnum((int)ch)||strchr(".!#$%&'*+-/=?^_`{|}~",(int)ch))) return 0;
        out[i]=(char)tolower((int)ch);
    }
    out[i]=0;return at;
}
typedef struct {char key[65];time_t start;unsigned attempts;} Rate;
static Rate rates[64];
static Rate *rate_for(const char *email) {
    char key[65];if(!digest(email,key)) return NULL;
    time_t now=time(NULL);Rate *chosen=NULL;
    for(size_t i=0;i<64;i++) {
        if(!strcmp(rates[i].key,key)) {chosen=&rates[i];break;}
        if(!chosen&&(!rates[i].key[0]||now-rates[i].start>=60||now<rates[i].start)) chosen=&rates[i];
    }
    /* A full active table must fail closed; rotating invented e-mails cannot
     * evict another user's still-active attempt counter. */
    if(!chosen) return NULL;
    if(strcmp(chosen->key,key)||now-chosen->start>=60||now<chosen->start) {
        snprintf(chosen->key,sizeof chosen->key,"%s",key);chosen->start=now;chosen->attempts=0;
    }
    return chosen;
}
static void db_error(WebResult *r) {web_error(r,503,"BANCO_INDISPONIVEL","Não foi possível concluir a operação no banco. Tente novamente.");}
static void rollback(Database *db) {json_object *r=web_query(db,"IF @@TRANCOUNT>0 ROLLBACK TRANSACTION;",NULL,0);if(r) json_object_put(r);}
int web_auth_route(Database *db,const WebUser *user,const WebRequest *request,const char *cookie,WebResult *r,char set_cookie[256]) {
    const char *path=request->path,*method=request->method;set_cookie[0]=0;
    if(!strcmp(path,"/api/v1/auth/setup")&&!strcmp(method,"GET")) {
        json_object *rows=web_query(db,"SELECT CAST((SELECT COUNT(*) FROM dbo.WebUsuarios) AS bigint) AS usuarios, CAST((SELECT COUNT(*) FROM dbo.Leituras l WHERE NOT EXISTS(SELECT 1 FROM dbo.WebQualidadeUsuarios q WHERE q.leitura_id=l.id)) AS bigint) AS legado_sem_vinculo",NULL,0);
        if(!rows||json_object_array_length(rows)!=1) {if(rows) json_object_put(rows);db_error(r);return 1;}
        json_object *row=json_object_array_get_idx(rows,0),*data=json_object_new_object();
        int first=integer(row,"usuarios")==0;
        json_object_object_add(data,"primeiro_usuario",json_object_new_boolean(first));
        json_object_object_add(data,"legado_sem_vinculo",json_object_new_int64(first?integer(row,"legado_sem_vinculo"):0));
        json_object_put(rows);web_ok(r,200,data);return 1;
    }
    if(!strcmp(path,"/api/v1/auth/me")&&!strcmp(method,"GET")) {web_ok(r,200,user_payload(user));return 1;}
    if(!strcmp(path,"/api/v1/auth/logout")&&!strcmp(method,"POST")) {
        char token[65],hash[65];
        if(!token_from_cookie(cookie,token)||!digest(token,hash)) {web_error(r,401,"SESSAO_INVALIDA","Entre novamente.");return 1;}
        WebParam p[]={web_text(hash),web_integer(user->id)};
        json_object *rows=web_query(db,"DELETE FROM dbo.WebSessoes WHERE token_hash=? AND usuario_id=?",p,2);
        OPENSSL_cleanse(token,sizeof token);OPENSSL_cleanse(hash,sizeof hash);
        if(!rows) {db_error(r);return 1;}json_object_put(rows);
        snprintf(set_cookie,256,"aqua_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
        web_ok(r,200,json_object_new_object());return 1;
    }
    int registration=!strcmp(path,"/api/v1/auth/register");
    if((!registration&&strcmp(path,"/api/v1/auth/login"))||strcmp(method,"POST")) return 0;
    const char *password=web_body_text(request->body,"senha");char email[255];
    if(!normalize_email(web_body_text(request->body,"email"),email)||!password||strlen(password)>128||strlen(password)==0) {
        web_error(r,400,"CAMPOS_INVALIDOS","Informe um e-mail válido e uma senha com até 128 bytes.");return 1;
    }
    Rate *rate=rate_for(email);
    if(!rate||rate->attempts>=5) {web_error(r,429,"MUITAS_TENTATIVAS","Aguarde um minuto antes de tentar novamente.");return 1;}
    rate->attempts++;
    WebUser authenticated;memset(&authenticated,0,sizeof authenticated);
    if(registration) {
        const char *name=web_body_text(request->body,"nome");
        json_object *import=NULL;int import_legacy=0;
        if(json_object_object_get_ex(request->body,"importar_legado",&import)) {
            if(!import||!json_object_is_type(import,json_type_boolean)) {web_error(r,400,"CAMPOS_INVALIDOS","importar_legado deve ser booleano.");return 1;}
            import_legacy=json_object_get_boolean(import);
        }
        if(!valid_name(name)||utf8_characters(password)<12) {web_error(r,400,"CAMPOS_INVALIDOS","Informe seu nome (até 100 caracteres) e uma senha com pelo menos 12 caracteres.");return 1;}
        char salt[33],hash[65];
        if(!random_hex(salt,16)||!password_hash(password,salt,PASSWORD_ITERATIONS,hash)) {web_error(r,503,"AUTENTICACAO_INDISPONIVEL","Não foi possível proteger a senha.");return 1;}
        json_object *rows=web_query(db,"SET TRANSACTION ISOLATION LEVEL SERIALIZABLE; BEGIN TRANSACTION; SELECT CAST(COUNT(*) AS bigint) AS usuarios FROM dbo.WebUsuarios WITH(UPDLOCK,HOLDLOCK);",NULL,0);
        if(!rows||json_object_array_length(rows)!=1) {if(rows) json_object_put(rows);rollback(db);db_error(r);return 1;}
        int admin=integer(json_object_array_get_idx(rows,0),"usuarios")==0;json_object_put(rows);
        if(import_legacy&&!admin) {rollback(db);web_error(r,403,"IMPORTACAO_RESTRITA","Somente o primeiro administrador pode vincular o legado no cadastro.");return 1;}
        WebParam ep=web_text(email);
        rows=web_query(db,"SELECT id FROM dbo.WebUsuarios WHERE email_norm=?",&ep,1);
        if(!rows) {rollback(db);db_error(r);return 1;}
        if(json_object_array_length(rows)) {json_object_put(rows);rollback(db);web_error(r,409,"EMAIL_CADASTRADO","Este e-mail já está cadastrado.");return 1;}json_object_put(rows);
        WebParam p[]={web_text(name),web_text(email),web_text(hash),web_text(salt),web_integer(admin)};
        rows=web_query(db,"INSERT dbo.WebUsuarios(nome,email,senha_hash,senha_salt,administrador) OUTPUT inserted.id,inserted.nome,inserted.email,inserted.administrador VALUES(?,?,?,?,?);",p,5);
        OPENSSL_cleanse(hash,sizeof hash);OPENSSL_cleanse(salt,sizeof salt);
        if(!rows||json_object_array_length(rows)!=1||!fill_user(json_object_array_get_idx(rows,0),&authenticated)) {if(rows) json_object_put(rows);rollback(db);db_error(r);return 1;}json_object_put(rows);
        WebParam uid=web_integer(authenticated.id);
        rows=web_query(db,"INSERT dbo.WebPreferencias(usuario_id) VALUES(?)",&uid,1);
        if(!rows) {rollback(db);db_error(r);return 1;}json_object_put(rows);
        if(import_legacy) {
            rows=web_query(db,"INSERT dbo.WebQualidadeUsuarios(leitura_id,usuario_id,origem_vinculo) SELECT l.id,?,'LEGADO_EXPLICITO' FROM dbo.Leituras l WHERE NOT EXISTS(SELECT 1 FROM dbo.WebQualidadeUsuarios q WHERE q.leitura_id=l.id)",&uid,1);
            if(!rows) {rollback(db);db_error(r);return 1;}json_object_put(rows);
        }
        if(!new_session(db,&authenticated,set_cookie)) {rollback(db);db_error(r);return 1;}
        rows=web_query(db,"COMMIT TRANSACTION; SET TRANSACTION ISOLATION LEVEL READ COMMITTED;",NULL,0);
        if(!rows) {set_cookie[0]=0;rollback(db);db_error(r);return 1;}json_object_put(rows);
    } else {
        WebParam p=web_text(email);
        json_object *rows=web_query(db,"SELECT id,nome,email,administrador,senha_hash,senha_salt,iteracoes FROM dbo.WebUsuarios WHERE email_norm=? AND ativo=1",&p,1);
        if(!rows) {db_error(r);return 1;}
        int found=json_object_array_length(rows)==1;json_object *row=found?json_object_array_get_idx(rows,0):NULL;
        const char *salt=found?string(row,"senha_salt"):"00000000000000000000000000000000";
        const char *expected=found?string(row,"senha_hash"):"0000000000000000000000000000000000000000000000000000000000000000";
        char calculated[65];int valid=password_hash(password,salt,found?(int)integer(row,"iteracoes"):PASSWORD_ITERATIONS,calculated)&&expected&&strlen(expected)==64&&CRYPTO_memcmp(calculated,expected,64)==0&&found;
        OPENSSL_cleanse(calculated,sizeof calculated);
        if(valid) valid=fill_user(row,&authenticated);
        json_object_put(rows);
        if(!valid) {web_error(r,401,"CREDENCIAIS_INVALIDAS","E-mail ou senha inválidos.");return 1;}
        if(!new_session(db,&authenticated,set_cookie)) {db_error(r);return 1;}
    }
    rate->attempts=0;web_ok(r,registration?201:200,user_payload(&authenticated));return 1;
}
