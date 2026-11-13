#!/usr/bin/env node
/* Respostas de uma sessão encerrada não podem contaminar a conta seguinte. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../../frontend/app.js', import.meta.url), 'utf8')
  .replace(/\nboot\(\);\s*$/, '\n');
let checks = 0;
function check(condition, message) { assert(condition, message); checks++; }
function environment(stubAuth = true) {
  const context = vm.createContext({Intl, URL, window:{location:{origin:'http://localhost:8080'}},
    document:{querySelector:()=>null}});
  vm.runInContext(source, context, {filename:'frontend/app.js'});
  vm.runInContext(stubAuth ? 'showAuth=()=>{};toast=()=>{};' : 'toast=()=>{};', context);
  return {context,...vm.runInContext('({api,state,authSession})', context)};
}
const account = id => ({usuario:{id,nome:`Conta ${id}`},csrf_token:`session-${id}`});

// A troca pode ocorrer enquanto os bytes ou o JSON da resposta ainda chegam.
for (const stage of ['fetch','json']) {
  for (const status of [200,401]) {
    const {context,api,state,authSession} = environment();
    authSession(account(1));
    let finish,entered;
    const readingJSON=new Promise(resolve=>{entered=resolve;});
    const response={ok:status===200,status,json:async()=>({data:{usuario:account(1).usuario},
      error:status===401?{code:'AUTENTICACAO_NECESSARIA',message:'Sessão antiga'}:null})};
    if(stage==='fetch') context.fetch=()=>new Promise(resolve=>{finish=()=>resolve(response);});
    else context.fetch=async()=>({...response,json:()=>{entered();return new Promise(resolve=>{finish=()=>resolve({data:{usuario:account(1).usuario},error:status===401?{message:'Sessão antiga'}:null});});}});
    const pending=api('/configuracoes');
    if(stage==='json') await readingJSON;
    authSession(account(2));
    finish();
    await assert.rejects(pending,error=>{
      check(error.code==='SESSAO_ALTERADA',`${stage}/${status}: descarta resposta da sessão anterior`);
      return true;
    });
    check(state.user.id===2 && state.csrf==='session-2',`${stage}/${status}: preserva a sessão atual`);
  }
}

// Uma escrita em trânsito conserva o aviso de resultado incerto após a troca.
for(const method of ['POST','PATCH']) {
  const {context,api,authSession}=environment();
  authSession(account(1));let finish;
  context.fetch=()=>new Promise(resolve=>{finish=resolve;});
  const pending=api('/configuracoes',{method,body:{}});
  authSession(account(2));
  finish({ok:true,status:200,json:async()=>({data:{id:1},error:null})});
  await assert.rejects(pending,error=>{
    check(error.code==='SESSAO_ALTERADA' && /não foi confirmado/.test(error.message) && /conta utilizada/.test(error.message),`${method}: não incentiva reenviar escrita da conta anterior`);
    return true;
  });
}

// Um novo login não deve herdar preferências/hidrômetros caso a próxima consulta falhe.
const {context,api,state,authSession}=environment();
authSession(account(1));
state.preferences={unidade:'m3',tema:'escuro'};state.meterCache=[{id:91,nome:'Privado A'}];state.alertCount=8;
authSession(account(2));
check(Object.keys(state.preferences).length===0,'Nova sessão limpa preferências anteriores');
check(state.meterCache.length===0,'Nova sessão limpa hidrômetros anteriores');
check(state.alertCount===null,'Nova sessão limpa contagem anterior');

// Expiração real ainda deve voltar ao login e invalidar renderizações pendentes.
const oldLoad=state.loadId;
state.preferences={unidade:'m3'};state.meterCache=[{id:92}];state.alertCount=4;
context.fetch=async()=>({ok:false,status:401,json:async()=>({data:null,error:{code:'AUTENTICACAO_NECESSARIA',message:'Entre novamente.'}})});
await assert.rejects(api('/configuracoes'),error=>error.status===401);
check(state.user===null && state.csrf===null,'401 da sessão atual encerra autenticação');
check(state.meterCache.length===0 && Object.keys(state.preferences).length===0 && state.alertCount===null,'Expiração limpa dados da conta');
check(state.loadId>oldLoad,'Expiração invalida renderizações iniciadas antes do 401');

// Uma autenticação também pode entrar na fila DEPOIS da última guarda de api()
// e ANTES de seu consumidor retomar. Nesse caso cada commit precisa de guarda.
for (const consumer of ['getMeters', 'settingsPage', 'openApplication']) {
  const isolated=environment();
  isolated.authSession(account(1));
  isolated.context.document.querySelector=()=>({replaceChildren(){},hidden:false,classList:{add(){},remove(){}}});
  vm.runInContext('updateProfile=()=>{};applyTheme=()=>{};routeFromHash=()=>{};',isolated.context);
  isolated.context.fetch=async()=>({ok:true,status:200,json:()=>{
    const later=turns=>turns?queueMicrotask(()=>later(turns-1)):isolated.authSession(account(2));
    later(4); // inclui a adoção da Promise entre o transporte e o contexto VM
    return Promise.resolve({data:consumer==='getMeters'?[{id:91,nome:'Privado A'}]:{
      usuario:account(1).usuario,medidores:[{id:91}],preferencias:{unidade:'m3'}}});
  }});
  const pending=vm.runInContext(`${consumer}()`,isolated.context);
  if(consumer==='openApplication') await pending;
  else await assert.rejects(pending,error=>error.code==='SESSAO_ALTERADA');
  check(isolated.state.user.id===2,`${consumer}: microtarefa não restaura perfil da conta anterior`);
  check(isolated.state.meterCache.length===0 && Object.keys(isolated.state.preferences).length===0,
    `${consumer}: microtarefa não contamina caches após validação de api`);
}

// Uma segunda conta não pode reutilizar a tela ou enviar um formulário da
// primeira enquanto suas configurações ainda estão carregando. Usa wireForm
// e api reais, incluindo a captura do token realmente enviado pelo transporte.
{
  class Element {
    constructor(tag,attrs={},...children) {
      this.tag=tag;this.attrs=attrs;this.children=[];this.listeners={};this.hidden=true;
      const classes=new Set();
      this.classList={add:name=>classes.add(name),remove:name=>classes.delete(name),contains:name=>classes.has(name),toggle:(name,force)=>{
        const active=force??!classes.has(name);if(active)classes.add(name);else classes.delete(name);return active;
      }};
      this.append(...children);
    }
    append(...children){this.children.push(...children.flat(Infinity));}
    replaceChildren(...children){this.children=[];this.append(...children);}
    setAttribute(name,value){this.attrs[name]=value;} removeAttribute(name){delete this.attrs[name];} focus(){}
    addEventListener(name,callback){this.listeners[name]=callback;}
    querySelectorAll(){return [];}
    reportValidity(){return true;}
  }
  const isolated=environment(),nodes=new Map(),forms=[],requests=[],errors=[];
  const node=selector=>{if(!nodes.has(selector))nodes.set(selector,new Element('node'));return nodes.get(selector);};
  isolated.context.document={querySelector:node,body:new Element('body')};
  isolated.context.makeElement=(tag,attrs,...children)=>{
    const element=new Element(tag,attrs,...children);if(tag==='form')forms.push(element);return element;
  };
  isolated.context.makeField=(label,name,options={})=>({row:new Element('field'),input:{value:options.value||''}});
  isolated.context.captureError=(form,message)=>errors.push(message);
  isolated.context.FormData=class {
    constructor(form){this.values=form.values||new Map();}
    get(key){return this.values.get(key);}
  };
  vm.runInContext('el=makeElement;field=makeField;icon=()=>null;updateProfile=()=>{};applyTheme=()=>{};pageHead=()=>el();panel=()=>el();table=()=>el();note=()=>el();button=()=>el();routeFromHash=()=>{};clearError=()=>{};formError=captureError;',isolated.context);
  let slow=false;
  isolated.context.fetch=async(url,options)=>{
    requests.push({path:new URL(url).pathname,...options});
    if(slow&&options.method==='GET')return new Promise(()=>{});
    return {ok:true,status:200,json:async()=>({data:options.method==='GET'?{
      usuario:{...account(1).usuario,email:'conta1@example.invalid'},preferencias:{},medidores:[],tarifas:[]}:{} })};
  };
  isolated.authSession(account(1));
  const oldView=await vm.runInContext('settingsPage()',isolated.context),oldForm=forms[0];
  node('#conteudo').replaceChildren(oldView);node('#dialog-root').replaceChildren('Dados privados da conta 1');
  node('#notification-count').hidden=false;node('#notification-count').textContent='8';
  node('#notifications-link').classList.add('notification-active');isolated.context.document.body.classList.add('dark');
  oldForm.values=new Map([['nome','Conta 1'],['email','conta1@example.invalid']]);
  isolated.authSession(account(2));slow=true;
  vm.runInContext('openApplication()',isolated.context);
  await oldForm.listeners.submit({preventDefault(){}});
  check(!node('#conteudo').children.includes(oldView),'Nova conta remove a tela anterior mesmo com configurações pendentes');
  check(node('#dialog-root').children.length===0,'Nova conta remove os diálogos anteriores');
  check(!requests.some(request=>request.method==='PATCH'),'Formulário anterior não envia mutação com o token da nova conta');
  check(errors.some(message=>/sessão mudou/.test(message)),'Formulário anterior informa troca de sessão antes do envio');
  check(node('#notification-count').hidden&&node('#notification-count').textContent==='','Nova conta remove o contador visual da sessão anterior');
  check(!node('#notifications-link').classList.contains('notification-active')&&node('#notifications-link').attrs['aria-label']==='Abrir alertas','Nova conta remove destaque e rótulo de alertas anteriores');
  check(!isolated.context.document.body.classList.contains('dark'),'Nova conta remove o tema da sessão anterior antes de carregar preferências');
}

// Set-Cookie já é aplicado pelo navegador antes de api descartar uma resposta
// antiga. Duas autenticações concorrentes precisam ser impedidas na origem.
{
  class Element {
    constructor(tag,attrs={},...children){
      this.tag=tag;this.attrs=attrs;this.children=[];this.listeners={};
      this.classList={add(){},remove(){},toggle(){},contains(){return false;}};
      this.append(...children);
    }
    append(...children){this.children.push(...children.flat(Infinity));}
    replaceChildren(...children){this.children=[];this.append(...children);}
    setAttribute(){} removeAttribute(){} focus(){}
    addEventListener(name,callback){this.listeners[name]=callback;}
    querySelectorAll(){return [];}
    reportValidity(){return true;}
  }
  const isolated=environment(false),nodes=new Map(),elements=[],forms=[],requests=[],finishes=[];
  const node=selector=>{if(!nodes.has(selector))nodes.set(selector,new Element('node'));return nodes.get(selector);};
  isolated.context.document={querySelector:node,body:new Element('body')};
  isolated.context.makeElement=(tag,attrs,...children)=>{
    const element=new Element(tag,attrs,...children);elements.push(element);
    if(tag==='form')forms.push(element);return element;
  };
  isolated.context.makeField=()=>({row:new Element('field'),input:{focus(){}}});
  isolated.context.FormData=class {
    constructor(form){this.values=form.values;}
    get(key){return this.values.get(key);}
    has(key){return this.values.has(key);}
  };
  vm.runInContext('el=makeElement;field=makeField;icon=()=>null;brand=()=>el();closeMenu=()=>{};clearError=()=>{};formError=()=>{};openApplication=async()=>{};',isolated.context);
  isolated.context.fetch=(url,options)=>{
    requests.push({path:new URL(url).pathname,...options});
    return new Promise(resolve=>finishes.push(resolve));
  };
  vm.runInContext("showAuth('login')",isolated.context);
  forms[0].values=new Map([['email','conta1@example.invalid'],['senha','Senha somente de teste']]);
  const first=forms[0].listeners.submit({preventDefault(){}});
  const register=elements.find(element=>element.tag==='button'&&element.children.includes('Criar conta'));
  register.attrs.onclick();
  const secondForm=forms.at(-1);
  secondForm.values=new Map([['email','conta2@example.invalid'],['senha','Outra senha de teste'],['nome','Conta 2']]);
  const second=secondForm.listeners.submit({preventDefault(){}});
  check(requests.length===1,'Trocar aba e reenviar durante login não inicia outra autenticação');
  check(forms.length===1,'Abas de autenticação não remontam formulário enquanto uma tentativa está pendente');
  finishes[0]({ok:true,status:200,json:async()=>({data:account(1)})});
  await Promise.all([first,second]);
  check(isolated.state.user.id===1,'Somente a autenticação original estabelece a conta');
  check(isolated.state.authPending===false,'Bloqueio de autenticação é liberado após concluir a tentativa');

  vm.runInContext("clearSession();showAuth('login')",isolated.context);
  const failedForm=forms.at(-1);
  failedForm.values=new Map([['email','conta2@example.invalid'],['senha','Senha somente de teste']]);
  isolated.context.fetch=async()=>{throw new Error('Falha de rede simulada');};
  await failedForm.listeners.submit({preventDefault(){}});
  check(isolated.state.authPending===false&&isolated.state.user===null,'Falha de autenticação libera o bloqueio sem estabelecer conta');
  const countBeforeRetry=forms.length;
  vm.runInContext("showAuth('login')",isolated.context);
  check(forms.length===countBeforeRetry+1,'Após falha é possível abrir uma nova tentativa de autenticação');
  const retryForm=forms.at(-1);
  retryForm.values=failedForm.values;
  isolated.context.fetch=async()=>({ok:true,status:200,json:async()=>({data:account(2)})});
  await retryForm.listeners.submit({preventDefault(){}});
  check(isolated.state.user.id===2&&isolated.state.authPending===false,'Nova tentativa após falha autentica e libera o bloqueio');
}

console.log(`${checks} verificações de isolamento de sessões aprovadas (sem servidor/banco).`);
