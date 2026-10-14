#define _POSIX_C_SOURCE 200809L
/* Test private protocol/auth helpers without opening SQL or binding a listener. */
#define main aquasense_server_main_not_executed
#include "../../backend/web_server.c"
#undef main
#include "../../backend/web_auth.c"
#include <sys/wait.h>

static unsigned checks=0;
#define CHECK(condition) do { ++checks; if(!(condition)) { \
    fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition);exit(1); \
} } while(0)

static void json_case(const char *source,int expected) {
    static unsigned case_number=0;++case_number;
    HttpRequest request={0};
    size_t length=strlen(source);
    CHECK(length<sizeof request.body);
    memcpy(request.body,source,length+1);request.body_length=length;
    json_object *value=parse_body(&request);
    if((value!=NULL)!=expected)fprintf(stderr,"JSON case %u expected=%d got=%d\n",case_number,expected,value!=NULL);
    CHECK((value!=NULL)==expected);
    if(value)json_object_put(value);
}

static void test_json(void) {
    json_case("{\"nome\":\"Água 🚰\",\"ph\":7.2,\"ativo\":true,\"limite\":null}",1);
    json_case("{\"usuario\":{\"nome\":\"Água 🚰\"},\"preferencias\":{\"ph_min\":6.5,\"ph_max\":8.5}}",1);
    json_case("{\"email\":\"a\",\"email\":\"b\"}",0);
    json_case("{\"preferencias\":{\"ph_min\":6,\"ph_min\":7}}",0);
    json_case("{\"email\":\"a\",\"\\u0065mail\":\"b\"}",0);
    json_case("{\"preferencias\":{\"ph_min\":6,\"\\u0070h_min\":7}}",0);
    json_case("{\"nome\":\"a\\u0000b\"}",0);
    json_case("{\"na\\u0000me\":\"value\"}",0);
    json_case("{\"nome\":\"\\ud83d\\udca7\"}",1);
    json_case("{\"nome\":\"\\ud83d\"}",0);
    json_case("{\"nome\":\"\\udca7\"}",0);
    json_case("{\"nome\":\"\\ud83d\\u0061\"}",0);
    json_case("{\"nome\":\"\\udca7\\ud83d\"}",0);
    json_case("{\"nome\":\"\\\\ud800\"}",1);
    json_case("{\"nome\":\"quoted { [ ] } and \\\"key\\\": text\"}",1);
    json_case("{\"preferencias\":[]}",0);
    json_case("[]",0);
    json_case("\"texto\"",0);
    json_case("{}{}",0);
    json_case("{/* comment */\"ph\":7}",0);
    json_case("{\"nome\":\"\xc0\xaf\"}",0);
    json_case("{\"nome\":\"\xe0\x80\xaf\"}",0);
    json_case("{\"nome\":\"\xf0\x80\x80\xaf\"}",0);
    json_case("{\"nome\":\"\xed\xa0\x80\"}",0);
    json_case("{\"nome\":\"\xf4\x90\x80\x80\"}",0);
    json_case("{\"nome\":\"\xf5\x80\x80\x80\"}",0);
    json_case("{\"nome\":\"\x80\"}",0);
    json_case("{\"nome\":\"\xe2\x82\"}",0);
    json_case("{\"nome\":\"\xe0\xa0\x80\"}",1);
    json_case("{\"nome\":\"\xf4\x8f\xbf\xbf\"}",1);
    json_case("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":1}}}}}",1);
    char deep[256];size_t used=0;
    for(unsigned i=0;i<20;i++){memcpy(deep+used,"{\"a\":",5);used+=5;}
    deep[used++]='1';for(unsigned i=0;i<20;i++)deep[used++]='}';deep[used]=0;
    json_case(deep,0);
}

