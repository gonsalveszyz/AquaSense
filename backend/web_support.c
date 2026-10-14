#include "web_support.h"
#include "report_input.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WebParam web_text(const char *s) { WebParam p={WEB_TEXT,s,0,0}; return p; }
WebParam web_integer(int64_t v) { WebParam p={WEB_INTEGER,NULL,v,0}; return p; }
WebParam web_real(double v) { WebParam p={WEB_REAL,NULL,0,v}; return p; }
WebParam web_null(void) { WebParam p={WEB_NULL,NULL,0,0}; return p; }
void web_ok(WebResult *r,int status,json_object *data) { r->status=status; r->data=data; r->code=NULL; r->message[0]=0; }
void web_error(WebResult *r,int status,const char *code,const char *message) {
    r->status=status; r->data=NULL; r->code=code;
    (void)snprintf(r->message,sizeof r->message,"%s",message);
}
const char *web_body_text(json_object *body,const char *key) {
    json_object *v=NULL;
    if (!body || !json_object_object_get_ex(body,key,&v) || !v || !json_object_is_type(v,json_type_string)) return NULL;
    const char *s=json_object_get_string(v);
    return (size_t)json_object_get_string_len(v)==strlen(s)?s:NULL;
}
int web_body_i64(json_object *body,const char *key,int64_t *out) {
    json_object *v=NULL;
    if (!body || !out || !json_object_object_get_ex(body,key,&v) || !v || !json_object_is_type(v,json_type_int)) return 0;
    *out=json_object_get_int64(v); return 1;
}
static int unhex(unsigned char ch) {
    if(ch>='0'&&ch<='9') return ch-'0';
    if(ch>='a'&&ch<='f') return ch-'a'+10;
    if(ch>='A'&&ch<='F') return ch-'A'+10;
    return -1;
}
static int decode(const char *s,size_t n,char *out,size_t cap) {
    size_t j=0;
    for(size_t i=0;i<n;i++) {
        unsigned char ch=(unsigned char)s[i];
        if(ch=='%') {
            if(i+2>=n) return 0;
            int a=unhex((unsigned char)s[i+1]),b=unhex((unsigned char)s[i+2]);
            if(a<0||b<0) return 0;
            ch=(unsigned char)(a*16+b); i+=2;
        } else if(ch=='+') ch=' ';
        if(ch==0||ch<32||ch==127||j+1>=cap) return 0;
        out[j++]=(char)ch;
    }
    out[j]=0; return 1;
}
int web_query_value(const char *query,const char *key,char *out,size_t cap) {
    if(!query||!key||!out||!cap) return -1;
    int found=0;
    for(const char *p=query;*p;) {
        const char *end=strchr(p,'&'); if(!end) end=p+strlen(p);
        const char *equal=memchr(p,'=',(size_t)(end-p));
        size_t keylen=(size_t)((equal?equal:end)-p);
        char decoded[128];
        if(!decode(p,keylen,decoded,sizeof decoded)) return -1;
        if(!strcmp(decoded,key)) {
            if(found||!equal||!decode(equal+1,(size_t)(end-equal-1),out,cap)) return -1;
            found=1;
        }
        p=*end?end+1:end;
    }
    return found;
}
int web_timestamp(const char *s,char out[20]) {
    if(!s) return 0;
    size_t n=strlen(s);
    if(n!=19 && !(n==20 && s[19]=='Z')) return 0;
    memcpy(out,s,19); out[19]=0;
    SQL_TIMESTAMP_STRUCT ts;
    if(!report_parse_timestamp(out,&ts)) return 0;
    out[10]='T'; return 1;
}
int web_utf8_valid(const unsigned char *text,size_t length) {
    for(size_t i=0;i<length;) {
        unsigned code=text[i++],tails=0;
        if(code>=0xc2&&code<=0xdf) {code&=0x1f;tails=1;}
        else if(code>=0xe0&&code<=0xef) {code&=0x0f;tails=2;}
        else if(code>=0xf0&&code<=0xf4) {code&=0x07;tails=3;}
        else if(code>=0x80) return 0;
        if(tails>length-i) return 0;
        for(unsigned j=0;j<tails;j++) {if((text[i]&0xc0)!=0x80) return 0;code=(code<<6)|(text[i++]&0x3f);}
        if((tails==1&&code<0x80)||(tails==2&&code<0x800)||(tails==3&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff)) return 0;
    }
    return 1;
}
static void sql_failure(Database *db,SQLHSTMT stmt,const char *stage) {
    SQLCHAR state[6]={0}; SQLINTEGER native=0; SQLCHAR message[2]={0}; SQLSMALLINT length=0;
    SQLRETURN rc=SQLGetDiagRec(SQL_HANDLE_STMT,stmt,1,state,&native,message,2,&length);
    if(SQL_SUCCEEDED(rc)) {
        if(state[0]=='0'&&state[1]=='8') db->broken=1;
        fprintf(stderr,"Consulta da API falhou (%s, SQLSTATE=%.5s, codigo=%d).\n",stage,(const char *)state,(int)native);
    } else fprintf(stderr,"Consulta da API nao concluida (%s).\n",stage);
}
/* SQL statement text also needs Unicode. Passing UTF-8 literals to the ANSI
 * SQLPrepare entry point can store mojibake even when parameters are Unicode. */
