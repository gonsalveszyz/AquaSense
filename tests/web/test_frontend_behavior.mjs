#!/usr/bin/env node
/* Regressões da UI com transporte/DOM mínimos; sem servidor ou banco. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const source = fs.readFileSync(path.join(root, 'frontend/app.js'), 'utf8').replace(/\nboot\(\);\s*$/, '\n');
let checks = 0;
function check(condition, description) { assert(condition, description); checks++; }
function environment() {
  const context = vm.createContext({ Intl, URL, window:{location:{origin:'http://localhost:8080'}}, document:{querySelector:()=>null} });
  vm.runInContext(source, context, {filename:'frontend/app.js'});
  return context;
}

// A escrita pode ter sido confirmada no SQL antes de a resposta se perder.
const transport = environment();
const api = vm.runInContext('api', transport);
for (const method of ['POST', 'PATCH']) {
  for (const failure of ['network', 'json', 'null', 'array', 'missing-data']) {
    transport.fetch = async () => {
      if (failure === 'network') throw new Error('connection lost');
      return {ok:true, status:200, json:async()=>{
        if (failure === 'json') throw new Error('truncated JSON');
        return failure === 'null' ? null : failure === 'array' ? [] : {};
      }};
    };
    await assert.rejects(api('/qualidade/leituras',{method,body:{}}), error=>{
      check(/não foi confirmado/.test(error.message), `${method}/${failure}: resultado incerto explícito`);
      check(/histórico/.test(error.message), `${method}/${failure}: conferir antes de reenviar`);
      return true;
    });
  }
}
transport.fetch = async()=>{throw new Error('connection lost');};
await assert.rejects(api('/dashboard'), error=>{
  check(/servidor local/.test(error.message), 'Falha GET explica a conexão local');
  check(!/não foi confirmado/.test(error.message), 'GET não afirma incerteza de escrita');
  return true;
});
transport.fetch = async()=>({ok:true,status:200,json:async()=>({data:{id:7},error:null})});
check((await api('/qualidade/leituras',{method:'POST',body:{}})).id === 7, 'Sucesso conserva o DTO');
transport.fetch = async()=>({ok:false,status:422,json:async()=>({data:null,error:{code:'DADOS_INVALIDOS',message:'pH inválido.'}})});
await assert.rejects(api('/qualidade/leituras',{method:'POST',body:{}}), error=>{
  check(error.status===422 && error.code==='DADOS_INVALIDOS' && error.message==='pH inválido.', 'Rejeição confirmada preserva mensagem/status/código');
  return true;
});

// Meses curtos e anos bissextos não devem avançar o início para o mês seguinte.
const ranges = environment();
const rangeDefault = vm.runInContext('rangeDefault', ranges);
for (const [today,preset,year,month,day] of [
  [[2026,4,30], '6m', 2025,10,30],
  [[2026,7,30], '6m', 2026,1,28],
  [[2024,1,28], '12m', 2023,1,28],
  [[2025,1,27], '12m', 2024,1,28],
]) {
  const fixed = new Date(...today,12).valueOf();
  ranges.Date = class extends Date { constructor(...args) { super(...(args.length ? args : [fixed])); } };
  const range = rangeDefault(preset),start = new Date(range.inicio),end = new Date(range.fim);
  check(start.getFullYear()===year && start.getMonth()===month && start.getDate()===day, `${preset} respeita o último dia do mês destino`);
  check(end.getHours()===0 && start.getHours()===0, 'Limites permanecem meia-noite local');
}
const fixed = new Date(2026,10,12,12).valueOf();
ranges.Date = class extends Date { constructor(...args) { super(...(args.length ? args : [fixed])); } };
for (const [preset,days] of [['7d',7],['30d',30]]) {
  const range = rangeDefault(preset),expected = new Date(range.fim); expected.setDate(expected.getDate()-days);
  check(range.inicio===expected.toISOString().slice(0,19)+'Z', `${preset} mantém aritmética por dias`);
}

// DOM mínimo para resolver a corrida entre edição dos filtros e resposta HTTP.
class Element {
  constructor(tag,attrs={},...children) {
    this.tag=tag;this.attrs=attrs;this.children=[];this.dataset={};this.listeners={};
    const classes=new Set();
    this.classList={add:name=>classes.add(name),remove:name=>classes.delete(name),contains:name=>classes.has(name),toggle:(name,force)=>{
      const enabled=force ?? !classes.has(name);if(enabled)classes.add(name);else classes.delete(name);return enabled;
    }};
    this.append(...children);
  }
  append(...children) {this.children.push(...children.flat(Infinity).filter(item=>item!==undefined && item!==null && item!==false));}
  prepend(...children) {this.children.unshift(...children);}
  replaceChildren(...children) {this.children=[];this.append(...children);}
  setAttribute(key,value) {this.attrs[key]=value;}
  removeAttribute(key) {delete this.attrs[key];}
  addEventListener(name,callback) {this.listeners[name]=callback;}
  focus() {this.focused=true;}
}
function textTree(node) {return node instanceof Element ? node.children.map(textTree).join('\n') : String(node);}
const report = environment();
let submit,finish,requested;
const fields={};
report.makeElement=(...args)=>new Element(...args);
report.makeField=(label,name,options={})=>{
  const input={value:options.value || ''};fields[name]=input;
  return {row:new Element('field',{},label),input};
};
report.captureSubmit=(form,callback)=>{submit=callback;return form;};
report.request=(url,options)=>{requested={url,range:options.query};return new Promise(resolve=>{finish=resolve;});};
vm.runInContext(`
  el=makeElement;field=makeField;wireForm=captureSubmit;api=request;
  icon=()=>null;announce=()=>{};date=value=>String(value);
  panel=(title,subtitle,body)=>el('section',{},title,subtitle,body);
  button=(label,action,style,symbol,attrs)=>el('button',attrs,label);
  pageHead=(title,description,actions)=>el('header',{},title,description,actions);
  empty=(...args)=>el('empty',{},args);loading=()=>el('loading');
  consumeMetrics=()=>el('metrics');chart=()=>el('chart');
  table=()=>el('table');note=message=>el('note',{},message);
  state.user={nome:'Teste isolado'};
`, report);
const reportRoot=await vm.runInContext('reportsPage()',report);
fields.inicio.value='2026-10-01';fields.fim.value='2026-10-31';
const pending=submit();
check(requested.range.fim===vm.runInContext("dayUTC('2026-10-31',true)",report), 'Consulta captura o período submetido');
fields.fim.value='2026-12-31';
finish({resumo:{},serie:[],leituras:[]});
await pending;
const expectedEnd=new Date(new Date(requested.range.fim).getTime()-1).toISOString();
const reportText=textTree(reportRoot);
check(reportText.includes(` a ${expectedEnd} · emitido em`), 'Cabeçalho usa fim capturado mesmo após edição');
check(reportText.includes(` até ${expectedEnd}`), 'Resumo usa o mesmo fim capturado');
check(!reportText.includes('2026-12-31'), 'Filtros não submetidos não rotulam o relatório');

// Fechar o menu torna os links inert: a navegação deve ocorrer no próprio handler.
const navigation=environment(),nodes=new Map();
navigation.document={
  querySelector:selector=>{if(!nodes.has(selector))nodes.set(selector,new Element('node'));return nodes.get(selector);},
  addEventListener(){},body:new Element('body')
};
navigation.window.addEventListener=()=>{};
navigation.window.matchMedia=()=>({matches:true,addEventListener(){}});
let hash='',rendered;
navigation.location={get hash(){return hash;},set hash(value){hash=value.startsWith('#')?value:`#${value}`;}};
navigation.makeElement=(...args)=>new Element(...args);
navigation.captureRender=id=>{rendered=id;};
vm.runInContext(`
  el=makeElement;icon=()=>null;loading=()=>el('loading');
  api=async()=>({});authSession=()=>{};openApplication=async()=>{};
  renderPage=captureRender;
`,navigation);
await vm.runInContext('boot()',navigation);
const sidebar=nodes.get('#sidebar'),links=nodes.get('#navigation').children;
for(const link of links) {
  sidebar.classList.add('open');sidebar.inert=false;
  let prevented=false;
  link.attrs.onclick({button:0,preventDefault(){prevented=true;}});
  check(prevented, `${link.dataset.route}: handler assume a navegação antes de inert`);
  check(hash===link.attrs.href, `${link.dataset.route}: clique móvel altera a rota`);
  check(sidebar.inert && !sidebar.classList.contains('open'), `${link.dataset.route}: menu fecha após selecionar a rota`);
}
const currentLink=links.at(-1);
rendered=null;
currentLink.attrs.onclick({button:0,preventDefault(){}});
check(rendered===currentLink.dataset.route, 'Clicar na rota atual recarrega a tela');
sidebar.classList.add('open');sidebar.inert=false;
currentLink.attrs.onclick({button:0,metaKey:true,preventDefault(){assert.fail('Cmd+clique mantém ação do navegador');}});
check(!sidebar.inert && sidebar.classList.contains('open'), 'Cmd+clique preserva a ação nativa com link disponível');

console.log(`${checks} verificações de transporte, períodos, relatório e navegação aprovadas (sem servidor/banco).`);
