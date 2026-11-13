#define _POSIX_C_SOURCE 200809L
#include "web_support.h"
#include "consumption.h"
#include "input.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Window functions always precede time filtering: the first displayed interval
 * retains its real predecessor even when it falls outside the requested range. */
#define CONSUMPTION_CTE \
 "WITH ordered AS (SELECT l.*,m.nome medidor_nome," \
 "LAG(l.acumulado_m3) OVER(PARTITION BY l.medidor_id ORDER BY l.registrado_em_utc,l.id) anterior_m3," \
 "LAG(l.registrado_em_utc) OVER(PARTITION BY l.medidor_id ORDER BY l.registrado_em_utc,l.id) anterior_em_utc " \
 "FROM dbo.WebLeiturasConsumo l JOIN dbo.WebMedidores m ON m.id=l.medidor_id AND m.usuario_id=l.usuario_id " \
 "WHERE l.usuario_id=@u AND (@m IS NULL OR l.medidor_id=@m))," \
 "intervals AS (SELECT *,CAST((acumulado_m3-anterior_m3)*1000 AS decimal(21,3)) consumo_litros " \
 "FROM ordered), filtered AS (SELECT * FROM intervals WHERE registrado_em_utc>=@inicio AND registrado_em_utc<@fim) "
#define FILTER_DECLARE "SET NOCOUNT ON; DECLARE @u bigint=?,@inicio datetime2(0)=CONVERT(datetime2(0),?,126),@fim datetime2(0)=CONVERT(datetime2(0),?,126),@m bigint=?; "
#define MEDIDOR_SELECT "SELECT id,nome,codigo,status,CONVERT(varchar(19),instalado_em_utc,126)+'Z' instalado_em_utc,unidade,substitui_medidor_id FROM dbo.WebMedidores WHERE usuario_id=? ORDER BY criado_em_utc DESC,id DESC;"
#define TARIFA_SELECT "SELECT id,valor_m3,CONVERT(varchar(19),inicio_utc,126)+'Z' inicio_utc FROM dbo.WebTarifas WHERE usuario_id=? ORDER BY inicio_utc DESC,id DESC;"
#define PREF_SELECT "SELECT limite_diario_litros,limite_mensal_litros,vazamento_litros_minuto,ph_min,ph_max,temperatura_min,temperatura_max,turbidez_max,unidade,tema,notificacoes FROM dbo.WebPreferencias WHERE usuario_id=?;"
#define QUALITY_COLUMNS "l.id,l.temperatura_c,l.ph,l.turbidez_ntu,l.origem,l.sensor_id,l.local_id,l.registrado_em_utc horario_confirmado_utc,CASE WHEN l.registrado_em_utc=1 THEN CONVERT(varchar(19),l.registrado_em,126)+'Z' ELSE NULL END registrado_em_utc,CONVERT(varchar(19),l.registrado_em,126) registrado_em,CONVERT(varchar(19),l.registrado_em_original,126) registrado_em_original,l.fuso_origem_windows,q.origem_vinculo"