static SQLWCHAR *wide_sql(const char *sql) {
    size_t bytes=strlen(sql);if(bytes>65535) return NULL;
    SQLWCHAR *out=calloc(bytes+1,sizeof *out);if(!out) return NULL;
    const unsigned char *p=(const unsigned char *)sql;size_t used=0;
    while(*p) {
        unsigned code=*p++,tails=0;
        if(code>=0xc2&&code<=0xdf) {code&=0x1f;tails=1;}
        else if(code>=0xe0&&code<=0xef) {code&=0x0f;tails=2;}
        else if(code>=0xf0&&code<=0xf4) {code&=0x07;tails=3;}
        else if(code>=0x80) {free(out);return NULL;}
        for(unsigned i=0;i<tails;i++) {if((*p&0xc0)!=0x80) {free(out);return NULL;}code=(code<<6)|(*p++&0x3f);}
        if((tails==1&&code<0x80)||(tails==2&&code<0x800)||(tails==3&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff)) {free(out);return NULL;}
        if(code>0xffff) {code-=0x10000;out[used++]=(SQLWCHAR)(0xd800+(code>>10));out[used++]=(SQLWCHAR)(0xdc00+(code&0x3ff));}
        else out[used++]=(SQLWCHAR)code;
    }
    out[used]=0;return out;
}
json_object *web_query(Database *db,const char *sql,const WebParam *params,size_t count) {
    if(!db_ready(db)||!sql||count>64||(!params&&count)) return NULL;
    SQLWCHAR *unicode=wide_sql(sql);if(!unicode) return NULL;
    SQLHSTMT stmt=SQL_NULL_HSTMT;
    if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT,db->connection,&stmt))) {free(unicode);return NULL;}
    json_object *rows=json_object_new_array();
    SQLLEN indicators[64]={0};
    int64_t integers[64]={0}; double reals[64]={0};
    int ok=rows!=NULL,exhausted=0;const char *stage="configuracao";
    if(ok) ok=SQL_SUCCEEDED(SQLSetStmtAttr(stmt,SQL_ATTR_QUERY_TIMEOUT,(SQLPOINTER)(uintptr_t)db->query_timeout,0));
    if(ok&&count) {stage="preparacao";ok=SQL_SUCCEEDED(SQLPrepareW(stmt,unicode,SQL_NTS));}
    for(size_t i=0;ok&&i<count;i++) {
        SQLSMALLINT ctype=SQL_C_CHAR,type=SQL_WVARCHAR; SQLULEN size=1;
        SQLPOINTER value=NULL; SQLLEN capacity=0;
        if(params[i].kind==WEB_TEXT) {
            if(!params[i].text||strlen(params[i].text)>16384) {ok=0;break;}
            size=(SQLULEN)strlen(params[i].text); if(!size) size=1;
            indicators[i]=SQL_NTS; value=(SQLPOINTER)(uintptr_t)params[i].text;
            capacity=(SQLLEN)strlen(params[i].text)+1;
        } else if(params[i].kind==WEB_INTEGER) {
            integers[i]=params[i].integer; ctype=SQL_C_SBIGINT;type=SQL_BIGINT;size=19;
            value=&integers[i];capacity=(SQLLEN)sizeof integers[i];
        } else if(params[i].kind==WEB_REAL) {
            if(!isfinite(params[i].real)) {ok=0;break;}
            reals[i]=params[i].real;ctype=SQL_C_DOUBLE;type=SQL_DOUBLE;size=15;
            value=&reals[i];capacity=(SQLLEN)sizeof reals[i];
        } else indicators[i]=SQL_NULL_DATA;
        stage="parametro";ok=SQL_SUCCEEDED(SQLBindParameter(stmt,(SQLUSMALLINT)(i+1),SQL_PARAM_INPUT,ctype,type,size,0,value,capacity,&indicators[i]));
    }
    if(ok) {
        stage="execucao";SQLRETURN rc=count?SQLExecute(stmt):SQLExecDirectW(stmt,unicode,SQL_NTS);
        /* NOCOUNT DML without a result set may return SQL_NO_DATA after a
         * successful mutation. It is completion, not an ODBC failure. */
        exhausted=rc==SQL_NO_DATA;ok=SQL_SUCCEEDED(rc)||exhausted;
    }
    while(ok&&!exhausted) {
        SQLSMALLINT columns=0;
        stage="colunas";
        if(!SQL_SUCCEEDED(SQLNumResultCols(stmt,&columns))||columns<0||columns>48) {ok=0;break;}
        char names[48][128]; SQLSMALLINT types[48]={0};
        for(SQLSMALLINT c=0;ok&&c<columns;c++) {
            stage="metadados";
            SQLSMALLINT namelen=0,scale=0,nullable=0;SQLULEN precision=0;
            SQLRETURN rc=SQLDescribeCol(stmt,(SQLUSMALLINT)(c+1),(SQLCHAR *)names[c],128,&namelen,&types[c],&precision,&scale,&nullable);
            if(!SQL_SUCCEEDED(rc)||namelen<=0||namelen>=128) ok=0;
        }
        if(!ok) break;
        if(columns) {
            for(;;) {
                stage="linha";
                SQLRETURN rc=SQLFetch(stmt);
                if(rc==SQL_NO_DATA) break;
                if(!SQL_SUCCEEDED(rc)) {ok=0;break;}
                if(json_object_array_length(rows)>=10000) {ok=0;break;}
                json_object *row=json_object_new_object();
                if(!row) {ok=0;break;}
                for(SQLSMALLINT c=0;ok&&c<columns;c++) {
                    stage="valor";
                    SQLLEN indicator=0;json_object *v=NULL;
                    if(types[c]==SQL_BIGINT||types[c]==SQL_INTEGER||types[c]==SQL_SMALLINT||types[c]==SQL_TINYINT||types[c]==SQL_BIT) {
                        int64_t number=0;
                        rc=SQLGetData(stmt,(SQLUSMALLINT)(c+1),SQL_C_SBIGINT,&number,(SQLLEN)sizeof number,&indicator);
                        if(SQL_SUCCEEDED(rc)&&indicator!=SQL_NULL_DATA) v=types[c]==SQL_BIT?json_object_new_boolean(number!=0):json_object_new_int64(number);
                    } else if(types[c]==SQL_FLOAT||types[c]==SQL_DOUBLE||types[c]==SQL_REAL) {
                        double number=0;
                        rc=SQLGetData(stmt,(SQLUSMALLINT)(c+1),SQL_C_DOUBLE,&number,(SQLLEN)sizeof number,&indicator);
                        if(SQL_SUCCEEDED(rc)&&indicator!=SQL_NULL_DATA&&isfinite(number)) v=json_object_new_double(number);
                        else if(indicator!=SQL_NULL_DATA) ok=0;
                    } else {
                        char text[8192]={0};
                        rc=SQLGetData(stmt,(SQLUSMALLINT)(c+1),SQL_C_CHAR,text,(SQLLEN)sizeof text,&indicator);
                        if(SQL_SUCCEEDED(rc)&&indicator!=SQL_NULL_DATA&&indicator>=0&&(size_t)indicator<sizeof text) v=json_object_new_string_len(text,(int)indicator);
                        else if(indicator!=SQL_NULL_DATA) ok=0;
                    }
                    if(!SQL_SUCCEEDED(rc)) ok=0;
                    if(ok) json_object_object_add(row,names[c],v);
                    else if(v) json_object_put(v);
                }
                if(ok) json_object_array_add(rows,row); else json_object_put(row);
                if(!ok) break;
            }
        }
        if(!ok) break;
        stage="conclusao";
        SQLRETURN rc=SQLMoreResults(stmt);
        if(rc==SQL_NO_DATA) break;
        if(!SQL_SUCCEEDED(rc)) {ok=0;break;}
    }
    if(!ok) sql_failure(db,stmt,stage);
    free(unicode);
    if(!SQL_SUCCEEDED(SQLFreeHandle(SQL_HANDLE_STMT,stmt))) {
        if(db->pending_statement==SQL_NULL_HSTMT) db->pending_statement=stmt;
        ok=0;
    }
    if(!ok) {if(rows) json_object_put(rows);return NULL;}
    return rows;
}