static int raw_request(const void *bytes,size_t length,HttpRequest *request) {
    int pair[2];CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    int capacity=65536;CHECK(setsockopt(pair[1],SOL_SOCKET,SO_SNDBUF,&capacity,sizeof capacity)==0);
    CHECK(send_all(pair[1],bytes,length));CHECK(shutdown(pair[1],SHUT_WR)==0);
    int result=read_request(pair[0],request);CHECK(close(pair[0])==0);CHECK(close(pair[1])==0);
    return result;
}
static void http_case(const char *source,int expected) {
    HttpRequest request;CHECK(raw_request(source,strlen(source),&request)==expected);
}
static void test_http(void) {
    http_case("GET /api/v1/health HTTP/1.1\r\nHost: localhost:8080\r\n\r\n",200);
    http_case("GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n\r\n",200);
    http_case("GET / HTTP/1.1\r\nHost: LOCALHOST:8080\r\nOrigin: http://localhost:8080\r\n\r\n",200);
    http_case("GET / HTTP/1.1\r\n\r\n",400);
    http_case("GET / HTTP/1.1\r\nHost: localhost:8080\r\nHost: localhost:8080\r\n\r\n",400);
    http_case("GET / HTTP/1.1\r\nHost: localhost.attacker:8080\r\n\r\n",400);
    http_case("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",400);
    http_case("GET / HTTP/1.0\r\nHost: localhost:8080\r\n\r\n",400);
    http_case("DELETE / HTTP/1.1\r\nHost: localhost:8080\r\n\r\n",405);
    http_case("GET / HTTP/1.1\r\nHost: localhost:8080\r\n Transfer-Encoding: chunked\r\n\r\n",400);
    http_case("GET / HTTP/1.1\r\nHost: localhost:8080\r\nTransfer-Encoding: chunked\r\n\r\n",400);
    http_case("GET / HTTP/1.1\r\nHost: localhost:8080\r\nExpect: 100-continue\r\n\r\n",400);
    http_case("GET / HTTP/1.1\r\nHost: localhost:8080\r\nOrigin: https://example.test\r\n\r\n",403);
    http_case("POST /api/v1/auth/login HTTP/1.1\r\nHost: localhost:8080\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",403);
    http_case("POST /api/v1/auth/login HTTP/1.1\r\nHost: localhost:8080\r\nOrigin: null\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",403);
    http_case("POST /api/v1/auth/login HTTP/1.1\r\nHost: localhost:8080\r\nOrigin: http://localhost:8080\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\n{}",415);
    http_case("POST /api/v1/auth/login HTTP/1.1\r\nHost: localhost:8080\r\nOrigin: http://localhost:8080\r\nContent-Type: application/json\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}",400);
    http_case("GET / HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 1\r\n\r\nx",400);
    http_case("GET / HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 99999999999999999999999999\r\n\r\n",413);
    http_case("POST /api/v1/auth/login HTTP/1.1\r\nHost: localhost:8080\r\nOrigin: http://localhost:8080\r\nContent-Type: application/json\r\nContent-Length: 16385\r\n\r\n",413);
    http_case("POST /api/v1/auth/login HTTP/1.1\r\nHost: localhost:8080\r\nOrigin: http://localhost:8080\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}x",400);
    HttpRequest request;
    const char *good="POST /api/v1/auth/login?foo=bar HTTP/1.1\r\nHost: localhost:8080\r\nOrigin: http://localhost:8080\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: 2\r\n\r\n{}";
    CHECK(raw_request(good,strlen(good),&request)==200);
    CHECK(request.body_length==2&&!strcmp((const char *)request.body,"{}"));
    CHECK(!strcmp(request.path,"/api/v1/auth/login")&&!strcmp(request.query,"foo=bar"));
    const char nul_header[]="GET / HTTP/1.1\r\nHost: localhost:8080\r\nX-A: x\0y\r\n\r\n";
    CHECK(raw_request(nul_header,sizeof nul_header-1,&request)==400);
    const char nul_body[]="POST /api/v1/auth/login HTTP/1.1\r\nHost: localhost:8080\r\nOrigin: http://localhost:8080\r\nContent-Type: application/json\r\nContent-Length: 3\r\n\r\n{}\0";
    CHECK(raw_request(nul_body,sizeof nul_body-1,&request)==400);
    char oversized[21000];
    int initial=snprintf(oversized,sizeof oversized,"GET / HTTP/1.1\r\nHost: localhost:8080\r\nX-Long: ");CHECK(initial>0);
    size_t length=(size_t)initial;memset(oversized+length,'x',17000);length+=17000;memcpy(oversized+length,"\r\n\r\n",4);length+=4;
    CHECK(raw_request(oversized,length,&request)==431);
    strcpy(oversized,"GET /");length=5;memset(oversized+length,'x',2050);length+=2050;
    const char suffix[]=" HTTP/1.1\r\nHost: localhost:8080\r\n\r\n";memcpy(oversized+length,suffix,sizeof suffix);length+=sizeof suffix-1;
    CHECK(raw_request(oversized,length,&request)==414);
}