typedef struct { char inicio[20],fim[20]; int64_t meter; int month,page,size,ascending; } Filters;
static json_object *field(json_object *obj,const char *key) { json_object *value=NULL; return obj && json_object_object_get_ex(obj,key,&value)?value:NULL; }
static const char *strfield(json_object *obj,const char *key) { json_object *v=field(obj,key); return v && json_object_get_type(v)==json_type_string && (size_t)json_object_get_string_len(v)==strlen(json_object_get_string(v))?json_object_get_string(v):NULL; }
static int failure(WebResult *r) { web_error(r,503,"BANCO_INDISPONIVEL","Não foi possível concluir a operação no banco. Tente novamente.");return 1; }
static int invalid(WebResult *r,const char *message) { web_error(r,422,"DADOS_INVALIDOS",message);return 1; }
static int notfound(WebResult *r) { web_error(r,404,"NAO_ENCONTRADO","Registro não encontrado para este usuário.");return 1; }
static json_object *take_first(json_object *rows) { if (!rows) return NULL; json_object *v=json_object_array_length(rows)?json_object_get(json_object_array_get_idx(rows,0)):json_object_new_object();json_object_put(rows);return v; }
static int done_rows(WebResult *r,json_object *rows,int status) { if(!rows)return failure(r); web_ok(r,status,take_first(rows));return 1; }
/* SQL NVARCHAR length counts UTF-16 code units, not UTF-8 bytes/codepoints. */
static int bounded_text(const char *text,size_t maximum,int required,int multiline) {
    if(!text)return !required;
    size_t units=0;int visible=0;const unsigned char *p=(const unsigned char *)text;
    while(*p){unsigned code=*p++;unsigned tails=0;
        if(code>=0xc2&&code<=0xdf){code&=0x1f;tails=1;}
        else if(code>=0xe0&&code<=0xef){code&=0x0f;tails=2;}
        else if(code>=0xf0&&code<=0xf4){code&=0x07;tails=3;}
        else if(code>=0x80)return 0;
        for(unsigned i=0;i<tails;++i){if((*p&0xc0)!=0x80)return 0;code=(code<<6)|(*p++&0x3f);}
        if((tails==1&&code<0x80)||(tails==2&&code<0x800)||(tails==3&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return 0;
        if(code<32 || code==127){if(!multiline || (code!=9&&code!=10&&code!=13))return 0;}
        if(code>32)visible=1;
        units+=code>0xffff?2U:1U;if(units>maximum)return 0;
    }
    return !required || visible;
}
static int text_ok(const char *text,size_t maximum) {return bounded_text(text,maximum,1,0);}
static int optional_text_type(json_object *body,const char *key) {
    json_object *v=NULL;if(!json_object_object_get_ex(body,key,&v))return 1;
    return !v || json_object_get_type(v)==json_type_null || (json_object_get_type(v)==json_type_string && (size_t)json_object_get_string_len(v)==strlen(json_object_get_string(v)));
}
static int parse_positive(const char *text,int64_t *out) {
    if(!text || !*text)return 0;
    int64_t n=0;
    for(const unsigned char *p=(const unsigned char *)text;*p;++p){if(*p<'0'||*p>'9'||n>(INT64_MAX-(*p-'0'))/10)return 0;n=n*10+(*p-'0');}
    if(n<=0)return 0;*out=n;return 1;
}
static void defaults(Filters *f) {
    time_t now=time(NULL),end=now+(86400-now%86400);struct tm tm,final_tm;
    gmtime_r(&now,&tm);gmtime_r(&end,&final_tm);
    /* Default includes the entire UTC day, including alerts generated during
     * this request; explicit API filters keep their exact exclusive end. */
    strftime(f->fim,sizeof(f->fim),"%Y-%m-%dT00:00:00",&final_tm);
    int year=tm.tm_year+1900,month=tm.tm_mon+1-6;
    if(month<1){month+=12;--year;}
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int last=days[month-1]+(month==2 && year%4==0 && (year%100!=0||year%400==0));
    if(tm.tm_mday>last)tm.tm_mday=last;
    snprintf(f->inicio,sizeof(f->inicio),"%04d-%02d-%02dT00:00:00",year,month,tm.tm_mday);
    f->meter=0;f->month=1;f->page=1;f->size=25;f->ascending=0;
}
static int filters(const WebRequest *req,Filters *f,WebResult *r) {
    char value[80]; defaults(f);
    const char *dates[]={"inicio","fim"};char *dest[]={f->inicio,f->fim};
    for(int i=0;i<2;++i){int present=web_query_value(req->query,dates[i],value,sizeof(value));if(present<0)return invalid(r,"Filtro de data inválido ou repetido."),0;if(present && !web_timestamp(value,dest[i]))return invalid(r,"Use datas UTC ISO 8601 nos filtros."),0;}
    if(strcmp(f->inicio,f->fim)>=0)return invalid(r,"A data inicial deve ser anterior à final."),0;
    int found=web_query_value(req->query,"agrupamento",value,sizeof(value));if(found<0)return invalid(r,"Agrupamento inválido."),0;
    if(found){if(!strcmp(value,"mes"))f->month=1;else if(!strcmp(value,"dia"))f->month=0;else return invalid(r,"Agrupamento deve ser dia ou mes."),0;}
    found=web_query_value(req->query,"ordem",value,sizeof(value));if(found<0 || (found && strcmp(value,"asc") && strcmp(value,"desc")))return invalid(r,"Ordem deve ser asc ou desc."),0;f->ascending=found && !strcmp(value,"asc");
    const char *keys[]={"medidor_id","page","page_size"};
    for(int i=0;i<3;++i){found=web_query_value(req->query,keys[i],value,sizeof(value));int64_t n=0;if(found<0 || (found && !parse_positive(value,&n)))return invalid(r,"Paginação ou medidor inválido."),0;
        if(found){if(i==0)f->meter=n;else if(i==1){if(n>1000000)return invalid(r,"Página fora do limite."),0;f->page=(int)n;}else{if(n>100)return invalid(r,"page_size deve estar entre 1 e 100."),0;f->size=(int)n;}}}
    return 1;
}
static void filter_params(WebParam p[4],int64_t uid,const Filters *f){p[0]=web_integer(uid);p[1]=web_text(f->inicio);p[2]=web_text(f->fim);p[3]=f->meter?web_integer(f->meter):web_null();}
static int meter_filter(Database *db,const WebUser *u,const Filters *f,WebResult *r) {
    if(!f->meter)return 1;WebParam p[]={web_integer(u->id),web_integer(f->meter)};
    json_object *rows=web_query(db,"SELECT id FROM dbo.WebMedidores WHERE usuario_id=? AND id=?;",p,2);
    if(!rows)return failure(r),0;int exists=json_object_array_length(rows)>0;json_object_put(rows);if(!exists)notfound(r);return exists;
}
static json_object *pagination(json_object *items,json_object *total,const Filters *f) {
    int64_t n=json_object_get_int64(field(total,"total"));json_object *data=json_object_new_object();
    json_object_object_add(data,"items",items);json_object_object_add(data,"page",json_object_new_int(f->page));
    json_object_object_add(data,"page_size",json_object_new_int(f->size));json_object_object_add(data,"total",json_object_new_int64(n));
    json_object_object_add(data,"total_pages",json_object_new_int64((n+f->size-1)/f->size));json_object_put(total);return data;
}
static int mutation_result(WebResult *r,json_object *rows,int status) {
    if(!rows)return failure(r);json_object *row=take_first(rows);const char *error=strfield(row,"domain_error");
    if(error){if(!strcmp(error,"NOT_FOUND"))notfound(r);else if(!strcmp(error,"DUPLICATE"))web_error(r,409,"DUPLICADO","Já existe um registro com essa identificação ou data.");
        else if(!strcmp(error,"OUT_OF_ORDER"))web_error(r,409,"FORA_DE_ORDEM","A leitura deve ser posterior à última leitura do medidor.");
        else if(!strcmp(error,"COUNTER_DECREASED"))web_error(r,409,"CONTADOR_REDUZIDO","O contador não pode diminuir. Cadastre outro medidor em caso de substituição.");
        else if(!strcmp(error,"FUTURE_TIMESTAMP"))invalid(r,"Não é permitido registrar leitura ou instalação no futuro (tolerância de 60 segundos).");
        else if(!strcmp(error,"INACTIVE"))web_error(r,409,"MEDIDOR_INATIVO","Ative o medidor antes de registrar leituras.");
        else if(!strcmp(error,"BEFORE_INSTALL"))web_error(r,409,"ANTES_INSTALACAO","A leitura não pode preceder a instalação do medidor.");
        else if(!strcmp(error,"CATEGORY_EXCEEDS"))web_error(r,409,"VOLUME_EXCEDIDO","A soma das categorias excede o consumo medido nesse intervalo.");
        else if(!strcmp(error,"FIRST_READING"))web_error(r,409,"SEM_INTERVALO","A primeira leitura não possui consumo para distribuir em categorias.");
        else invalid(r,"A operação não atende às regras do registro.");json_object_put(row);return 1;}
    web_ok(r,status,row);return 1;
}

static int medidores(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    WebParam one[]={web_integer(u->id)};
    if(!strcmp(req->method,"GET")){json_object *rows=web_query(db,MEDIDOR_SELECT,one,1);if(!rows)return failure(r);json_object *data=json_object_new_object();json_object_object_add(data,"items",rows);web_ok(r,200,data);return 1;}
    const char *name=web_body_text(req->body,"nome"),*code=web_body_text(req->body,"codigo"),*date=web_body_text(req->body,"instalado_em_utc");
    char timestamp[20];int64_t replaces=0;json_object *replacement=field(req->body,"substitui_medidor_id");
    if(!text_ok(name,100)||!text_ok(code,64)||!web_timestamp(date,timestamp)|| (replacement && (!web_body_i64(req->body,"substitui_medidor_id",&replaces)||replaces<=0)))return invalid(r,"Informe nome, código e instalação UTC válidos.");
    WebParam p[]={web_integer(u->id),web_text(code),web_text(name),web_text(timestamp),replaces?web_integer(replaces):web_null()};
    return mutation_result(r,web_query(db,
      "SET NOCOUNT ON;SET XACT_ABORT ON;DECLARE @u bigint=?,@codigo nvarchar(64)=?,@nome nvarchar(100)=?,@data datetime2(0)=CONVERT(datetime2(0),?,126),@anterior bigint=?;"
      "BEGIN TRY BEGIN TRANSACTION;"
      "IF @data>DATEADD(second,60,SYSUTCDATETIME()) BEGIN ROLLBACK;SELECT 'FUTURE_TIMESTAMP' domain_error;RETURN;END;"
      "IF @anterior IS NOT NULL AND NOT EXISTS(SELECT 1 FROM dbo.WebMedidores WITH(UPDLOCK,HOLDLOCK) WHERE id=@anterior AND usuario_id=@u) BEGIN ROLLBACK;SELECT 'NOT_FOUND' domain_error;RETURN;END;"
      "IF EXISTS(SELECT 1 FROM dbo.WebMedidores WITH(UPDLOCK,HOLDLOCK) WHERE usuario_id=@u AND codigo=@codigo) BEGIN ROLLBACK;SELECT 'DUPLICATE' domain_error;RETURN;END;"
      "INSERT dbo.WebMedidores(usuario_id,codigo,nome,instalado_em_utc,substitui_medidor_id) VALUES(@u,@codigo,@nome,@data,@anterior);DECLARE @id bigint=SCOPE_IDENTITY();"
      "IF @anterior IS NOT NULL UPDATE dbo.WebMedidores SET status='INATIVO' WHERE id=@anterior AND usuario_id=@u;COMMIT;"
      "SELECT id,nome,codigo,status,CONVERT(varchar(19),instalado_em_utc,126)+'Z' instalado_em_utc,unidade,substitui_medidor_id FROM dbo.WebMedidores WHERE id=@id AND usuario_id=@u;"
      "END TRY BEGIN CATCH IF XACT_STATE()<>0 ROLLBACK;THROW;END CATCH;",p,5),201);
}
static int meter_patch(Database *db,const WebUser *u,const WebRequest *req,WebResult *r,int64_t id) {
    const char *status=web_body_text(req->body,"status");if(!status || (strcmp(status,"ATIVO")&&strcmp(status,"INATIVO")))return invalid(r,"Status deve ser ATIVO ou INATIVO.");
    WebParam p[]={web_text(status),web_integer(id),web_integer(u->id)};
    json_object *rows=web_query(db,"SET NOCOUNT ON;UPDATE dbo.WebMedidores SET status=? OUTPUT inserted.id,inserted.status WHERE id=? AND usuario_id=?;",p,3);
    if(!rows)return failure(r);if(!json_object_array_length(rows)){json_object_put(rows);return notfound(r);}return done_rows(r,rows,200);
}
static int reading_insert(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    int64_t meter=0,litres=0;char value[24],date[20];
    const char *amount=web_body_text(req->body,"acumulado_m3"),*observations=web_body_text(req->body,"observacoes");
    if(!web_body_i64(req->body,"medidor_id",&meter)||meter<=0 || !consumption_parse_m3(amount,&litres,value) ||
       !web_timestamp(web_body_text(req->body,"registrado_em_utc"),date) || (!optional_text_type(req->body,"observacoes") || !bounded_text(observations,1000,0,1)))return invalid(r,"Informe medidor, leitura não negativa com até três casas decimais e data UTC.");
    WebParam p[]={web_integer(u->id),web_integer(meter),web_text(value),web_text(date),observations?web_text(observations):web_null()};
    return mutation_result(r,web_query(db,
     "SET NOCOUNT ON;SET XACT_ABORT ON;DECLARE @u bigint=?,@m bigint=?,@v decimal(18,3)=CONVERT(decimal(18,3),?),@d datetime2(0)=CONVERT(datetime2(0),?,126),@obs nvarchar(1000)=?;"
     "BEGIN TRY BEGIN TRANSACTION;DECLARE @status varchar(7),@install datetime2(0);SELECT @status=status,@install=instalado_em_utc FROM dbo.WebMedidores WITH(UPDLOCK,HOLDLOCK) WHERE id=@m AND usuario_id=@u;"
     "IF @status IS NULL BEGIN ROLLBACK;SELECT 'NOT_FOUND' domain_error;RETURN;END;"
     "IF @status<>'ATIVO' BEGIN ROLLBACK;SELECT 'INACTIVE' domain_error;RETURN;END;"
     "IF @d>DATEADD(second,60,SYSUTCDATETIME()) BEGIN ROLLBACK;SELECT 'FUTURE_TIMESTAMP' domain_error;RETURN;END;"
     "IF @d<@install BEGIN ROLLBACK;SELECT 'BEFORE_INSTALL' domain_error;RETURN;END;"
     "IF EXISTS(SELECT 1 FROM dbo.WebLeiturasConsumo WITH(UPDLOCK,HOLDLOCK) WHERE medidor_id=@m AND registrado_em_utc=@d) BEGIN ROLLBACK;SELECT 'DUPLICATE' domain_error;RETURN;END;"
     "DECLARE @last datetime2(0),@counter decimal(18,3);SELECT TOP(1) @last=registrado_em_utc,@counter=acumulado_m3 FROM dbo.WebLeiturasConsumo WITH(UPDLOCK,HOLDLOCK) WHERE medidor_id=@m AND usuario_id=@u ORDER BY registrado_em_utc DESC,id DESC;"
     "IF @last IS NOT NULL AND @d<@last BEGIN ROLLBACK;SELECT 'OUT_OF_ORDER' domain_error;RETURN;END;"
     "IF @counter IS NOT NULL AND @v<@counter BEGIN ROLLBACK;SELECT 'COUNTER_DECREASED' domain_error;RETURN;END;"
     "INSERT dbo.WebLeiturasConsumo(usuario_id,medidor_id,acumulado_m3,registrado_em_utc,origem,observacoes) VALUES(@u,@m,@v,@d,'MANUAL',@obs);DECLARE @id bigint=SCOPE_IDENTITY();COMMIT;"
     "SELECT @id id,@m medidor_id,@v acumulado_m3,CONVERT(varchar(19),@d,126)+'Z' registrado_em_utc,CAST((@v-@counter)*1000 AS decimal(21,3)) consumo_litros,'MANUAL' origem,@obs observacoes;"
     "END TRY BEGIN CATCH IF XACT_STATE()<>0 ROLLBACK;THROW;END CATCH;",p,5),201);
}
/* Only constant SQL order clauses are selected. User text is never joined to SQL. */
static json_object *sorted_query(Database *db,const char *sql,const WebParam *p,size_t count,int ascending) {
    if(!ascending)return web_query(db,sql,p,count);
    const char *desc[]={"ORDER BY registrado_em_utc DESC,id DESC","ORDER BY l.registrado_em DESC,l.id DESC"};
    const char *asc[]={"ORDER BY registrado_em_utc ASC,id ASC","ORDER BY l.registrado_em ASC,l.id ASC"};
    for(size_t i=0;i<2;++i){const char *position=strstr(sql,desc[i]);if(position){
        size_t length=strlen(sql)+1,prefix=(size_t)(position-sql);char *ordered=malloc(length);if(!ordered)return NULL;
        memcpy(ordered,sql,prefix);ordered[prefix]=0;strcat(ordered,asc[i]);strcat(ordered,position+strlen(desc[i]));
        json_object *rows=web_query(db,ordered,p,count);free(ordered);return rows;}}
    return NULL;
}
static json_object *consumption_rows(Database *db,const WebUser *u,const Filters *f,int paged) {
    WebParam p[6];filter_params(p,u->id,f);p[4]=web_integer((int64_t)(f->page-1)*f->size);p[5]=web_integer(f->size);
    const char *sql_paged=FILTER_DECLARE CONSUMPTION_CTE
        "SELECT id,medidor_id,medidor_nome,acumulado_m3,consumo_litros,CONVERT(varchar(19),registrado_em_utc,126)+'Z' registrado_em_utc,CASE WHEN anterior_em_utc IS NULL THEN NULL ELSE CONVERT(varchar(19),anterior_em_utc,126)+'Z' END anterior_em_utc,origem,observacoes,t.valor_m3 tarifa_valor_m3,CONVERT(varchar(19),t.inicio_utc,126)+'Z' tarifa_inicio_utc,CAST(consumo_litros/1000*t.valor_m3 AS decimal(30,7)) estimativa_reais FROM filtered OUTER APPLY(SELECT TOP(1) valor_m3,inicio_utc FROM dbo.WebTarifas WHERE usuario_id=@u AND inicio_utc<=filtered.registrado_em_utc ORDER BY inicio_utc DESC,id DESC) t ORDER BY registrado_em_utc DESC,id DESC OFFSET ? ROWS FETCH NEXT ? ROWS ONLY;";
    const char *sql_all=FILTER_DECLARE CONSUMPTION_CTE
        "SELECT id,medidor_id,medidor_nome,acumulado_m3,consumo_litros,CONVERT(varchar(19),registrado_em_utc,126)+'Z' registrado_em_utc,CASE WHEN anterior_em_utc IS NULL THEN NULL ELSE CONVERT(varchar(19),anterior_em_utc,126)+'Z' END anterior_em_utc,origem,observacoes,t.valor_m3 tarifa_valor_m3,CONVERT(varchar(19),t.inicio_utc,126)+'Z' tarifa_inicio_utc,CAST(consumo_litros/1000*t.valor_m3 AS decimal(30,7)) estimativa_reais FROM filtered OUTER APPLY(SELECT TOP(1) valor_m3,inicio_utc FROM dbo.WebTarifas WHERE usuario_id=@u AND inicio_utc<=filtered.registrado_em_utc ORDER BY inicio_utc DESC,id DESC) t ORDER BY registrado_em_utc DESC,id DESC;";
    return sorted_query(db,paged?sql_paged:sql_all,p,paged?6:4,f->ascending);
}
static int consumption_history(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    Filters f;if(!filters(req,&f,r)||!meter_filter(db,u,&f,r))return 1;WebParam p[4];filter_params(p,u->id,&f);
    json_object *items=consumption_rows(db,u,&f,1),*total=web_query(db,FILTER_DECLARE CONSUMPTION_CTE "SELECT COUNT_BIG(*) total FROM filtered;",p,4);
    if(!items||!total){if(items)json_object_put(items);if(total)json_object_put(total);return failure(r);}web_ok(r,200,pagination(items,take_first(total),&f));return 1;
}

static json_object *category_volumes(Database *db,const WebUser *u,const Filters *f) {
    WebParam p[4];filter_params(p,u->id,f);
    return web_query(db,FILTER_DECLARE
        "SELECT c.id,c.nome,r.origem,SUM(r.litros) litros FROM dbo.WebRegistrosCategoria r JOIN dbo.WebCategoriasConsumo c ON c.id=r.categoria_id "
        "JOIN dbo.WebLeiturasConsumo l ON l.id=r.leitura_id AND l.usuario_id=r.usuario_id WHERE r.usuario_id=@u AND l.registrado_em_utc>=@inicio AND l.registrado_em_utc<@fim "
        "AND (@m IS NULL OR l.medidor_id=@m) GROUP BY c.id,c.nome,r.origem ORDER BY c.id,r.origem;",p,4);
}
static int comparison_changes(json_object *rows) {
    if(json_object_array_length(rows)!=1)return 0;
    json_object *row=json_object_array_get_idx(rows,0);
    const char *current[]={"_volume_atual","_custo_atual"},*previous[]={"total_litros_anterior","_custo_anterior"};
    const char *output[]={"variacao_percentual","variacao_financeira_percentual"};
    for(size_t i=0;i<2;i++) {
        char text[128];if(!web_decimal_change(strfield(row,current[i]),strfield(row,previous[i]),text))return 0;
        json_object *value=text[0]?json_object_new_string(text):NULL;if(text[0]&&!value)return 0;
        json_object_object_add(row,output[i],value);
    }
    json_object_object_del(row,"_volume_atual");json_object_object_del(row,"_custo_atual");json_object_object_del(row,"_custo_anterior");return 1;
}
static json_object *summary_data(Database *db,const WebUser *u,const Filters *f) {
    WebParam p[5];filter_params(p,u->id,f);p[4]=web_integer(f->month);
    json_object *summary=web_query(db,FILTER_DECLARE CONSUMPTION_CTE
      "SELECT COUNT_BIG(*) leituras,COUNT_BIG(consumo_litros) intervalos,SUM(consumo_litros) total_litros,"
      "CAST(SUM(CASE WHEN consumo_litros IS NOT NULL THEN CAST(DATEDIFF_BIG(second,anterior_em_utc,registrado_em_utc) AS decimal(25,6)) END)/86400 AS decimal(25,6)) dias_observados,"
      "CAST(SUM(consumo_litros)*86400.0/NULLIF(SUM(CASE WHEN consumo_litros IS NOT NULL THEN CAST(DATEDIFF_BIG(second,anterior_em_utc,registrado_em_utc) AS decimal(25,6)) END),0) AS decimal(38,3)) media_diaria_litros,"
      "COUNT_BIG(CASE WHEN consumo_litros IS NOT NULL AND t.id IS NULL THEN 1 END) intervalos_sem_tarifa,COUNT_BIG(DISTINCT CASE WHEN consumo_litros IS NOT NULL THEN t.id END) tarifas_distintas,"
      "CASE WHEN COUNT_BIG(DISTINCT CASE WHEN consumo_litros IS NOT NULL THEN t.id END)=1 THEN MAX(t.valor_m3) ELSE NULL END tarifa_valor_m3,"
      "CASE WHEN COUNT_BIG(DISTINCT CASE WHEN consumo_litros IS NOT NULL THEN t.id END)=1 THEN MAX(CONVERT(varchar(19),t.inicio_utc,126)+'Z') ELSE NULL END tarifa_inicio_utc,"
      "CASE WHEN COUNT_BIG(CASE WHEN consumo_litros IS NOT NULL AND t.id IS NULL THEN 1 END)>0 THEN NULL ELSE CAST(SUM(consumo_litros/1000*t.valor_m3) AS decimal(30,2)) END estimativa_reais "
      "FROM filtered OUTER APPLY(SELECT TOP(1) id,valor_m3,inicio_utc FROM dbo.WebTarifas WHERE usuario_id=@u AND inicio_utc<=filtered.registrado_em_utc ORDER BY inicio_utc DESC,id DESC) t;",p,4);
    json_object *series=web_query(db,FILTER_DECLARE "DECLARE @monthly bit=?; " CONSUMPTION_CTE
      "SELECT CASE WHEN @monthly=1 THEN LEFT(CONVERT(varchar(10),registrado_em_utc,23),7) ELSE CONVERT(varchar(10),registrado_em_utc,23) END periodo,SUM(consumo_litros) total_litros,COUNT_BIG(consumo_litros) intervalos "
      "FROM filtered WHERE consumo_litros IS NOT NULL GROUP BY CASE WHEN @monthly=1 THEN LEFT(CONVERT(varchar(10),registrado_em_utc,23),7) ELSE CONVERT(varchar(10),registrado_em_utc,23) END ORDER BY periodo;",p,5);
    /* The preceding period has the same duration in seconds, bounded by SQL datetime2 minimum. */
    json_object *comparison=web_query(db,FILTER_DECLARE
      "DECLARE @seconds bigint=DATEDIFF_BIG(second,@inicio,@fim),@available bigint=DATEDIFF_BIG(second,CONVERT(datetime2(0),'0001-01-01T00:00:00',126),@inicio);IF @seconds>@available SET @seconds=@available;DECLARE @previous_start datetime2(0)=DATEADD(second,CONVERT(int,-(@seconds%86400)),DATEADD(day,CONVERT(int,-(@seconds/86400)),@inicio)); " CONSUMPTION_CTE
      ", previous AS(SELECT SUM(i.consumo_litros) volume,CASE WHEN COUNT_BIG(CASE WHEN i.consumo_litros IS NOT NULL AND t.id IS NULL THEN 1 END)>0 THEN NULL ELSE SUM(i.consumo_litros/1000*t.valor_m3) END custo FROM intervals i OUTER APPLY(SELECT TOP(1) id,valor_m3 FROM dbo.WebTarifas WHERE usuario_id=@u AND inicio_utc<=i.registrado_em_utc ORDER BY inicio_utc DESC,id DESC) t WHERE i.registrado_em_utc<@inicio AND i.registrado_em_utc>=@previous_start),"
      "currentperiod AS(SELECT SUM(f.consumo_litros) volume,CASE WHEN COUNT_BIG(CASE WHEN f.consumo_litros IS NOT NULL AND t.id IS NULL THEN 1 END)>0 THEN NULL ELSE SUM(f.consumo_litros/1000*t.valor_m3) END custo FROM filtered f OUTER APPLY(SELECT TOP(1) id,valor_m3 FROM dbo.WebTarifas WHERE usuario_id=@u AND inicio_utc<=f.registrado_em_utc ORDER BY inicio_utc DESC,id DESC) t) SELECT previous.volume total_litros_anterior,"
      "currentperiod.volume _volume_atual,CAST(previous.custo AS decimal(30,2)) estimativa_reais_anterior,previous.custo _custo_anterior,currentperiod.custo _custo_atual FROM previous CROSS JOIN currentperiod;",p,4);
    json_object *applied=web_query(db,FILTER_DECLARE CONSUMPTION_CTE
      "SELECT t.id,t.valor_m3,CONVERT(varchar(19),t.inicio_utc,126)+'Z' inicio_utc,COUNT_BIG(*) intervalos,SUM(f.consumo_litros) litros,CAST(SUM(f.consumo_litros/1000*t.valor_m3) AS decimal(30,2)) estimativa_reais FROM filtered f CROSS APPLY(SELECT TOP(1) id,valor_m3,inicio_utc FROM dbo.WebTarifas WHERE usuario_id=@u AND inicio_utc<=f.registrado_em_utc ORDER BY inicio_utc DESC,id DESC) t WHERE f.consumo_litros IS NOT NULL GROUP BY t.id,t.valor_m3,t.inicio_utc ORDER BY t.inicio_utc;",p,4);
    json_object *cats=category_volumes(db,u,f);
    if(!summary||!series||!comparison||!cats||!applied||!comparison_changes(comparison)){if(summary)json_object_put(summary);if(series)json_object_put(series);if(comparison)json_object_put(comparison);if(cats)json_object_put(cats);if(applied)json_object_put(applied);return NULL;}
    json_object *data=json_object_new_object();char timestamp[22];
    snprintf(timestamp,sizeof(timestamp),"%sZ",f->inicio);json_object_object_add(data,"inicio",json_object_new_string(timestamp));
    snprintf(timestamp,sizeof(timestamp),"%sZ",f->fim);json_object_object_add(data,"fim",json_object_new_string(timestamp));
    json_object_object_add(data,"agrupamento",json_object_new_string(f->month?"mes":"dia"));
    json_object_object_add(data,"resumo",take_first(summary));json_object_object_add(data,"serie",series);
    json_object_object_add(data,"comparacao",take_first(comparison));json_object_object_add(data,"categorias",cats);json_object_object_add(data,"tarifas_aplicadas",applied);
    json_object_object_add(data,"nota_atribuicao",json_object_new_string("O consumo de cada intervalo é atribuído à data da leitura final. Não há distribuição artificial entre dias sem medição."));
    json_object_object_add(data,"base_media",json_object_new_string("Total dividido pela soma da duração dos intervalos observados dos medidores, em dias."));
    json_object_object_add(data,"base_estimativa",json_object_new_string("Soma dos volumes de cada intervalo em m³ multiplicados pela tarifa vigente na data de sua leitura final. Sem tarifa para qualquer intervalo, a estimativa total é desconhecida. Não inclui faixas, esgoto ou taxas fixas."));
    return data;
}
static int consumption_summary(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    Filters f;if(!filters(req,&f,r)||!meter_filter(db,u,&f,r))return 1;json_object *data=summary_data(db,u,&f);if(!data)return failure(r);web_ok(r,200,data);return 1;
}
static json_object *quality_rows(Database *db,const WebUser *u,const Filters *f,int paged) {
    WebParam p[]={web_integer(u->id),web_text(f->inicio),web_text(f->fim),web_integer((int64_t)(f->page-1)*f->size),web_integer(f->size)};
    const char *paged_sql="SELECT " QUALITY_COLUMNS " FROM dbo.WebQualidadeUsuarios q JOIN dbo.Leituras l ON l.id=q.leitura_id WHERE q.usuario_id=? AND l.registrado_em_utc=1 AND l.registrado_em>=CONVERT(datetime2(0),?,126) AND l.registrado_em<CONVERT(datetime2(0),?,126) ORDER BY l.registrado_em DESC,l.id DESC OFFSET ? ROWS FETCH NEXT ? ROWS ONLY;";
    const char *all_sql="SELECT " QUALITY_COLUMNS " FROM dbo.WebQualidadeUsuarios q JOIN dbo.Leituras l ON l.id=q.leitura_id WHERE q.usuario_id=? AND l.registrado_em_utc=1 AND l.registrado_em>=CONVERT(datetime2(0),?,126) AND l.registrado_em<CONVERT(datetime2(0),?,126) ORDER BY l.registrado_em DESC,l.id DESC;";
    return sorted_query(db,paged?paged_sql:all_sql,p,paged?5:3,f->ascending);
}
static int quality_history(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    Filters f;if(!filters(req,&f,r))return 1;
    /* An unfiltered quality history includes legacy records whose timezone is unknown. */
    char scratch[80];int bounded=web_query_value(req->query,"inicio",scratch,sizeof(scratch))>0 || web_query_value(req->query,"fim",scratch,sizeof(scratch))>0;
    WebParam p[]={web_integer(u->id),web_text(f.inicio),web_text(f.fim),web_integer((int64_t)(f.page-1)*f.size),web_integer(f.size)};
    json_object *items=bounded?quality_rows(db,u,&f,1):sorted_query(db,"SELECT " QUALITY_COLUMNS " FROM dbo.WebQualidadeUsuarios q JOIN dbo.Leituras l ON l.id=q.leitura_id WHERE q.usuario_id=? ORDER BY l.registrado_em DESC,l.id DESC OFFSET ? ROWS FETCH NEXT ? ROWS ONLY;",(WebParam[]){p[0],p[3],p[4]},3,f.ascending);
    json_object *total=bounded?web_query(db,"SELECT COUNT_BIG(*) total FROM dbo.WebQualidadeUsuarios q JOIN dbo.Leituras l ON l.id=q.leitura_id WHERE q.usuario_id=? AND l.registrado_em_utc=1 AND l.registrado_em>=CONVERT(datetime2(0),?,126) AND l.registrado_em<CONVERT(datetime2(0),?,126);",p,3):web_query(db,"SELECT COUNT_BIG(*) total FROM dbo.WebQualidadeUsuarios WHERE usuario_id=?;",p,1);
    if(!items||!total){if(items)json_object_put(items);if(total)json_object_put(total);return failure(r);}web_ok(r,200,pagination(items,take_first(total),&f));return 1;
}
static int number_body(json_object *body,const char *key,double *out) {
    json_object *v=field(body,key);if(!v || (json_object_get_type(v)!=json_type_double && json_object_get_type(v)!=json_type_int))return 0;
    *out=json_object_get_double(v);return isfinite(*out);
}
static int quality_insert(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    double temperature,ph,turbidity;
    if(!number_body(req->body,"temperatura_c",&temperature)||!number_body(req->body,"ph",&ph)||!number_body(req->body,"turbidez_ntu",&turbidity)||!input_valid_measurement(temperature,ph,turbidity))return invalid(r,"Informe temperatura entre -20 e 100 °C, pH de 0 a 14 e turbidez não negativa.");
    WebParam p[]={web_integer(u->id),web_real(temperature),web_real(ph),web_real(turbidity)};
    return done_rows(r,web_query(db,"SET NOCOUNT ON;SET XACT_ABORT ON;DECLARE @u bigint=?,@t float=?,@ph float=?,@ntu float=?;BEGIN TRY BEGIN TRANSACTION;"
      "INSERT dbo.Leituras(temperatura_c,ph,turbidez_ntu,origem,registrado_em_utc,sensor_id,local_id) VALUES(@t,@ph,@ntu,'MANUAL',1,1,1);DECLARE @id bigint=SCOPE_IDENTITY();"
      "INSERT dbo.WebQualidadeUsuarios(leitura_id,usuario_id,origem_vinculo) VALUES(@id,@u,'WEB');COMMIT;"
      "SELECT " QUALITY_COLUMNS " FROM dbo.WebQualidadeUsuarios q JOIN dbo.Leituras l ON l.id=q.leitura_id WHERE q.usuario_id=@u AND l.id=@id;"
      "END TRY BEGIN CATCH IF XACT_STATE()<>0 ROLLBACK;THROW;END CATCH;",p,4),201);
}
static int import_legacy(Database *db,const WebUser *u,WebResult *r) {
    if(!u->administrador){web_error(r,403,"ADMINISTRADOR_NECESSARIO","Somente o administrador local pode vincular leituras anteriores.");return 1;}
    WebParam p[]={web_integer(u->id)};
    return done_rows(r,web_query(db,"SET NOCOUNT ON;SET XACT_ABORT ON;DECLARE @u bigint=?;BEGIN TRY BEGIN TRANSACTION;"
      "INSERT dbo.WebQualidadeUsuarios(leitura_id,usuario_id,origem_vinculo) SELECT l.id,@u,'LEGADO_EXPLICITO' FROM dbo.Leituras l WITH(UPDLOCK,HOLDLOCK) WHERE NOT EXISTS(SELECT 1 FROM dbo.WebQualidadeUsuarios q WITH(UPDLOCK,HOLDLOCK) WHERE q.leitura_id=l.id);DECLARE @n int=@@ROWCOUNT;COMMIT;SELECT @n importadas;"
      "END TRY BEGIN CATCH IF XACT_STATE()<>0 ROLLBACK;THROW;END CATCH;",p,1),200);
}
static json_object *quality_latest(Database *db,const WebUser *u) {
    WebParam p[]={web_integer(u->id)};
    json_object *rows=web_query(db,"SELECT TOP(1) " QUALITY_COLUMNS " FROM dbo.WebQualidadeUsuarios q JOIN dbo.Leituras l ON l.id=q.leitura_id WHERE q.usuario_id=? ORDER BY l.registrado_em DESC,l.id DESC;",p,1),*prefs=web_query(db,PREF_SELECT,p,1);
    if(!rows||!prefs){if(rows)json_object_put(rows);if(prefs)json_object_put(prefs);return NULL;}
    json_object *data=json_object_new_object(),*reading=json_object_array_length(rows)?json_object_get(json_object_array_get_idx(rows,0)):NULL,*pref=json_object_array_length(prefs)?json_object_array_get_idx(prefs,0):NULL;
    const char *state="SEM_DADOS";int configured=0,violation=0;
    const char *limits[]={"ph_min","ph_max","temperatura_min","temperatura_max","turbidez_max"};
    const char *measures[]={"ph","ph","temperatura_c","temperatura_c","turbidez_ntu"};
    if(reading){for(int i=0;i<5;++i){json_object *v=field(pref,limits[i]);if(v && json_object_get_type(v)!=json_type_null){configured=1;double value=json_object_get_double(field(reading,measures[i])),threshold=json_object_get_double(v);if((i==0||i==2)?value<threshold:value>threshold)violation=1;}}
        state=!configured?"SEM_REFERENCIA":violation?"FORA_LIMITES_CONFIGURADOS":"DENTRO_LIMITES_CONFIGURADOS";}
    json_object_object_add(data,"ultima",reading);json_object_object_add(data,"estado",json_object_new_string(state));
    json_object_object_add(data,"nota",json_object_new_string("Avaliação apenas dos parâmetros medidos e limites configurados pelo usuário. Não determina potabilidade."));
    json_object_put(rows);json_object_put(prefs);return data;
}

static int email_valid(const char *email) {
    if(!email || strlen(email)<3 || strlen(email)>254)return 0;
    int at=0;
    for(size_t i=0;email[i];++i){unsigned ch=(unsigned char)email[i];
        if(ch<=32||ch>=127)return 0;
        if(ch=='@'){if(at||i==0||!email[i+1])return 0;at=1;}
        else if(!(isalnum((int)ch)||strchr(".!#$%&'*+-/=?^_`{|}~",(int)ch)))return 0;
    }
    return at;
}
static const char *decimal_text(json_object *value) {
    if(!value)return NULL;enum json_type type=json_object_get_type(value);
    if(type!=json_type_string && type!=json_type_int && type!=json_type_double)return NULL;
    const char *text=json_object_get_string(value);
    if(type==json_type_string && (size_t)json_object_get_string_len(value)!=strlen(text))return NULL;
    return text;
}
static json_object *settings_data(Database *db,const WebUser *u) {
    WebParam p[]={web_integer(u->id)};
    json_object *user=web_query(db,"SELECT id,nome,email,administrador FROM dbo.WebUsuarios WHERE id=? AND ativo=1;",p,1),*pref=web_query(db,PREF_SELECT,p,1),*meters=web_query(db,MEDIDOR_SELECT,p,1),*rates=web_query(db,TARIFA_SELECT,p,1);
    if(!user||!pref||!meters||!rates){if(user)json_object_put(user);if(pref)json_object_put(pref);if(meters)json_object_put(meters);if(rates)json_object_put(rates);return NULL;}
    json_object *data=json_object_new_object();json_object_object_add(data,"usuario",take_first(user));json_object_object_add(data,"preferencias",take_first(pref));json_object_object_add(data,"medidores",meters);json_object_object_add(data,"tarifas",rates);return data;
}
static int settings_get(Database *db,const WebUser *u,WebResult *r) {json_object *data=settings_data(db,u);if(!data)return failure(r);web_ok(r,200,data);return 1;}
static int settings_patch(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    if(!req->body || json_object_get_type(req->body)!=json_type_object)return invalid(r,"Informe um objeto de configurações.");
    json_object *data=settings_data(db,u);if(!data)return failure(r);
    json_object *existing=field(data,"preferencias"),*profile=field(req->body,"usuario"),*changes=field(req->body,"preferencias");
    if(!profile)profile=field(req->body,"perfil");if(!profile)profile=req->body;if(!changes)changes=req->body;
    if(json_object_get_type(profile)!=json_type_object || json_object_get_type(changes)!=json_type_object){json_object_put(data);return invalid(r,"Perfil e preferências devem ser objetos.");}
    json_object *profile_value=NULL;int has_name=json_object_object_get_ex(profile,"nome",&profile_value),has_email=json_object_object_get_ex(profile,"email",&profile_value);
    if((has_name && !strfield(profile,"nome")) || (has_email && !strfield(profile,"email"))){json_object_put(data);return invalid(r,"Nome e e-mail devem ser textos.");}
    const char *name=strfield(profile,"nome"),*email=strfield(profile,"email");
    if(!name)name=strfield(field(data,"usuario"),"nome");if(!email)email=strfield(field(data,"usuario"),"email");
    if(!text_ok(name,100)||!email_valid(email)){json_object_put(data);return invalid(r,"Nome ou e-mail inválido.");}
    const char *keys[]={"limite_diario_litros","limite_mensal_litros","vazamento_litros_minuto","ph_min","ph_max","temperatura_min","temperatura_max","turbidez_max","unidade","tema","notificacoes"};
    char decimals[3][24];WebParam values[11];
    for(int i=0;i<11;++i){json_object *candidate=NULL;int present=json_object_object_get_ex(changes,keys[i],&candidate);json_object *value=present?candidate:field(existing,keys[i]);
        if(i<3){int64_t scaled=0;if(!value || json_object_get_type(value)==json_type_null){values[i]=web_null();continue;}
            if(json_object_get_type(value)!=json_type_string && json_object_get_type(value)!=json_type_int && json_object_get_type(value)!=json_type_double){json_object_put(data);return invalid(r,"Limites devem ser decimais positivos ou null.");}
            if(!consumption_parse_decimal(decimal_text(value),i==2?4:3,&scaled,decimals[i],sizeof(decimals[i])) || scaled<=0 || (i==2 && scaled>INT64_C(999999999999))){json_object_put(data);return invalid(r,"Limites devem ser decimais positivos dentro da precisão suportada.");}values[i]=web_text(decimals[i]);
        }else if(i<8){if(!value || json_object_get_type(value)==json_type_null){values[i]=web_null();continue;}
            if(json_object_get_type(value)!=json_type_double && json_object_get_type(value)!=json_type_int){json_object_put(data);return invalid(r,"Referências de qualidade devem ser números ou null.");}
            double n=json_object_get_double(value);if(!isfinite(n)||(i<5&&(n<0||n>14))||(i>=5&&i<7&&(n< -20||n>100))||(i==7&&n<0)){json_object_put(data);return invalid(r,"Referência de qualidade fora da faixa suportada.");}values[i]=web_real(n);
        }else if(i<10){const char *s=value?json_object_get_string(value):NULL;if(!s || (i==8?(strcmp(s,"litros")&&strcmp(s,"m3")):(strcmp(s,"claro")&&strcmp(s,"escuro")))){json_object_put(data);return invalid(r,"Unidade ou tema inválido.");}values[i]=web_text(s);
        }else{if(!value || json_object_get_type(value)!=json_type_boolean){json_object_put(data);return invalid(r,"Notificações deve ser verdadeiro ou falso.");}values[i]=web_integer(json_object_get_boolean(value));}}
    if((values[3].kind!=WEB_NULL && values[4].kind!=WEB_NULL && values[3].real>values[4].real)||(values[5].kind!=WEB_NULL&&values[6].kind!=WEB_NULL&&values[5].real>values[6].real)){json_object_put(data);return invalid(r,"O limite mínimo não pode exceder o máximo.");}
    char normalized_email[255];for(size_t i=0;i<=strlen(email);++i)normalized_email[i]=(char)tolower((unsigned char)email[i]);
    WebParam p[14]={web_integer(u->id),web_text(name),web_text(normalized_email)};for(int i=0;i<11;++i)p[i+3]=values[i];
    json_object *rows=web_query(db,"SET NOCOUNT ON;SET XACT_ABORT ON;DECLARE @u bigint=?,@nome nvarchar(100)=?,@email varchar(254)=?;"
      "BEGIN TRY BEGIN TRANSACTION;IF EXISTS(SELECT 1 FROM dbo.WebUsuarios WITH(UPDLOCK,HOLDLOCK) WHERE email_norm=LOWER(@email) AND id<>@u) BEGIN ROLLBACK;SELECT 'DUPLICATE' domain_error;RETURN;END;"
      "UPDATE dbo.WebUsuarios SET nome=@nome,email=@email WHERE id=@u;IF NOT EXISTS(SELECT 1 FROM dbo.WebPreferencias WITH(UPDLOCK,HOLDLOCK) WHERE usuario_id=@u) INSERT dbo.WebPreferencias(usuario_id) VALUES(@u);"
      "UPDATE dbo.WebPreferencias SET limite_diario_litros=CONVERT(decimal(18,3),?),limite_mensal_litros=CONVERT(decimal(18,3),?),vazamento_litros_minuto=CONVERT(decimal(12,4),?),ph_min=?,ph_max=?,temperatura_min=?,temperatura_max=?,turbidez_max=?,unidade=?,tema=?,notificacoes=?,atualizado_em_utc=SYSUTCDATETIME() WHERE usuario_id=@u;COMMIT;SELECT @u id;"
      "END TRY BEGIN CATCH IF XACT_STATE()<>0 ROLLBACK;THROW;END CATCH;",p,14);
    json_object_put(data);if(!rows)return failure(r);json_object *row=take_first(rows);if(strfield(row,"domain_error")){json_object_put(row);web_error(r,409,"EMAIL_DUPLICADO","Esse e-mail já está cadastrado.");return 1;}json_object_put(row);return settings_get(db,u,r);
}
static int tariffs(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    WebParam one[]={web_integer(u->id)};
    if(!strcmp(req->method,"GET")){json_object *rows=web_query(db,TARIFA_SELECT,one,1);if(!rows)return failure(r);json_object *data=json_object_new_object();json_object_object_add(data,"items",rows);web_ok(r,200,data);return 1;}
    int64_t scaled;char value[24],date[20];json_object *amount=field(req->body,"valor_m3");const char *text=decimal_text(amount);
    if(!consumption_parse_decimal(text,4,&scaled,value,sizeof(value)) || scaled>INT64_C(999999999999) || !web_timestamp(web_body_text(req->body,"inicio_utc"),date))return invalid(r,"Informe tarifa não negativa com até quatro casas decimais e início UTC.");
    WebParam p[]={web_integer(u->id),web_text(value),web_text(date)};
    return mutation_result(r,web_query(db,"SET NOCOUNT ON;SET XACT_ABORT ON;DECLARE @u bigint=?,@v decimal(12,4)=CONVERT(decimal(12,4),?),@d datetime2(0)=CONVERT(datetime2(0),?,126);BEGIN TRY BEGIN TRANSACTION;"
      "IF EXISTS(SELECT 1 FROM dbo.WebTarifas WITH(UPDLOCK,HOLDLOCK) WHERE usuario_id=@u AND inicio_utc=@d) BEGIN ROLLBACK;SELECT 'DUPLICATE' domain_error;RETURN;END;"
      "INSERT dbo.WebTarifas(usuario_id,valor_m3,inicio_utc) VALUES(@u,@v,@d);DECLARE @id bigint=SCOPE_IDENTITY();COMMIT;SELECT @id id,@v valor_m3,CONVERT(varchar(19),@d,126)+'Z' inicio_utc;END TRY BEGIN CATCH IF XACT_STATE()<>0 ROLLBACK;THROW;END CATCH;",p,3),201);
}
static int categories_get(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    Filters f;if(!filters(req,&f,r)||!meter_filter(db,u,&f,r))return 1;WebParam p[4];filter_params(p,u->id,&f);
    json_object *items=web_query(db,FILTER_DECLARE "SELECT c.id,c.nome,SUM(v.litros) litros FROM dbo.WebCategoriasConsumo c LEFT JOIN(SELECT r.categoria_id,r.litros FROM dbo.WebRegistrosCategoria r JOIN dbo.WebLeiturasConsumo l ON l.id=r.leitura_id AND l.usuario_id=r.usuario_id WHERE r.usuario_id=@u AND l.registrado_em_utc>=@inicio AND l.registrado_em_utc<@fim AND(@m IS NULL OR l.medidor_id=@m)) v ON v.categoria_id=c.id GROUP BY c.id,c.nome ORDER BY c.id;",p,4);
    json_object *records=web_query(db,FILTER_DECLARE "SELECT r.id,r.leitura_id,r.categoria_id,c.nome,r.litros,r.origem,CONVERT(varchar(19),r.criado_em_utc,126)+'Z' criado_em_utc FROM dbo.WebRegistrosCategoria r JOIN dbo.WebCategoriasConsumo c ON c.id=r.categoria_id JOIN dbo.WebLeiturasConsumo l ON l.id=r.leitura_id AND l.usuario_id=r.usuario_id WHERE r.usuario_id=@u AND l.registrado_em_utc>=@inicio AND l.registrado_em_utc<@fim AND(@m IS NULL OR l.medidor_id=@m) ORDER BY l.registrado_em_utc DESC,r.id DESC;",p,4);
    if(!items||!records){if(items)json_object_put(items);if(records)json_object_put(records);return failure(r);}json_object *data=json_object_new_object();json_object_object_add(data,"items",items);json_object_object_add(data,"registros",records);web_ok(r,200,data);return 1;
}
static int category_insert(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    int64_t reading,category,scaled;char value[24];const char *origin=web_body_text(req->body,"origem");json_object *amount=field(req->body,"litros");
    if(!web_body_i64(req->body,"leitura_id",&reading)||reading<=0||!web_body_i64(req->body,"categoria_id",&category)||category<=0||category>INT_MAX||!origin||(strcmp(origin,"INFORMADO")&&strcmp(origin,"MEDIDO")&&strcmp(origin,"ESTIMADO"))||!amount||!consumption_parse_decimal(decimal_text(amount),3,&scaled,value,sizeof(value))||scaled<=0)return invalid(r,"Informe leitura, categoria, volume positivo e origem INFORMADO, MEDIDO ou ESTIMADO.");
    WebParam p[]={web_integer(u->id),web_integer(reading),web_integer(category),web_text(value),web_text(origin)};
    return mutation_result(r,web_query(db,"SET NOCOUNT ON;SET XACT_ABORT ON;DECLARE @u bigint=?,@l bigint=?,@c int=?,@litros decimal(18,3)=CONVERT(decimal(18,3),?),@origem varchar(9)=?;BEGIN TRY BEGIN TRANSACTION;"
      "DECLARE @m bigint,@d datetime2(0),@v decimal(18,3),@prev decimal(18,3);SELECT @m=medidor_id,@d=registrado_em_utc,@v=acumulado_m3 FROM dbo.WebLeiturasConsumo WITH(UPDLOCK,HOLDLOCK) WHERE id=@l AND usuario_id=@u;"
      "IF @m IS NULL OR NOT EXISTS(SELECT 1 FROM dbo.WebCategoriasConsumo WHERE id=@c) BEGIN ROLLBACK;SELECT 'NOT_FOUND' domain_error;RETURN;END;"
      "SELECT TOP(1) @prev=acumulado_m3 FROM dbo.WebLeiturasConsumo WHERE usuario_id=@u AND medidor_id=@m AND registrado_em_utc<@d ORDER BY registrado_em_utc DESC,id DESC;"
      "IF @prev IS NULL BEGIN ROLLBACK;SELECT 'FIRST_READING' domain_error;RETURN;END;"
      "DECLARE @allocated decimal(38,3);SELECT @allocated=COALESCE(SUM(litros),0) FROM dbo.WebRegistrosCategoria WITH(UPDLOCK,HOLDLOCK) WHERE usuario_id=@u AND leitura_id=@l;"
      "IF @allocated+@litros>(@v-@prev)*1000 BEGIN ROLLBACK;SELECT 'CATEGORY_EXCEEDS' domain_error;RETURN;END;"
      "INSERT dbo.WebRegistrosCategoria(usuario_id,leitura_id,categoria_id,litros,origem) VALUES(@u,@l,@c,@litros,@origem);DECLARE @id bigint=SCOPE_IDENTITY();COMMIT;SELECT @id id,@l leitura_id,@c categoria_id,@litros litros,@origem origem;END TRY BEGIN CATCH IF XACT_STATE()<>0 ROLLBACK;THROW;END CATCH;",p,5),201);
}

/* Persistent occurrences are deduplicated independently of status. Resolving an
 * occurrence never recreates it; each transition has an immutable history row. */
static int refresh_alerts(Database *db,const WebUser *u) {
    WebParam p[]={web_integer(u->id)};
    const char *const fragments[] = {
      "SET NOCOUNT ON;SET XACT_ABORT ON;DECLARE @u bigint=?;BEGIN TRY BEGIN TRANSACTION;DECLARE @lock int;"
      "DECLARE @resource nvarchar(255)=N'AquaSense.WebAlertas.'+CONVERT(nvarchar(20),@u);EXEC @lock=sys.sp_getapplock @Resource=@resource,@LockMode='Exclusive',@LockOwner='Transaction',@LockTimeout=10000;IF @lock<0 THROW 51720,'Falha ao bloquear geracao de alertas.',1;"
      "DECLARE @daily decimal(18,3),@monthly decimal(18,3),@leak decimal(12,4),@phmin float,@phmax float,@tmin float,@tmax float,@ntumax float;"
      "SELECT @daily=limite_diario_litros,@monthly=limite_mensal_litros,@leak=vazamento_litros_minuto,@phmin=ph_min,@phmax=ph_max,@tmin=temperatura_min,@tmax=temperatura_max,@ntumax=turbidez_max FROM dbo.WebPreferencias WHERE usuario_id=@u;"
      "DECLARE @candidate TABLE(codigo varchar(128) PRIMARY KEY,tipo varchar(20),descricao nvarchar(1000),severidade varchar(8),consumo_id bigint NULL,qualidade_id bigint NULL);"
      "WITH o AS(SELECT id,medidor_id,registrado_em_utc,acumulado_m3,LAG(acumulado_m3) OVER(PARTITION BY medidor_id ORDER BY registrado_em_utc,id) anterior FROM dbo.WebLeiturasConsumo WHERE usuario_id=@u),"
      "i AS(SELECT *,CAST((acumulado_m3-anterior)*1000 AS decimal(21,3)) litros FROM o) "
      "INSERT @candidate SELECT 'DIARIO:'+CONVERT(varchar(10),registrado_em_utc,23),'CONSUMO_ELEVADO',N'Consumo observado de '+CONVERT(nvarchar(60),SUM(litros))+N' L atribuído ao dia '+CONVERT(varchar(10),registrado_em_utc,23)+N', acima do limite configurado de '+CONVERT(nvarchar(60),@daily)+N' L.','MEDIA',MAX(id),NULL FROM i WHERE litros IS NOT NULL AND @daily IS NOT NULL GROUP BY CONVERT(varchar(10),registrado_em_utc,23) HAVING SUM(litros)>@daily;"
      "WITH o AS(SELECT id,registrado_em_utc,acumulado_m3,LAG(acumulado_m3) OVER(PARTITION BY medidor_id ORDER BY registrado_em_utc,id) anterior FROM dbo.WebLeiturasConsumo WHERE usuario_id=@u),"
      "i AS(SELECT *,CAST((acumulado_m3-anterior)*1000 AS decimal(21,3)) litros FROM o) "
      "INSERT @candidate SELECT 'MENSAL:'+LEFT(CONVERT(varchar(10),registrado_em_utc,23),7),'CONSUMO_ELEVADO',N'Consumo observado de '+CONVERT(nvarchar(60),SUM(litros))+N' L atribuído ao mês '+LEFT(CONVERT(varchar(10),registrado_em_utc,23),7)+N', acima do limite configurado de '+CONVERT(nvarchar(60),@monthly)+N' L.','MEDIA',MAX(id),NULL FROM i WHERE litros IS NOT NULL AND @monthly IS NOT NULL GROUP BY LEFT(CONVERT(varchar(10),registrado_em_utc,23),7) HAVING SUM(litros)>@monthly;",
      "WITH o AS(SELECT id,medidor_id,registrado_em_utc,acumulado_m3,LAG(acumulado_m3) OVER(PARTITION BY medidor_id ORDER BY registrado_em_utc,id) anterior,LAG(registrado_em_utc) OVER(PARTITION BY medidor_id ORDER BY registrado_em_utc,id) antes FROM dbo.WebLeiturasConsumo WHERE usuario_id=@u),"
      "i AS(SELECT *,CASE WHEN DATEDIFF_BIG(second,antes,registrado_em_utc) BETWEEN 1 AND 3600 AND (acumulado_m3-anterior)*60000>@leak*DATEDIFF_BIG(second,antes,registrado_em_utc) THEN 1 ELSE 0 END acima FROM o),"
      "windows AS(SELECT *,SUM(acima) OVER(PARTITION BY medidor_id ORDER BY registrado_em_utc,id ROWS BETWEEN 2 PRECEDING AND CURRENT ROW) sequencia,COUNT(*) OVER(PARTITION BY medidor_id ORDER BY registrado_em_utc,id ROWS BETWEEN 2 PRECEDING AND CURRENT ROW) n FROM i),"
      "suspeitas AS(SELECT *,ROW_NUMBER() OVER(PARTITION BY medidor_id,CONVERT(varchar(10),registrado_em_utc,23) ORDER BY registrado_em_utc,id) ordinal FROM windows WHERE n=3 AND sequencia=3 AND @leak IS NOT NULL) "
      "INSERT @candidate SELECT 'VAZAMENTO:'+CONVERT(varchar(20),medidor_id)+':'+CONVERT(varchar(10),registrado_em_utc,23),'POSSIVEL_VAZAMENTO',N'Indício de consumo contínuo anormal: três intervalos consecutivos de até 60 minutos excederam '+CONVERT(nvarchar(60),@leak)+N' L/min. Quatro leituras sustentam o indício; confirme o uso e inspecione a instalação. Não comprova vazamento.','ALTA',id,NULL FROM suspeitas WHERE ordinal=1;"
      "INSERT @candidate SELECT 'QUALIDADE:'+CONVERT(varchar(20),l.id)+':'+v.parametro,'QUALIDADE',N'Medição de '+v.parametro+N' fora da referência configurada: '+CONVERT(nvarchar(60),v.valor)+N'. Não determina potabilidade.','MEDIA',NULL,l.id FROM dbo.Leituras l JOIN dbo.WebQualidadeUsuarios q ON q.leitura_id=l.id AND q.usuario_id=@u "
      "CROSS APPLY(VALUES(N'ph',l.ph,@phmin,@phmax),(N'temperatura_c',l.temperatura_c,@tmin,@tmax),(N'turbidez_ntu',l.turbidez_ntu,CAST(NULL AS float),@ntumax)) v(parametro,valor,minimo,maximo) WHERE (v.minimo IS NOT NULL AND v.valor<v.minimo) OR(v.maximo IS NOT NULL AND v.valor>v.maximo);"
      "DECLARE @created TABLE(id bigint);INSERT dbo.WebAlertas(usuario_id,tipo,descricao,severidade,codigo_ocorrencia,leitura_consumo_id,leitura_qualidade_id) OUTPUT inserted.id INTO @created "
      "SELECT @u,c.tipo,c.descricao,c.severidade,c.codigo,c.consumo_id,c.qualidade_id FROM @candidate c WHERE NOT EXISTS(SELECT 1 FROM dbo.WebAlertas a WITH(UPDLOCK,HOLDLOCK) WHERE a.usuario_id=@u AND a.codigo_ocorrencia=c.codigo);"
      "INSERT dbo.WebAlertasHistorico(alerta_id,usuario_id,status_anterior,status_novo,observacao) SELECT id,@u,NULL,'ABERTO',N'Gerado a partir de medições e referências configuradas.' FROM @created;COMMIT;SELECT COUNT_BIG(*) gerados FROM @created;"
      "END TRY BEGIN CATCH IF XACT_STATE()<>0 ROLLBACK;THROW;END CATCH;"};
    size_t length=1;for(size_t i=0;i<sizeof(fragments)/sizeof(fragments[0]);++i)length+=strlen(fragments[i]);
    char *sql=malloc(length);if(!sql)return 0;sql[0]=0;
    for(size_t i=0;i<sizeof(fragments)/sizeof(fragments[0]);++i)strcat(sql,fragments[i]);
    json_object *rows=web_query(db,sql,p,1);free(sql);
    if(!rows)return 0;json_object_put(rows);return 1;
}
#define ALERT_COLUMNS "id,tipo,descricao,severidade,status,leitura_consumo_id,leitura_qualidade_id,CONVERT(varchar(19),criado_em_utc,126)+'Z' criado_em_utc,CONVERT(varchar(19),atualizado_em_utc,126)+'Z' atualizado_em_utc"
static json_object *alert_rows(Database *db,const WebUser *u,const Filters *f,int paged,const char *status,const char *type) {
    WebParam p[]={web_integer(u->id),web_text(f->inicio),web_text(f->fim),status?web_text(status):web_null(),type?web_text(type):web_null(),web_integer((int64_t)(f->page-1)*f->size),web_integer(f->size)};
    json_object *items=web_query(db,paged?
      "SET NOCOUNT ON;DECLARE @u bigint=?,@inicio datetime2(0)=CONVERT(datetime2(0),?,126),@fim datetime2(0)=CONVERT(datetime2(0),?,126),@status varchar(10)=?,@tipo varchar(20)=?;SELECT " ALERT_COLUMNS " FROM dbo.WebAlertas WHERE usuario_id=@u AND criado_em_utc>=@inicio AND criado_em_utc<@fim AND(@status IS NULL OR status=@status) AND(@tipo IS NULL OR tipo=@tipo) ORDER BY criado_em_utc DESC,id DESC OFFSET ? ROWS FETCH NEXT ? ROWS ONLY;":
      "SET NOCOUNT ON;DECLARE @u bigint=?,@inicio datetime2(0)=CONVERT(datetime2(0),?,126),@fim datetime2(0)=CONVERT(datetime2(0),?,126),@status varchar(10)=?,@tipo varchar(20)=?;SELECT " ALERT_COLUMNS " FROM dbo.WebAlertas WHERE usuario_id=@u AND criado_em_utc>=@inicio AND criado_em_utc<@fim AND(@status IS NULL OR status=@status) AND(@tipo IS NULL OR tipo=@tipo) ORDER BY criado_em_utc DESC,id DESC;",p,paged?7:5);
    if(!items)return NULL;
    for(size_t i=0;i<json_object_array_length(items);++i){json_object *row=json_object_array_get_idx(items,i);WebParam hp[]={web_integer(u->id),web_integer(json_object_get_int64(field(row,"id")))};
        json_object *history=web_query(db,"SELECT id,status_anterior,status_novo,CONVERT(varchar(19),alterado_em_utc,126)+'Z' alterado_em_utc,observacao FROM dbo.WebAlertasHistorico WHERE usuario_id=? AND alerta_id=? ORDER BY id;",hp,2);
        if(!history){json_object_put(items);return NULL;}json_object_object_add(row,"historico",history);}
    return items;
}
static json_object *alert_counts(Database *db,const WebUser *u,const Filters *f) {
    WebParam p[]={web_integer(u->id),web_text(f->inicio),web_text(f->fim)};
    return take_first(web_query(db,"SELECT COUNT_BIG(CASE WHEN tipo='CONSUMO_ELEVADO' THEN 1 END) CONSUMO_ELEVADO,COUNT_BIG(CASE WHEN tipo='POSSIVEL_VAZAMENTO' THEN 1 END) POSSIVEL_VAZAMENTO,COUNT_BIG(CASE WHEN tipo='QUALIDADE' THEN 1 END) QUALIDADE,COUNT_BIG(CASE WHEN tipo='OUTROS' THEN 1 END) OUTROS,COUNT_BIG(CASE WHEN status='ABERTO' THEN 1 END) ABERTO,COUNT_BIG(CASE WHEN status='EM_ANALISE' THEN 1 END) EM_ANALISE,COUNT_BIG(CASE WHEN status='RESOLVIDO' THEN 1 END) RESOLVIDO FROM dbo.WebAlertas WHERE usuario_id=? AND criado_em_utc>=CONVERT(datetime2(0),?,126) AND criado_em_utc<CONVERT(datetime2(0),?,126);",p,3));
}
static int alerts_get(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    Filters f;if(!filters(req,&f,r))return 1;
    char status[20],type[30];int s=web_query_value(req->query,"status",status,sizeof(status)),t=web_query_value(req->query,"tipo",type,sizeof(type));
    if(s<0||t<0||(s&&strcmp(status,"ABERTO")&&strcmp(status,"EM_ANALISE")&&strcmp(status,"RESOLVIDO"))||(t&&strcmp(type,"CONSUMO_ELEVADO")&&strcmp(type,"POSSIVEL_VAZAMENTO")&&strcmp(type,"QUALIDADE")&&strcmp(type,"OUTROS")))return invalid(r,"Tipo ou status de alerta inválido.");
    if(!refresh_alerts(db,u))return failure(r);
    json_object *items=alert_rows(db,u,&f,1,s?status:NULL,t?type:NULL),*counts=alert_counts(db,u,&f);
    WebParam p[]={web_integer(u->id),web_text(f.inicio),web_text(f.fim),s?web_text(status):web_null(),t?web_text(type):web_null()};
    json_object *total=web_query(db,"DECLARE @u bigint=?,@inicio datetime2(0)=CONVERT(datetime2(0),?,126),@fim datetime2(0)=CONVERT(datetime2(0),?,126),@s varchar(10)=?,@t varchar(20)=?;SELECT COUNT_BIG(*) total FROM dbo.WebAlertas WHERE usuario_id=@u AND criado_em_utc>=@inicio AND criado_em_utc<@fim AND(@s IS NULL OR status=@s) AND(@t IS NULL OR tipo=@t);",p,5);
    if(!items||!counts||!total){if(items)json_object_put(items);if(counts)json_object_put(counts);if(total)json_object_put(total);return failure(r);}json_object *data=pagination(items,take_first(total),&f);json_object_object_add(data,"contagens",counts);web_ok(r,200,data);return 1;
}
static int alerts_patch(Database *db,const WebUser *u,const WebRequest *req,WebResult *r,int64_t id) {
    const char *status=web_body_text(req->body,"status"),*observation=web_body_text(req->body,"observacao");
    if(!status||(strcmp(status,"ABERTO")&&strcmp(status,"EM_ANALISE")&&strcmp(status,"RESOLVIDO"))||(!optional_text_type(req->body,"observacao") || !bounded_text(observation,500,0,1)))return invalid(r,"Informe status ABERTO, EM_ANALISE ou RESOLVIDO e observação com até 500 caracteres.");
    WebParam p[]={web_integer(u->id),web_integer(id),web_text(status),observation?web_text(observation):web_null()};
    return mutation_result(r,web_query(db,"SET NOCOUNT ON;SET XACT_ABORT ON;DECLARE @u bigint=?,@id bigint=?,@novo varchar(10)=?,@obs nvarchar(500)=?;BEGIN TRY BEGIN TRANSACTION;DECLARE @antigo varchar(10);SELECT @antigo=status FROM dbo.WebAlertas WITH(UPDLOCK,HOLDLOCK) WHERE id=@id AND usuario_id=@u;"
      "IF @antigo IS NULL BEGIN ROLLBACK;SELECT 'NOT_FOUND' domain_error;RETURN;END;"
      "IF @antigo<>@novo BEGIN UPDATE dbo.WebAlertas SET status=@novo,atualizado_em_utc=SYSUTCDATETIME() WHERE id=@id AND usuario_id=@u;INSERT dbo.WebAlertasHistorico(alerta_id,usuario_id,status_anterior,status_novo,observacao) VALUES(@id,@u,@antigo,@novo,@obs);END;COMMIT;SELECT " ALERT_COLUMNS " FROM dbo.WebAlertas WHERE id=@id AND usuario_id=@u;END TRY BEGIN CATCH IF XACT_STATE()<>0 ROLLBACK;THROW;END CATCH;",p,4),200);
}
static int dashboard(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    Filters f;if(!filters(req,&f,r)||!meter_filter(db,u,&f,r))return 1;if(!refresh_alerts(db,u))return failure(r);
    json_object *data=summary_data(db,u,&f),*quality=quality_latest(db,u),*counts=alert_counts(db,u,&f);Filters latest=f;latest.page=1;latest.size=5;
    json_object *items=alert_rows(db,u,&latest,1,NULL,NULL);
    if(!data||!quality||!counts||!items){if(data)json_object_put(data);if(quality)json_object_put(quality);if(counts)json_object_put(counts);if(items)json_object_put(items);return failure(r);}
    json_object *alerts=json_object_new_object();json_object_object_add(alerts,"items",items);json_object_object_add(alerts,"contagens",counts);json_object_object_add(data,"qualidade",quality);json_object_object_add(data,"alertas",alerts);web_ok(r,200,data);return 1;
}
static int reports(Database *db,const WebUser *u,const WebRequest *req,WebResult *r,const char *kind) {
    Filters f;if(!filters(req,&f,r)||!meter_filter(db,u,&f,r))return 1;
    json_object *data=summary_data(db,u,&f);if(!data)return failure(r);
    json_object_object_add(data,"tipo",json_object_new_string(kind));
    if(!strcmp(kind,"consumo")||!strcmp(kind,"financeiro")||!strcmp(kind,"detalhado")){json_object *rows=consumption_rows(db,u,&f,0);if(!rows){json_object_put(data);return failure(r);}json_object_object_add(data,"leituras",rows);}
    if(!strcmp(kind,"alertas")||!strcmp(kind,"detalhado")){
        if(!refresh_alerts(db,u)){json_object_put(data);return failure(r);}json_object *items=alert_rows(db,u,&f,0,NULL,NULL),*counts=alert_counts(db,u,&f);
        if(!items||!counts){if(items)json_object_put(items);if(counts)json_object_put(counts);json_object_put(data);return failure(r);}json_object_object_add(data,"alertas",items);json_object_object_add(data,"contagens",counts);}
    if(!strcmp(kind,"detalhado")){json_object *quality=quality_rows(db,u,&f,0);WebParam p[]={web_integer(u->id)};json_object *meters=web_query(db,MEDIDOR_SELECT,p,1);
        if(!quality||!meters){if(quality)json_object_put(quality);if(meters)json_object_put(meters);json_object_put(data);return failure(r);}json_object_object_add(data,"qualidade",quality);json_object_object_add(data,"medidores",meters);}
    web_ok(r,200,data);return 1;
}
static int path_id(const char *path,const char *prefix,int64_t *id) {size_t n=strlen(prefix);return !strncmp(path,prefix,n)&&parse_positive(path+n,id);}
int web_domain_route(Database *db,const WebUser *u,const WebRequest *req,WebResult *r) {
    if(!db||!u||!req||!r||!req->method||!req->path)return 0;
    const char *method=req->method,*path=req->path;int64_t id=0;
    if(!strcmp(path,"/api/v1/medidores")&&(!strcmp(method,"GET")||!strcmp(method,"POST")))return medidores(db,u,req,r);
    if(path_id(path,"/api/v1/medidores/",&id)&&!strcmp(method,"PATCH"))return meter_patch(db,u,req,r,id);
    if(!strcmp(path,"/api/v1/consumo/leituras")&&!strcmp(method,"POST"))return reading_insert(db,u,req,r);
    if(!strcmp(path,"/api/v1/consumo/historico")&&!strcmp(method,"GET"))return consumption_history(db,u,req,r);
    if(!strcmp(path,"/api/v1/consumo/resumo")&&!strcmp(method,"GET"))return consumption_summary(db,u,req,r);
    if(!strcmp(path,"/api/v1/qualidade/leituras")){if(!strcmp(method,"GET"))return quality_history(db,u,req,r);if(!strcmp(method,"POST"))return quality_insert(db,u,req,r);}
    if(!strcmp(path,"/api/v1/qualidade/importar-legado")&&!strcmp(method,"POST"))return import_legacy(db,u,r);
    if(!strcmp(path,"/api/v1/configuracoes")){if(!strcmp(method,"GET"))return settings_get(db,u,r);if(!strcmp(method,"PATCH"))return settings_patch(db,u,req,r);}
    if(!strcmp(path,"/api/v1/tarifas")&&(!strcmp(method,"GET")||!strcmp(method,"POST")))return tariffs(db,u,req,r);
    if(!strcmp(path,"/api/v1/categorias")&&!strcmp(method,"GET"))return categories_get(db,u,req,r);
    if(!strcmp(path,"/api/v1/categorias/registros")&&!strcmp(method,"POST"))return category_insert(db,u,req,r);
    if(!strcmp(path,"/api/v1/alertas")&&!strcmp(method,"GET"))return alerts_get(db,u,req,r);
    if(path_id(path,"/api/v1/alertas/",&id)&&!strcmp(method,"PATCH"))return alerts_patch(db,u,req,r,id);
    if(!strcmp(path,"/api/v1/dashboard")&&!strcmp(method,"GET"))return dashboard(db,u,req,r);
    if(!strncmp(path,"/api/v1/relatorios/",19)&&!strcmp(method,"GET")){const char *kind=path+19;
        if(!strcmp(kind,"consumo")||!strcmp(kind,"financeiro")||!strcmp(kind,"alertas")||!strcmp(kind,"detalhado"))return reports(db,u,req,r,kind);}
    return 0;
}
