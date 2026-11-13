#!/usr/bin/env node
/* Paginação do histórico não pode produzir CSV com leituras duplicadas/omitidas. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source=fs.readFileSync(new URL('../../frontend/app.js',import.meta.url),'utf8')
  .replace(/\nboot\(\);\s*$/,'\n');
const context=vm.createContext({Intl});
vm.runInContext(source,context,{filename:'frontend/app.js'});
const {collectPages,state}=vm.runInContext('({collectPages,state})',context);
let checks=0;
const first=Array.from({length:100},(_,index)=>({id:101-index}));
const original=[...first,{id:1}];
let pageCalls;
function pages(second) {
  pageCalls=[];
  context.request=async(path,{query})=>{
    pageCalls.push(query.page);
    return query.page===1?{items:first,total:101,total_pages:2}:second;
  };
  vm.runInContext('api=request;',context);
}
pages({items:[{id:1}],total:101,total_pages:2});
assert.deepEqual(Array.from(await collectPages('/consumo/historico'),row=>row.id),original.map(row=>row.id));checks++;
assert.deepEqual(pageCalls,[1,2]);checks++;

// Uma leitura nova entre páginas desloca o OFFSET e repete a leitura #2.
for(const second of [
  {items:[{id:2},{id:1}],total:102,total_pages:2},
  {items:[{id:2}],total:101,total_pages:2},
  {items:[],total:101,total_pages:2},
]) {
  pages(second);
  await assert.rejects(collectPages('/consumo/historico'),/mudaram durante a exportação/);checks++;
}

context.request=async()=>({items:[],total:0,total_pages:0});
vm.runInContext('api=request;',context);
assert.equal((await collectPages('/qualidade/leituras')).length,0);checks++;

// Mudar de conta entre páginas também exige reiniciar a exportação.
state.csrf='A';pages({items:[{id:1}],total:101,total_pages:2});
const request=context.request;
context.request=async(...args)=>{
  const result=await request(...args);
  if(pageCalls.length===2)state.csrf='B';
  return result;
};
vm.runInContext('api=request;',context);
await assert.rejects(collectPages('/consumo/historico'),/sessão mudou/);checks++;

console.log(`${checks} verificações de exportação paginada aprovadas (sem servidor/banco).`);