static void test_deadline(void) {
    int pair[2];CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    pid_t writer=fork();CHECK(writer>=0);
    if(writer==0){close(pair[0]);struct timespec interval={0,500000000};
        for(unsigned i=0;i<20;i++){if(send(pair[1],"G",1,0)!=1)break;(void)nanosleep(&interval,NULL);}close(pair[1]);_exit(0);}
    CHECK(close(pair[1])==0);HttpRequest request;int64_t started=milliseconds();
    CHECK(read_request(pair[0],&request)==408);int64_t elapsed=milliseconds()-started;
    CHECK(elapsed>=4000&&elapsed<6500);CHECK(close(pair[0])==0);
    int result=kill(writer,SIGTERM);CHECK(result==0||errno==ESRCH);int status=0;CHECK(waitpid(writer,&status,0)==writer);
    printf("PASS: prazo HTTP global com bytes a cada 500 ms: %lld ms.\n",(long long)elapsed);
}

static void test_auth(void) {
    char calculated[65];
    CHECK(password_hash("SenhaAquaSense-2026","000102030405060708090a0b0c0d0e0f",600000,calculated));
    /* Independently derived by Python hashlib.pbkdf2_hmac SHA256/600000. */
    CHECK(!strcmp(calculated,"57fdcd2474bb6d64a8ab2e214516f6e1baec128e564d0b107e13872f60d2d315"));
    CHECK(!password_hash("SenhaAquaSense-2026","000102030405060708090a0b0c0d0e0f",599999,calculated));
    CHECK(!password_hash("SenhaAquaSense-2026","000102030405060708090a0b0c0d0e0f",2000001,calculated));
    CHECK(!password_hash("SenhaAquaSense-2026","xx",600000,calculated));
    CHECK(valid_name("Água 🚰"));CHECK(!valid_name("   "));CHECK(!valid_name("Nome\n"));
    char name[405]={0};
    for(unsigned i=0;i<50;i++)memcpy(name+4*i,"💧",4);CHECK(valid_name(name));
    memcpy(name+200,"💧",4);CHECK(!valid_name(name));
    memset(name,'x',100);name[100]=0;CHECK(valid_name(name));name[100]='x';name[101]=0;CHECK(!valid_name(name));
    char token[65],cookie[256];memset(token,'a',64);token[64]=0;
    snprintf(cookie,sizeof cookie,"other=x; aqua_session=%s; another=y",token);CHECK(token_from_cookie(cookie,calculated));CHECK(!strcmp(token,calculated));
    snprintf(cookie,sizeof cookie,"aqua_session=%s; aqua_session=%s",token,token);CHECK(!token_from_cookie(cookie,calculated));
    CHECK(!token_from_cookie("aqua_session=short",calculated));
    memset(token,'A',64);snprintf(cookie,sizeof cookie,"aqua_session=%s",token);CHECK(!token_from_cookie(cookie,calculated));
    char email[255];CHECK(normalize_email("Person@Example.test",email));CHECK(!strcmp(email,"person@example.test"));CHECK(!normalize_email("bad email@example.test",email));
    memset(rates,0,sizeof rates);Rate *victim=rate_for("victim@example.test");CHECK(victim!=NULL);victim->attempts=5;
    for(unsigned i=0;i<63;i++){snprintf(email,sizeof email,"filler%u@example.test",i);Rate *slot=rate_for(email);CHECK(slot!=NULL);slot->attempts=1;}
    CHECK(rate_for("overflow@example.test")==NULL);victim=rate_for("victim@example.test");CHECK(victim!=NULL&&victim->attempts==5);
    for(size_t i=0;i<64;i++)rates[i].start=time(NULL)-61;
    Rate *renewed=rate_for("overflow@example.test");CHECK(renewed!=NULL&&renewed->attempts==0);
}

int main(void) {
    signal(SIGPIPE,SIG_IGN);
    test_json();test_http();test_auth();test_deadline();
    printf("APROVADO: %u verificacoes puras de JSON, HTTP, prazo, cookies, PBKDF2, UTF16 e limite de tentativas. Nenhuma conexao SQL.\n",checks);
    return 0;
}
