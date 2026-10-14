'use strict';

/* AquaSense · buildless frontend. Cookies e validações permanecem no servidor C. */
const $ = (selector, context = document) => context.querySelector(selector);
const state = { user: null, csrf: null, page: 'inicio', loadId: 0, setup: null, preferences: {}, meterCache: [], report: null, alertCount: null };
const paths = {
  home:'<path d="m3 10 9-7 9 7v10H3Z"/><path d="M9 20v-7h6v7"/>',
  drop:'<path d="M12 2C10 6 4 11 4 16a8 8 0 0 0 16 0c0-5-6-10-8-14Z"/><path d="M8 16a4 4 0 0 0 4 4"/>',
  chart:'<path d="M4 3v17h17"/><path d="m7 14 4-5 4 3 5-7"/>',
  history:'<path d="M3 11a9 9 0 1 1 2 7"/><path d="M3 5v6h6M12 7v5l3 2"/>',
  report:'<path d="M6 3h9l4 4v14H6Z"/><path d="M14 3v5h5M9 12h7M9 16h7"/>',
  bell:'<path d="M18 8a6 6 0 0 0-12 0c0 8-3 8-3 10h18c0-2-3-2-3-10M10 21h4"/>',
  settings:'<path d="m9 3-1 3-3 1-1 3 2 2-1 3 2 3 3-1 2 2 3-1 1-3 3-1 1-3-2-2 1-3-2-3-3 1-2-2Z"/><circle cx="12" cy="12" r="3"/>',
  logout:'<path d="M10 4H4v16h6M9 12h12m-4-4 4 4-4 4"/>',
  plus:'<path d="M12 5v14M5 12h14"/>',
  calendar:'<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M7 3v4M17 3v4M3 10h18M7 14h2M13 14h2"/>',
  wallet:'<path d="M20 7H5a2 2 0 0 1 0-4h13v4M4 5v15h17V7H5"/><path d="M21 12h-6v4h6M17 14h.1"/>',
  average:'<path d="M3 19h18M6 15V9M12 15V4M18 15v-7"/>',
  shield:'<path d="m12 2 8 3v7c0 5-8 10-8 10S4 17 4 12V5Z"/><path d="m8 12 3 3 5-6"/>',
  alert:'<path d="m12 3 10 18H2Z"/><path d="M12 9v5M12 17h.1"/>',
  info:'<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7h.1"/>',
  check:'<path d="m5 12 4 4L20 5"/>',
  close:'<path d="m6 6 12 12M18 6 6 18"/>',
  arrow:'<path d="M4 12h16m-6-6 6 6-6 6"/>',
  download:'<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
  print:'<path d="M7 8V3h10v5M7 17H3V8h18v9h-4M7 14h10v7H7M17 11h.1"/>',
  menu:'<path d="M4 6h16M4 12h16M4 18h16"/>',
  meter:'<circle cx="12" cy="12" r="9"/><path d="m12 12 4-5M5 12h2M12 4v2M17 12h2M8 17h8"/>',
  edit:'<path d="m16 3 5 5-12 12H4v-5ZM13 6l5 5"/>',
  refresh:'<path d="M20 10a8 8 0 0 0-14-5L3 8M3 3v5h5M4 14a8 8 0 0 0 14 5l3-3M21 21v-5h-5"/>',
  search:'<circle cx="10" cy="10" r="6"/><path d="m15 15 6 6"/>',
  category:'<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>'
};
function icon(name) { const node = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); node.setAttribute('viewBox', '0 0 24 24'); node.setAttribute('class', 'icon'); node.setAttribute('aria-hidden', 'true'); node.innerHTML = paths[name] || paths.info; return node; }
function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (value === undefined || value === null || (value === false && !key.startsWith('aria-'))) continue;
    if (key === 'class') node.className = value;
    else if (key.startsWith('on') && typeof value === 'function') node.addEventListener(key.slice(2), value);
    else if (key === 'text') node.textContent = value;
    else if (key in node && !key.startsWith('aria') && key !== 'role') node[key] = value;
    else node.setAttribute(key, value === true && !key.startsWith('aria-') ? '' : String(value));
  }
  for (const child of children.flat(Infinity)) {
    if (child === null || child === undefined || child === false) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}
function button(label, action, style = 'secondary', symbol, attrs = {}) { return el('button', {type:'button', class:`btn btn-${style}`, onclick:event=>{try{const pending=action?.(event);if(pending?.catch)pending.catch(error=>toast(safeMessage(error),true));}catch(error){toast(safeMessage(error),true);}}, ...attrs}, symbol && icon(symbol), label); }
function list(data) { return Array.isArray(data) ? data : data?.items || data?.leituras || data?.registros || []; }
function pick(object, ...keys) { for (const key of keys) if (object && object[key] !== undefined && object[key] !== null) return object[key]; return null; }
function finite(value) { if (value === null || value === undefined || value === '') return null; const n = Number(value); return Number.isFinite(n) ? n : null; }
/* DECIMAL chega como string: apresentação e conversão de unidade nunca usam
 * binary64. BigInt também conserva o transporte do arredondamento nos centavos.
 * Number permanece para FLOAT de qualidade, contagens e geometria dos gráficos. */
function decimalParts(value) {
  if(value===null || value===undefined || value==='')return null;
  if(typeof value==='number' && !Number.isFinite(value))return null;
  if(!['number','string','bigint'].includes(typeof value))return null;
  const text=String(value);
  if(text.length>1000)return null;
  const match=/^(-?)(\d+)(?:\.(\d+))?(?:[eE]([+-]?\d+))?$/.exec(text);
  if(!match)return null;
  const fraction=match[3] || '',exponent=match[4]?parseInt(match[4],10):0;
  if(!Number.isInteger(exponent) || Math.abs(exponent)>1000)return null;
  let coefficient=BigInt(match[2]+fraction),scale=fraction.length-exponent;
  if(scale<0){coefficient*=10n**BigInt(-scale);scale=0;}
  if(scale>1000)return null;
  if(match[1])coefficient=-coefficient;
  return {coefficient,scale};
}
function decimalString(parts) {
  if(!parts)return null;
  const sign=parts.coefficient<0n?'-':'',absolute=parts.coefficient<0n?-parts.coefficient:parts.coefficient;
  const digits=absolute.toString().padStart(parts.scale+1,'0');
  return sign+(parts.scale?digits.slice(0,-parts.scale)+'.'+digits.slice(-parts.scale):digits);
}
function decimalShift(value, places = 0) {
  const parts=decimalParts(value);
  if(!parts || !Number.isInteger(places) || places<0 || places>1000)return null;
  return decimalString({...parts,scale:parts.scale+places});
}
function decimalSum(values) {
  const parts=values.map(decimalParts);
  if(parts.some(value=>value===null))return null;
  const scale=Math.max(0,...parts.map(value=>value.scale));
  const coefficient=parts.reduce((sum,value)=>sum+value.coefficient*10n**BigInt(scale-value.scale),0n);
  return decimalString({coefficient,scale});
}
function decimalPercentage(value, total, digits = 1) {
  const numerator=decimalParts(value),denominator=decimalParts(total);
  if(!numerator || !denominator || denominator.coefficient===0n || !Number.isInteger(digits) || digits<0 || digits>100)return null;
  const negative=(numerator.coefficient<0n)!==(denominator.coefficient<0n);
  const top=(numerator.coefficient<0n?-numerator.coefficient:numerator.coefficient)*10n**BigInt(denominator.scale)*100n*10n**BigInt(digits);
  const bottom=(denominator.coefficient<0n?-denominator.coefficient:denominator.coefficient)*10n**BigInt(numerator.scale);
  const rounded=top/bottom+((top%bottom)*2n>=bottom?1n:0n);
  return decimalString({coefficient:negative?-rounded:rounded,scale:digits});
}
function formatDecimal(value, digits = 0, minimumDigits = 0) {
  const parts=decimalParts(value);
  if(!parts || !Number.isInteger(digits) || digits<0 || digits>100 || minimumDigits<0 || minimumDigits>digits)return null;
  const negative=parts.coefficient<0n,absolute=negative?-parts.coefficient:parts.coefficient;
  let rounded;
  if(parts.scale>digits){
    const divisor=10n**BigInt(parts.scale-digits);
    rounded=absolute/divisor+((absolute%divisor)*2n>=divisor?1n:0n);
  }else rounded=absolute*10n**BigInt(digits-parts.scale);
  const unit=10n**BigInt(digits),whole=rounded/unit;
  let fraction=digits?(rounded%unit).toString().padStart(digits,'0'):'';
  while(fraction.length>minimumDigits && fraction.endsWith('0'))fraction=fraction.slice(0,-1);
  const grouped=new Intl.NumberFormat('pt-BR',{maximumFractionDigits:0}).format(whole);
  return (negative?'-':'')+grouped+(fraction?','+fraction:'');
}
function number(value, digits = 0) { return formatDecimal(value,digits) ?? 'Sem dados'; }
function money(value, digits = 2) {
  const formatted=formatDecimal(value,digits,digits);
  if(formatted===null)return 'Sem tarifa';
  return formatted.startsWith('-')?'-R$\u00a0'+formatted.slice(1):'R$\u00a0'+formatted;
}
function date(value, time = true) { if (!value) return '—'; const parsed = new Date(value); return Number.isNaN(parsed.valueOf()) ? '—' : new Intl.DateTimeFormat('pt-BR', time ? {dateStyle:'short', timeStyle:'short'} : {dateStyle:'medium'}).format(parsed); }
function displayVolume(value) { const isM3=state.preferences.unidade==='m3',shifted=decimalShift(value,isM3?3:0); return shifted===null?'Sem dados':`${number(shifted,isM3?3:1)} ${volumeUnit()}`; }
function volumeUnit() { return state.preferences.unidade === 'm3' ? 'm³' : 'L'; }
function localInput(value = new Date()) { const d = value instanceof Date ? value : new Date(value); return new Date(d.getTime() - d.getTimezoneOffset() * 60000).toISOString().slice(0,16); }
function localDate(value = new Date()) { return localInput(value).slice(0,10); }
function utc(value) { const parsed = new Date(value); if (Number.isNaN(parsed.valueOf())) throw new Error('Informe uma data e hora válidas.'); return parsed.toISOString().slice(0,19)+'Z'; }
function dayUTC(value, end = false) { const parsed = new Date(`${value}T00:00:00`); if (Number.isNaN(parsed.valueOf())) throw new Error('Informe uma data válida.'); if (end) parsed.setDate(parsed.getDate() + 1); return parsed.toISOString().slice(0,19)+'Z'; }
function safeMessage(error) { return error?.message || 'Não foi possível concluir esta ação. Tente novamente.'; }
function announce(message) { const node = $('#announcer'); node.textContent = ''; requestAnimationFrame(() => { node.textContent = message; }); }
function toast(message, isError = false) { const node = el('div', {class:`toast ${isError ? 'error' : ''}`, role:isError ? 'alert' : 'status'}, icon(isError ? 'alert' : 'check'), el('span', {}, message)); $('#toast-root').append(node); setTimeout(() => node.remove(), 6000); announce(message); }
async function api(path, options = {}) {
  const {method = 'GET', body, query} = options;
  const url = new URL(`/api/v1${path}`, window.location.origin);
  for (const [key, value] of Object.entries(query || {})) if (value !== '' && value !== null && value !== undefined) url.searchParams.set(key, value);
  const headers = {Accept:'application/json'};
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  if (method !== 'GET' && state.csrf) headers['X-CSRF-Token'] = state.csrf;
  let response;
  try { response = await fetch(url, {method, headers, credentials:'same-origin', body:body === undefined ? undefined : JSON.stringify(body)}); }
  catch (_) { $('.status-dot')?.classList.add('offline'); throw new Error('Não foi possível acessar o servidor local. Verifique se a API está em execução e tente novamente.'); }
  let payload;
  try { payload = await response.json(); } catch (_) { throw new Error('O servidor retornou uma resposta inválida. Tente novamente.'); }
  $('.status-dot')?.classList.remove('offline');
  if (!response.ok || payload.error) {
    if (response.status === 401 && state.user) { state.user = null; state.csrf = null; showAuth('login'); toast('Sua sessão terminou. Entre novamente para continuar.', true); }
    const error = new Error(payload?.error?.message || 'Não foi possível concluir a solicitação.'); error.status = response.status; error.code = payload?.error?.code; throw error;
  }
  if (!Object.prototype.hasOwnProperty.call(payload, 'data')) throw new Error('A resposta da API está incompleta. Tente novamente.');
  return payload.data;
}
function loading(message = 'Carregando seus dados…') { return el('div', {class:'loading', role:'status'}, el('span',{class:'spinner','aria-hidden':true}), message); }
function empty(title = 'Sem dados neste período', text = 'Os registros aparecerão aqui quando houver medições.', symbol = 'chart', action) { return el('div', {class:'empty'}, el('span',{class:'empty-icon'},icon(symbol)),el('strong',{},title),el('p',{},text),action); }
function errorBlock(error, retry) { return el('div',{class:'panel error-state'},el('div',{class:'empty',role:'alert'},el('span',{class:'empty-icon'},icon('alert')),el('strong',{},'Não foi possível carregar'),el('p',{},safeMessage(error)),button('Tentar novamente',retry,'secondary','refresh'))); }
function note(text) { return el('div',{class:'notice'},icon('info'),el('span',{},text)); }
function panel(title, subtitle, body, action) { return el('section',{class:'panel'},el('div',{class:'panel-head'},el('div',{class:'panel-title'},el('h2',{},title),subtitle && el('p',{},subtitle)),action),el('div',{class:'panel-body'},body)); }
function pageHead(title, description, actions = []) { return el('div',{class:'page-head'},el('div',{},el('h1',{},title),el('p',{},description)),el('div',{class:'page-head-actions no-print'},actions)); }
function metric(label, value, unit, hint, symbol, color = '', quality = false, extra) { return el('div',{class:'metric-card'},el('div',{class:'metric-title-row'},el('span',{class:'metric-label'},label),el('span',{class:`metric-icon ${color}`},icon(symbol))),el('div',{class:`metric-value ${quality?'metric-quality':''}`},value,unit && el('small',{},unit)),el('div',{class:'metric-hint'},hint),extra); }
function consumeMetrics(data, quality) {
  const summary = data.resumo || data;
  const total = decimalParts(summary.total_litros)?summary.total_litros:null, avg = decimalParts(summary.media_diaria_litros)?summary.media_diaria_litros:null;
  const isM3 = state.preferences.unidade === 'm3';
  const delta = decimalParts(data.comparacao?.variacao_percentual)?data.comparacao.variacao_percentual:null;
  const missing=finite(summary.intervalos_sem_tarifa)||0,tariffCount=finite(summary.tarifas_distintas)||0;
  const estimated=decimalParts(summary.estimativa_reais)?summary.estimativa_reais:null,financeDelta=decimalParts(data.comparacao?.variacao_financeira_percentual)?data.comparacao.variacao_financeira_percentual:null;
  const financeComparison=financeDelta===null?null:el('div',{class:'mini-values'},`${financeDelta>0?'+':''}${number(financeDelta,1)}% em relação ao período anterior`);
  const financeValue=estimated===null?(total===null?'Sem dados':missing>0?'Indisponível':'Sem tarifa'):money(estimated);
  const financeHint=missing>0?`${number(missing)} ${missing===1?'intervalo sem tarifa':'intervalos sem tarifa'} vigente`:tariffCount>1?`Tarifas conforme vigência · ${number(tariffCount)} aplicadas`:decimalParts(summary.tarifa_valor_m3)!==null?`${money(summary.tarifa_valor_m3,4)} por m³ · vigência aplicável`:'Depende de consumo e tarifa configurados';
  const cards = [metric('Consumo atual',number(decimalShift(total,isM3?3:0),isM3?3:0), total !== null && volumeUnit(),delta !== null ? `${delta > 0 ? '+' : ''}${number(delta,1)}% em relação ao período anterior` : 'Volume entre leituras consecutivas','drop'),metric('Conta estimada',financeValue,null,financeHint,'wallet','green',false,financeComparison),metric('Média diária',number(decimalShift(avg,isM3?3:0),isM3?3:1),avg !== null && `${volumeUnit()}/dia`,decimalParts(summary.dias_observados) !== null ? `${number(summary.dias_observados,1)} dias observados` : 'Depende de dois registros de consumo','average','orange')];
  if (quality) {
    const q = data.qualidade || {}, last = q.ultima;
    const labels = {SEM_DADOS:'Sem dados',SEM_REFERENCIA:'Sem referência',DENTRO_LIMITES_CONFIGURADOS:'Dentro dos limites',FORA_LIMITES_CONFIGURADOS:'Fora dos limites'};
    const values = last && el('div',{class:'mini-values'},el('span',{},`pH ${number(pick(last,'ph','PH'),2)}`),el('span',{},`${number(pick(last,'temperatura_c','TemperaturaC'),1)} °C`),el('span',{},`${number(pick(last,'turbidez_ntu','TurbidezNTU'),2)} NTU`));
    cards.push(metric('Qualidade da água',labels[q.estado] || (last ? 'Medição disponível' : 'Sem dados'),null,last ? `Medição de ${last.registrado_em_utc?date(last.registrado_em_utc,false):'horário original disponível'}` : 'Registre temperatura, pH e turbidez','shield','purple',true,values));
  }
  return el('div',{class:`metric-grid ${cards.length===3?'metric-three':''}`},cards);
}
function field(label, name, options = {}) {
  const id = `field-${name}-${Math.random().toString(36).slice(2,7)}`;
  const {hint,wide,choices,value,...attrs} = options;
  let input;
  if (choices) { input = el('select',{id,name,...attrs},choices.map(choice => { const pair = Array.isArray(choice) ? choice : [choice,choice]; return el('option',{value:pair[0]},pair[1]); })); if (value !== undefined && value !== null) input.value = String(value); }
  else if (attrs.type === 'textarea') { delete attrs.type; input = el('textarea',{id,name,...attrs},value ?? ''); }
  else input = el('input',{id,name,value:value ?? '',...attrs});
  const row = el('div',{class:`field ${wide?'field-wide':''}`},el('label',{htmlFor:id},label),input,hint && el('small',{id:`${id}-hint`},hint));
  if (hint) input.setAttribute('aria-describedby',`${id}-hint`);
  return {row,input};
}
function formError(form, message) { let node = $('.form-error',form); if (!node) { node = el('div',{class:'form-error',role:'alert',tabIndex:-1}); form.append(node); } node.textContent = message; node.focus(); }
function busy(form, active) { form.setAttribute('aria-busy',String(active)); form.querySelectorAll('button[type=submit]').forEach(b => { b.disabled = active; }); }
function clearError(form) { $('.form-error',form)?.remove(); }
function wireForm(form, callback) { form.addEventListener('submit', async event => { event.preventDefault(); if (!form.reportValidity()) return; clearError(form); busy(form,true); try { await callback(new FormData(form)); } catch (error) { formError(form,safeMessage(error)); } finally { busy(form,false); } }); return form; }
function modal(title, description, content) {
  const origin = document.activeElement;
  const dialog = el('dialog',{class:'modal','aria-labelledby':'modal-title'});
  const close = () => dialog.close();
  dialog.append(el('div',{class:'modal-head'},el('h2',{id:'modal-title'},title),el('button',{class:'icon-button',type:'button','aria-label':'Fechar',onclick:close},icon('close'))),el('div',{class:'modal-body'},description && el('p',{class:'modal-description'},description),typeof content === 'function' ? content(close) : content));
  dialog.addEventListener('click',event => { if (event.target === dialog) { const r = dialog.getBoundingClientRect(); if (event.clientX < r.left || event.clientX > r.right || event.clientY < r.top || event.clientY > r.bottom) close(); } });
  dialog.addEventListener('keydown',event => { if (event.key !== 'Tab') return; const controls = [...dialog.querySelectorAll('button,input,select,textarea,a[href],[tabindex="0"]')].filter(node => !node.disabled && !node.hidden && node.getClientRects().length); if (!controls.length) return; const first = controls[0], last = controls.at(-1); if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); } else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); } });
  dialog.addEventListener('close',() => { dialog.remove(); origin?.focus(); });
  $('#dialog-root').append(dialog); dialog.showModal(); return dialog;
}
function details(title, pairs, extra) { return modal(title,null,el('div',{},el('dl',{class:'detail-grid'},pairs.map(([label,value,wide]) => el('div',{class:wide?'detail-wide':''},el('dt',{},label),el('dd',{},value ?? '—')))),extra)); }
function authSession(data) { state.user = data.usuario; state.csrf = data.csrf_token; state.setup = null; if (!state.user || !state.csrf) throw new Error('Não foi possível iniciar a sessão. Tente entrar novamente.'); }
function brand() { return $('.brand').cloneNode(true); }
function showAuth(mode = 'login') {
  closeMenu(); $('#application').hidden = true; $('#auth-root').hidden = false; document.body.classList.remove('dark'); $('#dialog-root').replaceChildren();
  const first = mode === 'register', userName = field('Nome completo','nome',{required:true,maxLength:100,autoComplete:'name'}),email = field('E-mail','email',{type:'email',required:true,maxLength:254,autoComplete:'email'}),password = field('Senha','senha',{type:'password',required:true,minLength:first?12:1,maxLength:128,autoComplete:first?'new-password':'current-password',hint:first?'Use pelo menos 12 caracteres. Sua senha é protegida pelo servidor.':null});
  const form = el('form',{class:'auth-form'},first && userName.row,email.row,password.row);
  if (first && state.setup && pick(state.setup,'primeiro_usuario','first_user','primeiro_cadastro') && finite(pick(state.setup,'legado_sem_vinculo','registros_sem_vinculo','leituras_sem_vinculo','leituras_legado')) > 0) {
    form.append(el('label',{class:'checkbox-row'},el('input',{type:'checkbox',name:'importar_legado'}),el('span',{},'Vincular leituras anteriores à minha conta',el('small',{},`${number(pick(state.setup,'legado_sem_vinculo','registros_sem_vinculo','leituras_sem_vinculo','leituras_legado'))} medições do terminal disponíveis. Os registros originais serão preservados.`))));
  }
  form.append(el('button',{type:'submit',class:'btn btn-primary'},first?'Criar minha conta':'Entrar no AquaSense',icon('arrow')));
  wireForm(form,async data => { const body = {email:data.get('email').trim(),senha:data.get('senha')}; if (first) { body.nome = data.get('nome').trim(); body.importar_legado = data.has('importar_legado'); } authSession(await api(`/auth/${first?'register':'login'}`,{method:'POST',body})); await openApplication(); });
  $('#auth-root').replaceChildren(el('div',{class:'auth-layout'},el('div',{class:'auth-visual'},brand(),el('div',{class:'auth-copy'},el('div',{class:'auth-kicker'},'CADA GOTA CONTA'),el('h1',{},'Mais clareza sobre ',el('br'),el('em',{},'a água que você usa.')),el('p',{},'Acompanhe seu consumo, observe os parâmetros da água e transforme medições em decisões conscientes.'),el('div',{class:'auth-feature'},el('span',{class:'metric-icon'},icon('drop')),'Consumo e estimativa financeira'),el('div',{class:'auth-feature'},el('span',{class:'metric-icon'},icon('shield')),'Histórico de qualidade e alertas'),el('div',{class:'auth-feature'},el('span',{class:'metric-icon'},icon('report')),'Relatórios com seus dados reais')),el('div',{class:'auth-foot'},'AquaSense · Ambiente de desenvolvimento local')),el('section',{class:'auth-form-shell'},el('div',{class:'auth-form-wrap'},el('h2',{},first?'Crie sua conta':'Bem-vindo de volta'),el('p',{},first?'Comece a acompanhar suas medições.':'Entre para acessar seu monitoramento hídrico.'),el('div',{class:'auth-tabs'},button('Entrar',()=>showAuth('login'),'quiet',null,{class:!first?'active':''}),button('Criar conta',()=>showAuth('register'),'quiet',null,{class:first?'active':''})),form,el('p',{class:'auth-note'},'Suas informações ficam na sua conta, no SQL Server local.',el('br'),'Os parâmetros registrados não certificam potabilidade.')))));
  email.input.focus();
}
const routes = [{id:'inicio',title:'Início',icon:'home'},{id:'consumo',title:'Consumo de Água',icon:'drop'},{id:'historico',title:'Histórico',icon:'history'},{id:'relatorios',title:'Relatórios',icon:'report'},{id:'alertas',title:'Alertas',icon:'bell'},{id:'configuracoes',title:'Configurações',icon:'settings'}];
function updateProfile() { const user = state.user || {}; $('#profile-name').textContent = user.nome || 'Minha conta'; $('#profile-initials').textContent = (user.nome || 'A').trim().split(/\s+/).slice(0,2).map(v=>v[0]).join('').toUpperCase(); }
function applyTheme() { document.body.classList.toggle('dark',state.preferences.tema === 'escuro'); }
function updateNotifications(count = state.alertCount) { state.alertCount=finite(count);const visible=!!state.preferences.notificacoes && state.alertCount>0;const badge=$('#notification-count');if(badge){badge.hidden=!visible;badge.textContent=visible?String(state.alertCount>99?'99+':state.alertCount):'';}$('#notifications-link').classList.toggle('notification-active',visible);$('#notifications-link').setAttribute('aria-label',visible?`Abrir alertas · ${number(state.alertCount)} abertos no período`:'Abrir alertas'); }
async function openApplication() {
  $('#auth-root').replaceChildren(); $('#auth-root').hidden = true; $('#application').hidden = false; updateProfile();
  try { const config = await api('/configuracoes'); state.preferences = config.preferencias || {}; state.meterCache = config.medidores || []; applyTheme(); } catch (_) { /* Each view exposes its own server error and retry. */ }
  routeFromHash();
}
function nav(id) { const target = routes.some(r=>r.id===id) ? id : 'inicio'; if (location.hash === `#${target}`) renderPage(target); else location.hash = target; closeMenu(); }
function routeFromHash() { const id = location.hash.slice(1).split('?')[0]; if (state.user) renderPage(routes.some(r=>r.id===id)?id:'inicio'); }
function syncSidebar() { const mobile=window.matchMedia('(max-width:760px)').matches,closed=!$('#sidebar').classList.contains('open');$('#sidebar').inert=mobile&&closed;if(mobile&&closed)$('#sidebar').setAttribute('aria-hidden','true');else $('#sidebar').removeAttribute('aria-hidden');document.body.classList.toggle('menu-open',mobile&&!closed); }
function closeMenu() { $('#sidebar').classList.remove('open'); $('#menu-shade').hidden = true; $('#mobile-menu').setAttribute('aria-expanded','false'); syncSidebar(); }
function table(columns, items, render, emptyText = 'Nenhum registro encontrado para os filtros selecionados.') { return el('div',{class:'table-scroll'},el('table',{},el('thead',{},el('tr',{},columns.map(col=>el('th',{scope:'col',class:col==='Ações'?'no-print':null},col)))),el('tbody',{},items.length ? items.map(item=>el('tr',{},render(item).map((cell,index)=>el('td',{class:columns[index]==='Ações'?'no-print':null},cell)))) : el('tr',{},el('td',{colSpan:columns.length,class:'table-empty'},emptyText))))); }
function pagination(data, callback) { const page = finite(data.page) || 1, total = finite(data.total) || 0, pages = finite(data.total_pages) || Math.ceil(total/(finite(data.page_size)||10)) || 1; return el('div',{class:'pagination'},el('span',{},`${number(total)} registros · página ${page} de ${pages}`),el('div',{class:'pagination-actions'},button('Anterior',()=>callback(page-1),'secondary',null,{disabled:page<=1,class:'btn btn-secondary btn-small'}),button('Próxima',()=>callback(page+1),'secondary',null,{disabled:page>=pages,class:'btn btn-secondary btn-small'}))); }
const statusLabels = {ABERTO:'Aberto',EM_ANALISE:'Em análise',RESOLVIDO:'Resolvido',ATIVO:'Ativo',INATIVO:'Inativo'};
const typeLabels = {CONSUMO_ELEVADO:'Consumo elevado',POSSIVEL_VAZAMENTO:'Possível vazamento',QUALIDADE:'Qualidade da água',OUTROS:'Outros'};
const originLabels = {MANUAL:'Manual',INFORMADO:'Informado',MEDIDO:'Medido',ESTIMADO:'Estimado',TERMINAL:'Terminal',LEGADO:'Terminal · legado'};
function statusBadge(value) { return el('span',{class:`badge ${value==='ABERTO'?'badge-open':value==='EM_ANALISE'?'badge-analysis':value==='RESOLVIDO'||value==='ATIVO'?'badge-resolved':''}`},statusLabels[value] || value || '—'); }
function severityBadge(value) { const label = {ALTA:'Alta',MEDIA:'Média',BAIXA:'Baixa',CRITICA:'Crítica'}[value] || value || '—'; return el('span',{class:`badge ${value==='ALTA'||value==='CRITICA'?'badge-high':value==='MEDIA'?'badge-medium':'badge-low'}`},label); }
function rangeDefault(preset = '6m') { const end = new Date(); end.setDate(end.getDate()+1); end.setHours(0,0,0,0); const start = new Date(end); if (preset === '7d') start.setDate(start.getDate()-7); else if (preset === '30d') start.setDate(start.getDate()-30); else start.setMonth(start.getMonth()-(preset==='12m'?12:6)); return {inicio:start.toISOString().slice(0,19)+'Z',fim:end.toISOString().slice(0,19)+'Z',agrupamento:preset.endsWith('m')?'mes':'dia'}; }
function filterForm(callback, options = {}) {
  const preset = field('Período','periodo',{choices:[...(options.all?[['all','Todos os registros']]:[]),['7d','Últimos 7 dias'],['30d','Últimos 30 dias'],['6m','Últimos 6 meses'],['12m','Últimos 12 meses'],['custom','Personalizado']],value:options.preset || '30d'});
  const initial = options.range || rangeDefault(options.preset || '30d');
  const start = field('De','inicio',{type:'date',required:true,value:localDate(initial.inicio)}), endDate = new Date(initial.fim); endDate.setDate(endDate.getDate()-1);
  const end = field('Até','fim',{type:'date',required:true,value:localDate(endDate)}), group = field('Agrupar por','agrupamento',{choices:[['dia','Dia'],['mes','Mês']],value:initial.agrupamento});
  const custom = el('div',{class:'filter-dates',hidden:preset.input.value!=='custom'},start.row,end.row);
  const form = el('form',{class:'filter-bar no-print'},preset.row,custom,options.group!==false && group.row);
  let meter;
  if (options.meters) { meter = field('Hidrômetro','medidor_id',{choices:[['','Todos os hidrômetros'],...options.meters.map(m=>[m.id,m.nome])],value:options.medidor_id || ''}); form.append(meter.row); }
  preset.input.addEventListener('change',()=>{ custom.hidden = preset.input.value!=='custom'; start.input.required = end.input.required = preset.input.value==='custom'; if (preset.input.value!=='custom') group.input.value = preset.input.value.endsWith('m')?'mes':'dia'; });
  start.input.required = end.input.required = preset.input.value==='custom';
  form.append(el('button',{class:'btn btn-primary',type:'submit'},icon('search'),'Aplicar'));
  wireForm(form,async data=>{ let range = preset.input.value==='all'?{}:preset.input.value==='custom' ? {inicio:dayUTC(data.get('inicio')),fim:dayUTC(data.get('fim'),true)} : rangeDefault(preset.input.value); if (range.inicio && new Date(range.inicio)>=new Date(range.fim)) throw new Error('A data final precisa ser igual ou posterior à data inicial.'); range.agrupamento = group.input.value; if (meter && meter.input.value) range.medidor_id = meter.input.value; await callback(range); });
  return form;
}
function svgEl(tag, attrs = {}, text) { const node = document.createElementNS('http://www.w3.org/2000/svg',tag); for (const [key,value] of Object.entries(attrs)) node.setAttribute(key,String(value)); if (text !== undefined) node.textContent = text; return node; }
function chartLabel(value, monthly = false) { if (!value) return '—'; const d = new Date(value.length===7?`${value}-01T12:00:00`:value.length===10?`${value}T12:00:00`:value); return Number.isNaN(d.valueOf()) ? value : new Intl.DateTimeFormat('pt-BR',monthly?{month:'short',year:'2-digit'}:{day:'2-digit',month:'short'}).format(d); }
function chart(series, kind = 'line', grouping = 'mes') {
  const points = list(series).filter(row=>finite(pick(row,'total_litros','litros'))!==null);
  if (!points.length) return empty('Sem dados para o gráfico','Registre pelo menos duas leituras de um hidrômetro para calcular o consumo.','chart');
  const wrap = el('div',{class:'chart-area'}), uid = `area-${Math.random().toString(36).slice(2,8)}`;
  const svg = svgEl('svg',{class:'chart-svg',viewBox:'0 0 650 260',role:'img','aria-labelledby':`${uid}-title ${uid}-desc`});
  svg.append(svgEl('title',{id:`${uid}-title`},`Consumo por ${grouping==='mes'?'mês':'dia'}`),svgEl('desc',{id:`${uid}-desc`},`${points.length} períodos com consumo registrado. Uma tabela equivalente está disponível abaixo.`));
  const left = 52, right = 635, top = 18, bottom = 217, maxValue = Math.max(...points.map(r=>Number(pick(r,'total_litros','litros'))),1)*1.14;
  const unitFactor = state.preferences.unidade==='m3'?1000:1;
  for (let i=0;i<5;i++) { const y = bottom-(bottom-top)*i/4; svg.append(svgEl('line',{x1:left,x2:right,y1:y,y2:y,stroke:'var(--line)','stroke-dasharray':'3 4'}),svgEl('text',{x:left-10,y:y+4,'text-anchor':'end'},number(maxValue*i/4/unitFactor,maxValue/unitFactor<10?1:0))); }
  svg.append(svgEl('text',{x:left-12,y:7,'text-anchor':'end'},volumeUnit()));
  const xOf = i => kind==='bar'?left+(right-left)*(i+.5)/points.length : points.length===1?(left+right)/2:left+(right-left)*i/(points.length-1);
  const mapped = points.map((row,i)=>({row,x:xOf(i),y:bottom-(bottom-top)*Number(pick(row,'total_litros','litros'))/maxValue}));
  if (kind==='line') { const defs = svgEl('defs'),gradient = svgEl('linearGradient',{id:uid,x1:'0',y1:'0',x2:'0',y2:'1'}); gradient.append(svgEl('stop',{offset:'0%','stop-color':'#1674ea','stop-opacity':'.14'}),svgEl('stop',{offset:'100%','stop-color':'#1674ea','stop-opacity':'.01'})); defs.append(gradient); svg.append(defs); const curve = mapped.map((p,i)=>`${i?'L':'M'}${p.x},${p.y}`).join(' '); if(mapped.length>1) svg.append(svgEl('path',{d:`${curve} L${mapped.at(-1).x},${bottom} L${mapped[0].x},${bottom} Z`,fill:`url(#${uid})`})); svg.append(svgEl('path',{d:curve,fill:'none',stroke:'#1674ea','stroke-width':'2.4','stroke-linecap':'round','stroke-linejoin':'round'})); }
  const tooltip = el('div',{class:'chart-tooltip',hidden:true});
  mapped.forEach((point,i)=>{
    const label = chartLabel(point.row.periodo,grouping==='mes'),value = displayVolume(pick(point.row,'total_litros','litros'));
    const shape = kind==='bar'?svgEl('rect',{x:point.x-Math.max(2,Math.min(23,(right-left)/points.length*.29)),y:point.y,width:Math.max(4,Math.min(46,(right-left)/points.length*.58)),height:Math.max(1,bottom-point.y),rx:3,fill:'#3d8bef',class:'chart-point',tabindex:0,role:'img','aria-label':`${label}: ${value}`}):svgEl('circle',{cx:point.x,cy:point.y,r:4,fill:'#1674ea',stroke:'var(--surface)','stroke-width':2,class:'chart-point',tabindex:0,role:'img','aria-label':`${label}: ${value}`});
    const show = ()=>{ tooltip.textContent = `${label} · ${value}`; tooltip.hidden = false; };
    shape.addEventListener('pointerenter',show);shape.addEventListener('pointerleave',()=>tooltip.hidden=true);shape.addEventListener('focus',show);shape.addEventListener('blur',()=>tooltip.hidden=true); svg.append(shape);
    const step = Math.ceil(points.length/7); if (i%step===0 || i===points.length-1) svg.append(svgEl('text',{x:point.x,y:bottom+24,'text-anchor':'middle'},label));
  });
  wrap.append(svg,tooltip,el('div',{class:'chart-legend'},el('i'),'Consumo registrado'),el('details',{class:'chart-data-details'},el('summary',{},'Consultar os valores do gráfico'),table(['Período',`Consumo (${volumeUnit()})`],points,row=>[chartLabel(row.periodo,grouping==='mes'),displayVolume(row.total_litros)])));
  return wrap;
}
function donut(categories) {
  const rows = list(categories).filter(r=>decimalParts(r.litros)?.coefficient>0n),total = decimalSum(rows.map(row=>row.litros));
  if (!decimalParts(total) || decimalParts(total).coefficient===0n) return empty('Categorias sem registros','Distribua manualmente o consumo ou registre uma medição por categoria.','category');
  const colors = ['#1674ea','#65a3f1','#68c0d2','#99d6d1','#a5b6d3','#8994b0'],svg = svgEl('svg',{class:'donut-svg',viewBox:'0 0 160 160',role:'img','aria-label':`Distribuição de ${displayVolume(total)} por categorias informadas, medidas ou estimadas.`});
  const circumference = 2*Math.PI*57; let offset=0;
  rows.forEach((row,index)=>{const size=Number(row.litros)/total*circumference;svg.append(svgEl('circle',{cx:80,cy:80,r:57,fill:'none',stroke:colors[index%colors.length],'stroke-width':20,'stroke-dasharray':`${size} ${circumference-size}`,'stroke-dashoffset':-offset,transform:'rotate(-90 80 80)'}));offset+=size;});
  svg.append(svgEl('text',{x:80,y:77,'text-anchor':'middle',fill:'var(--text)','font-size':18,'font-weight':700},number(decimalShift(total,state.preferences.unidade==='m3'?3:0),state.preferences.unidade==='m3'?2:0)),svgEl('text',{x:80,y:98,'text-anchor':'middle',fill:'var(--muted)','font-size':9},`${volumeUnit()} classificados`));
  return el('div',{},el('div',{class:'donut-layout'},svg,el('div',{class:'donut-legend'},rows.map((row,index)=>el('div',{class:'donut-item'},el('i',{class:`legend-color-${index%colors.length}`}),el('span',{},row.nome,el('span',{class:'table-secondary'},originLabels[row.origem] || 'Informado')),el('strong',{},`${number(decimalPercentage(row.litros,total,1),1)}%`))))),el('p',{class:'chart-caption'},'Percentuais sobre o volume classificado. A origem de cada registro é informada pelo usuário.'));
}

async function getMeters(refresh = false) { if (refresh || !state.meterCache.length) state.meterCache = list(await api('/medidores')); return state.meterCache; }
async function openReadingModal(done = () => renderPage(state.page)) {
  let meters;
  try { meters = (await getMeters(true)).filter(m=>m.status==='ATIVO'); } catch(error) { toast(safeMessage(error),true); return; }
  if (!meters.length) { modal('Cadastre seu hidrômetro','Para registrar o consumo, primeiro identifique um hidrômetro ativo.',close=>el('div',{},note('Cada hidrômetro mantém sua própria sequência de leituras acumuladas.'),button('Cadastrar hidrômetro',()=>{close();openMeterModal(done);},'primary','plus'))); return; }
  modal('Registrar leitura de consumo','Informe o número acumulado indicado no hidrômetro, em m³. O consumo será calculado entre esta e a leitura anterior.',close=>{
    const meter=field('Hidrômetro','medidor_id',{required:true,choices:meters.map(m=>[m.id,`${m.nome} · ${m.codigo}`])}),value=field('Leitura acumulada (m³)','acumulado_m3',{type:'number',min:0,step:'.001',max:'999999999999999.999',required:true,placeholder:'Ex.: 125,340',hint:'1 m³ equivale a 1.000 litros. Até três casas decimais.'}),when=field('Data e hora da leitura','registrado_em_utc',{type:'datetime-local',required:true,value:localInput(),max:localInput(),hint:'Horário local, convertido para UTC ao salvar.'}),obs=field('Observações','observacoes',{type:'textarea',maxLength:1000,placeholder:'Opcional · condições ou contexto da leitura',wide:true});
    const form=el('form',{},el('div',{class:'form-grid'},meter.row,value.row,when.row,obs.row),el('div',{class:'form-actions'},button('Cancelar',close),el('button',{type:'submit',class:'btn btn-primary'},icon('check'),'Salvar leitura')));
    return wireForm(form,async data=>{const accumulated=data.get('acumulado_m3');if(!/^\d+(\.\d{1,3})?$/.test(accumulated))throw new Error('Informe um valor positivo com até três casas decimais.');await api('/consumo/leituras',{method:'POST',body:{medidor_id:Number(data.get('medidor_id')),acumulado_m3:accumulated,registrado_em_utc:utc(data.get('registrado_em_utc')),observacoes:data.get('observacoes').trim()}});close();toast('Leitura de consumo registrada.');await done();});
  });
}
function openQualityModal(done = () => renderPage(state.page)) {
  modal('Registrar parâmetros de qualidade','A medição será registrada no horário atual. Temperatura, pH e turbidez são indicadores de acompanhamento e não certificam potabilidade.',close=>{
    const temp=field('Temperatura (°C)','temperatura_c',{type:'number',step:'any',min:-20,max:100,required:true,hint:'Entre −20 °C e 100 °C.'}),ph=field('pH','ph',{type:'number',step:'any',min:0,max:14,required:true,hint:'Entre 0 e 14.'}),turbidity=field('Turbidez (NTU)','turbidez_ntu',{type:'number',step:'any',min:0,required:true,hint:'Valor maior ou igual a zero.'});
    const form=el('form',{},el('div',{class:'form-grid'},temp.row,ph.row,turbidity.row),el('div',{class:'form-actions'},button('Cancelar',close),el('button',{type:'submit',class:'btn btn-primary'},icon('check'),'Salvar medição')));
    return wireForm(form,async data=>{await api('/qualidade/leituras',{method:'POST',body:{temperatura_c:Number(data.get('temperatura_c')),ph:Number(data.get('ph')),turbidez_ntu:Number(data.get('turbidez_ntu'))}});close();toast('Medição de qualidade registrada.');await done();});
  });
}
function openMeterModal(done = () => renderPage(state.page), replacing) {
  modal(replacing?'Substituir hidrômetro':'Cadastrar hidrômetro',replacing?`A nova sequência será independente. O hidrômetro ${replacing.nome} será identificado como substituído, preservando as leituras anteriores.`:'Identifique o equipamento. As leituras acumuladas são mantidas separadamente para cada hidrômetro.',close=>{
    const name=field('Nome do hidrômetro','nome',{required:true,maxLength:100,placeholder:'Ex.: Entrada principal'}),code=field('Código ou número de série','codigo',{required:true,maxLength:64,placeholder:'Identificação do equipamento'}),when=field('Data e hora de instalação','instalado_em_utc',{type:'datetime-local',required:true,value:localInput(),wide:true,hint:'As leituras devem ser posteriores à instalação.'});
    const form=el('form',{},el('div',{class:'form-grid'},name.row,code.row,when.row),el('div',{class:'form-actions'},button('Cancelar',close),el('button',{type:'submit',class:'btn btn-primary'},icon('check'),replacing?'Registrar substituição':'Cadastrar')));
    return wireForm(form,async data=>{const body={nome:data.get('nome').trim(),codigo:data.get('codigo').trim(),instalado_em_utc:utc(data.get('instalado_em_utc'))};if(replacing)body.substitui_medidor_id=replacing.id;await api('/medidores',{method:'POST',body});state.meterCache=[];close();toast(replacing?'Hidrômetro substituído. O histórico foi preservado.':'Hidrômetro cadastrado.');await done();});
  });
}
async function openCategoryModal(done = () => renderPage(state.page), filters = {}) {
  const selectedFilters={...filters};
  let categoryData,readings;
  try {
    [categoryData,readings]=await Promise.all([
      api('/categorias',{query:selectedFilters}),
      api('/consumo/historico',{query:{...selectedFilters,page:1,page_size:100,ordem:'desc'}})
    ]);
  } catch(error) {toast(safeMessage(error),true);return;}
  const categories=list(categoryData),firstValid=list(readings).filter(row=>decimalParts(row.consumo_litros)?.coefficient>0n);
  if(!firstValid.length && (Number(readings.total_pages)||1)<=1){
    modal('Sem consumo para classificar','Não há intervalos com consumo positivo nos filtros aplicados. Ajuste o período ou hidrômetro para consultar outros registros.',close=>button('Fechar',close));
    return;
  }
  modal('Classificar consumo por categoria','O período e o hidrômetro seguem os filtros aplicados. Informe a parte de um intervalo destinada a uma categoria; a soma não pode exceder seu consumo. A origem ficará identificada.',close=>{
    const reading=field('Intervalo de consumo','leitura_id',{required:true,wide:true,choices:[]});
    const category=field('Categoria','categoria_id',{required:true,choices:categories.map(c=>[c.id,c.nome])});
    const volume=field('Volume classificado (L)','litros',{type:'number',required:true,min:'.001',step:'.001'});
    const origin=field('Origem desta informação','origem',{required:true,wide:true,choices:[['INFORMADO','Informado manualmente'],['MEDIDO','Medição específica'],['ESTIMADO','Estimativa do usuário']]});
    const pickerPages=el('div'),pickerMessage=el('p',{class:'small-text',role:'status'});
    const save=el('button',{type:'submit',class:'btn btn-primary'},icon('check'),'Salvar categoria');
    const form=el('form',{},reading.row,pickerMessage,pickerPages,el('div',{class:'form-grid'},category.row,volume.row,origin.row),el('div',{class:'form-actions'},button('Cancelar',close),save));
    let selectionRequest=0,selectionData=readings;
    function showReadings(data){
      selectionData=data;
      const valid=list(data).filter(row=>decimalParts(row.consumo_litros)?.coefficient>0n);
      reading.input.replaceChildren(...valid.map(row=>el('option',{value:row.id},`${date(row.registrado_em_utc)} · ${row.medidor_nome} · ${displayVolume(row.consumo_litros)}`)));
      reading.input.disabled=!valid.length;
      save.disabled=!valid.length;
      pickerMessage.textContent=valid.length?'Somente intervalos com consumo positivo podem ser classificados.':'Esta página não tem consumo positivo. Consulte outra página de leituras.';
      pickerPages.replaceChildren(pagination(data,changePage));
    }
    async function changePage(page){
      const ticket=++selectionRequest;
      reading.input.disabled=true;save.disabled=true;
      pickerMessage.textContent='Carregando os intervalos desta página…';
      pickerPages.querySelectorAll('button').forEach(control=>control.disabled=true);
      try{
        const data=await api('/consumo/historico',{query:{...selectedFilters,page,page_size:100,ordem:'desc'}});
        if(ticket!==selectionRequest || !form.isConnected)return;
        clearError(form);showReadings(data);
      }catch(error){
        if(ticket!==selectionRequest || !form.isConnected)return;
        showReadings(selectionData);formError(form,safeMessage(error));
      }
    }
    showReadings(readings);
    return wireForm(form,async data=>{
      await api('/categorias/registros',{method:'POST',body:{leitura_id:Number(data.get('leitura_id')),categoria_id:Number(data.get('categoria_id')),litros:data.get('litros'),origem:data.get('origem')}});
      close();toast('Volume classificado com origem registrada.');await done();
    });
  });
}
function qualityTimestamp(row) { if(row.registrado_em_utc) return date(row.registrado_em_utc); const value=pick(row,'registrado_em_original','registrado_em'); return value ? `${String(value).replace('T',' ').slice(0,19)} · horário original` : 'Sem horário confirmado'; }
function consumptionDetails(row) { details('Detalhes da leitura de consumo',[['Identificador',`#${row.id}`],['Hidrômetro',`${row.medidor_nome || '—'} (#${row.medidor_id})`],['Data e hora',date(row.registrado_em_utc)],['Leitura anterior',date(row.anterior_em_utc)],['Acumulado do contador',`${number(row.acumulado_m3,3)} m³`],['Consumo do intervalo',displayVolume(row.consumo_litros)],['Tarifa do intervalo',decimalParts(row.tarifa_valor_m3)===null?(decimalParts(row.consumo_litros)===null?'Leitura inicial':'Sem tarifa'):money(row.tarifa_valor_m3,4)+' por m³'],['Vigência da tarifa',date(row.tarifa_inicio_utc)],['Custo estimado do intervalo',decimalParts(row.estimativa_reais)===null?'Indisponível':money(row.estimativa_reais)],['Origem',originLabels[row.origem] || row.origem],['Observações',row.observacoes || 'Nenhuma observação',true]],note(decimalParts(row.consumo_litros)===null?'A primeira leitura forma a base deste hidrômetro. Ainda não há consumo calculado.':'O consumo corresponde à diferença entre leituras acumuladas deste mesmo hidrômetro.')); }
function qualityDetails(row) { details('Detalhes da medição de qualidade',[['Identificador',`#${row.id}`],['Data e hora',qualityTimestamp(row)],['Temperatura',`${number(row.temperatura_c,2)} °C`],['pH',number(row.ph,2)],['Turbidez',`${number(row.turbidez_ntu,2)} NTU`],['Origem',originLabels[row.origem] || row.origem],['Vínculo',row.origem_vinculo || 'Conta autenticada'],['Referência de fuso',row.fuso_origem_windows || (row.horario_confirmado_utc?'UTC confirmado':'Não informado')]],note('Estes parâmetros não permitem certificar que a água é potável. Horários legados são exibidos como registrados, sem presumir um fuso.')); }
function consumptionTable(items) { return table(['Data e hora','Hidrômetro','Acumulado (m³)',`Consumo (${volumeUnit()})`,'Origem','Observações','Ações'],items,row=>[date(row.registrado_em_utc),el('span',{},el('span',{class:'table-main'},row.medidor_nome || '—'),el('span',{class:'table-secondary'},`#${row.medidor_id}`)),number(row.acumulado_m3,3),decimalParts(row.consumo_litros)===null?el('span',{class:'muted'},'Leitura inicial'):displayVolume(row.consumo_litros),originLabels[row.origem] || row.origem || '—',el('span',{class:'row-description',title:row.observacoes || ''},row.observacoes || '—'),button('Detalhes',()=>consumptionDetails(row),'quiet',null,{class:'btn btn-quiet btn-small','aria-label':`Detalhes da leitura ${row.id}`})]); }
function qualityTable(items) { return table(['Data e hora','Temperatura (°C)','pH','Turbidez (NTU)','Origem','Ações'],items,row=>[qualityTimestamp(row),number(row.temperatura_c,2),number(row.ph,2),number(row.turbidez_ntu,2),originLabels[row.origem] || row.origem || '—',button('Detalhes',()=>qualityDetails(row),'quiet',null,{class:'btn btn-quiet btn-small','aria-label':`Detalhes da medição ${row.id}`})]); }
function recentAlerts(alerts,refresh) { const rows=list(alerts).slice(0,4); return rows.length ? el('div',{class:'recent-alerts'},rows.map(row=>el('div',{class:'recent-alert'},el('span',{class:`alert-sign ${row.severidade==='ALTA'?'high':''}`},icon(row.tipo==='QUALIDADE'?'shield':'alert')),el('button',{type:'button',onclick:()=>alertDetails(row,refresh)},el('h3',{},typeLabels[row.tipo] || row.tipo),el('p',{},row.descricao),el('time',{},date(row.criado_em_utc))),statusBadge(row.status)))) : empty('Nenhum alerta registrado','Ocorrências serão exibidas conforme suas medições e os limites configurados.','bell'); }
async function dashboardPage() {
  let range=rangeDefault('6m'), requestId=0; const data=await api('/dashboard',{query:range});
  const section=el('div'),body=el('div');
  const refresh=async next=>{range=next || range;const ticket=++requestId;body.replaceChildren(loading());try{const result=await api('/dashboard',{query:range});if(ticket===requestId)draw(result);}catch(error){if(ticket===requestId)body.replaceChildren(errorBlock(error,()=>refresh()));}};
  const period=el('select',{'aria-label':'Período do gráfico',class:'btn btn-secondary'},[['30d','Últimos 30 dias'],['6m','Últimos 6 meses'],['12m','Últimos 12 meses']].map(([value,label])=>el('option',{value,selected:value==='6m'},label)));period.addEventListener('change',()=>refresh(rangeDefault(period.value)));
  section.append(pageHead(`Olá, ${(state.user.nome || '').trim().split(/\s+/)[0]}!`,'Aqui está um resumo do seu monitoramento de água.',[el('div',{class:'date-badge'},icon('calendar'),date(new Date().toISOString(),false)),button('Registrar leitura',()=>openReadingModal(()=>refresh()),'primary','plus')]),body);
  function draw(current){updateNotifications(current.alertas?.contagens?.ABERTO);body.replaceChildren(consumeMetrics(current,true),el('div',{class:'dashboard-columns'},panel('Evolução do consumo','Volumes atribuídos ao fim de cada intervalo',chart(current.serie,'line',range.agrupamento),period),panel('Alertas recentes','Acompanhe ocorrências e atualize o status',recentAlerts(current.alertas,()=>refresh()),button('Ver todos',()=>nav('alertas'),'quiet'))),panel('Ações rápidas','Acesse as principais ferramentas do AquaSense',el('div',{class:'quick-grid'},[['Registrar leitura','plus',()=>openReadingModal(()=>refresh())],['Ver histórico','history',()=>nav('historico')],['Gerar relatório','report',()=>nav('relatorios')],['Configurações','settings',()=>nav('configuracoes')]].map(([label,symbol,action])=>el('button',{type:'button',class:'quick-action',onclick:action},icon(symbol),label)))),note(current.qualidade?.nota || 'As medições de qualidade acompanham parâmetros individuais e não certificam potabilidade.'));
  }
  draw(data);return section;
}
async function consumptionPage() {
  const meters=await getMeters(true);let range=rangeDefault('30d'),page=1,requestId=0;const root=el('div'),body=el('div');
  const refresh=async(next,newPage=1)=>{range=next || range;page=newPage;const ticket=++requestId;body.replaceChildren(loading());try{const [summary,history]=await Promise.all([api('/consumo/resumo',{query:range}),api('/consumo/historico',{query:{...range,page,page_size:10,ordem:'desc'}})]);if(ticket===requestId)draw(summary,history);}catch(error){if(ticket===requestId)body.replaceChildren(errorBlock(error,()=>refresh(range,page)));}};
  root.append(pageHead('Consumo de Água','Observe o consumo entre leituras e acompanhe a evolução.',[button('Registrar leitura',()=>openReadingModal(()=>refresh()),'primary','plus')]),el('section',{class:'panel'},filterForm(refresh,{meters})),body);
  function draw(summary,history){body.replaceChildren(consumeMetrics(summary),el('div',{class:'chart-grid'},panel('Consumo por período',range.agrupamento==='mes'?'Volumes mensais calculados':'Volumes diários calculados',chart(summary.serie,'bar',range.agrupamento)),panel('Distribuição por categoria','Volumes informados, medidos ou estimados',donut(summary.categorias),button('Classificar',()=>openCategoryModal(()=>refresh(),{...range}),'quiet','plus'))),el('section',{class:'panel'},el('div',{class:'panel-head'},el('div',{class:'panel-title'},el('h2',{},'Últimas leituras'),el('p',{},'Leitura acumulada do hidrômetro e consumo calculado')),button('Histórico completo',()=>nav('historico'),'quiet')),el('div',{class:'panel-spacer'}),consumptionTable(list(history)),pagination(history,p=>refresh(range,p))),note(summary.nota_atribuicao || 'O consumo é calculado entre leituras consecutivas. Períodos sem medições não são preenchidos com valores inventados.'),summary.base_estimativa&&note(summary.base_estimativa));
  }
  await refresh();return root;
}
async function historyPage() {
  const meters=await getMeters(true);let range={},tab='consumo',page=1,order='desc',current,requestId=0;
  const root=el('div'),body=el('div'),actions=el('div',{class:'section-actions'}),tabs=el('div',{class:'tabs no-print'}),panelNode=el('section',{class:'panel'});
  const historyQuery=()=>tab==='consumo'&&!range.inicio?{...range,inicio:'0001-01-01T00:00:00Z',fim:rangeDefault('7d').fim}:range;
  const refresh=async(next,newPage=1)=>{range=next || range;page=newPage;const ticket=++requestId;body.replaceChildren(loading());try{const result=await api(tab==='consumo'?'/consumo/historico':'/qualidade/leituras',{query:{...historyQuery(),page,page_size:10,ordem:order}});if(ticket===requestId){current=result;draw();}}catch(error){if(ticket===requestId)body.replaceChildren(errorBlock(error,()=>refresh(range,page)));}};
  const exportCurrentHistory=async()=>{
    const selectedTab=tab,selectedQuery={...historyQuery(),ordem:order};
    const rows=await collectPages(selectedTab==='consumo'?'/consumo/historico':'/qualidade/leituras',selectedQuery);
    exportReadings(selectedTab,rows);
  };
  const sortButton=button('Mais recentes primeiro',()=>{order=order==='desc'?'asc':'desc';sortButton.replaceChildren(icon('history'),order==='desc'?'Mais recentes primeiro':'Mais antigos primeiro');refresh();},'secondary','history',{class:'btn btn-secondary btn-small'});
  function switchTab(next){tab=next;for(const b of tabs.children){const active=b.dataset.tab===tab;b.classList.toggle('active',active);b.setAttribute('aria-selected',String(active));}actions.replaceChildren(button(tab==='consumo'?'Registrar leitura':'Registrar qualidade',()=>tab==='consumo'?openReadingModal(()=>refresh()):openQualityModal(()=>refresh()),'primary','plus'),button('Exportar CSV',exportCurrentHistory,'secondary','download'));refresh();}
  tabs.setAttribute('role','tablist');tabs.setAttribute('aria-label','Tipo de histórico');
  for(const [id,label] of [['consumo','Consumo'],['qualidade','Qualidade da água']]){const b=el('button',{type:'button',role:'tab','aria-selected':id===tab,onclick:()=>switchTab(id),class:id===tab?'active':''},label);b.dataset.tab=id;tabs.append(b);}
  tabs.addEventListener('keydown',event=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;event.preventDefault();const next=tab==='consumo'?'qualidade':'consumo';switchTab(event.key==='Home'?'consumo':event.key==='End'?'qualidade':next);[...tabs.children].find(b=>b.dataset.tab===tab)?.focus();});
  const filters=filterForm(refresh,{preset:'all',all:true,group:false,meters});
  panelNode.append(tabs,filters,el('div',{class:'panel-head no-print'},el('p',{class:'small-text'},'Dados persistidos · horário local para registros UTC'),sortButton),el('div',{class:'panel-spacer'}),body);
  root.append(pageHead('Histórico','Consulte e rastreie os registros de consumo e de qualidade.',[actions]),panelNode);
  function draw(){body.replaceChildren(tab==='consumo'?consumptionTable(list(current)):qualityTable(list(current)),pagination(current,p=>refresh(range,p)));if(tab==='qualidade')body.append(el('div',{class:'source-note'},'Medições antigas sem fuso confirmado exibem o horário original. O filtro UTC não presume o horário das medições legadas.'));}
  actions.append(button('Registrar leitura',()=>openReadingModal(()=>refresh()),'primary','plus'),button('Exportar CSV',exportCurrentHistory,'secondary','download'));
  await refresh();return root;
}
async function alertDetails(row,done = ()=>renderPage(state.page)) {
  const history=list(row.historico),timeline=el('div',{},el('h3',{},'Histórico de status'),history.length?el('ol',{class:'timeline'},history.map(item=>el('li',{},el('strong',{},`${item.status_anterior?`${statusLabels[item.status_anterior] || item.status_anterior} → `:''}${statusLabels[item.status_novo] || item.status_novo}`),date(item.alterado_em_utc),item.observacao && el('p',{},item.observacao)))):el('p',{class:'small-text'},'Nenhuma alteração registrada.'));
  modal('Detalhes do alerta',null,close=>{
    const status=field('Status','status',{choices:Object.entries(statusLabels).filter(([id])=>['ABERTO','EM_ANALISE','RESOLVIDO'].includes(id)),value:row.status});
    const form=el('form',{},status.row,el('div',{class:'form-actions'},button('Fechar',close),el('button',{type:'submit',class:'btn btn-primary'},icon('check'),'Atualizar status')));
    wireForm(form,async data=>{await api(`/alertas/${row.id}`,{method:'PATCH',body:{status:data.get('status')}});close();toast('Status atualizado e alteração registrada.');await done();});
    return el('div',{},el('dl',{class:'detail-grid'},[['Identificador',`#${row.id}`],['Tipo',typeLabels[row.tipo] || row.tipo],['Registrado em',date(row.criado_em_utc)],['Severidade',severityBadge(row.severidade)],['Descrição',row.descricao,true],['Leitura de origem',row.leitura_consumo_id?`Consumo #${row.leitura_consumo_id}`:row.leitura_qualidade_id?`Qualidade #${row.leitura_qualidade_id}`:'—']].map(([label,value,wide])=>el('div',{class:wide?'detail-wide':''},el('dt',{},label),el('dd',{},value)))),note('Alertas representam condições observadas ou indícios. Um possível vazamento exige verificação; os parâmetros não certificam potabilidade.'),timeline,el('div',{class:'divider'}),form);
  });
}
function alertsTable(items,done) { return table(['Data e hora','Tipo','Descrição','Severidade','Status','Ações'],items,row=>[date(row.criado_em_utc),typeLabels[row.tipo] || row.tipo,el('span',{class:'row-description'},row.descricao),severityBadge(row.severidade),statusBadge(row.status),button('Detalhes',()=>alertDetails(row,done),'quiet',null,{class:'btn btn-quiet btn-small','aria-label':`Detalhes do alerta ${row.id}`})]); }
async function alertsPage() {
  let range=rangeDefault('6m'),page=1,type='',status='',requestId=0;const root=el('div'),body=el('div');
  const refresh=async(next,newPage=1)=>{range=next || range;page=newPage;const ticket=++requestId;body.replaceChildren(loading());try{const result=await api('/alertas',{query:{...range,tipo:type,status,page,page_size:10}});if(ticket===requestId)draw(result);}catch(error){if(ticket===requestId)body.replaceChildren(errorBlock(error,()=>refresh(range,page)));}};
  const typeField=field('Tipo de alerta','tipo',{choices:[['','Todos os tipos'],...Object.entries(typeLabels)]}),statusField=field('Status','status',{choices:[['','Todos os status'],...Object.entries(statusLabels).filter(([id])=>['ABERTO','EM_ANALISE','RESOLVIDO'].includes(id))]});
  const filters=filterForm(next=>{type=typeField.input.value;status=statusField.input.value;return refresh(next);},{preset:'6m',group:false});filters.insertBefore(typeField.row,filters.lastElementChild);filters.insertBefore(statusField.row,filters.lastElementChild);
  root.append(pageHead('Alertas e Notificações','Acompanhe ocorrências e mantenha um histórico das ações.',[button('Configurar limites',()=>nav('configuracoes'),'secondary','settings')]),el('section',{class:'panel'},filters),body);
  function draw(data){const counts=data.contagens || {};updateNotifications(counts.ABERTO);body.replaceChildren(el('div',{class:'metric-grid'},[['CONSUMO_ELEVADO','Consumo elevado','drop',''],['POSSIVEL_VAZAMENTO','Possível vazamento','alert','orange'],['QUALIDADE','Qualidade da água','shield','purple'],['OUTROS','Outros avisos','bell','green']].map(([id,label,symbol,color])=>{const card=metric(label,number(counts[id] ?? 0),null,'Ver ocorrências deste tipo',symbol,color);const b=el('button',{type:'button',class:'metric-card alert-metric',onclick:()=>{type=id;typeField.input.value=id;refresh();},'aria-label':`${label}: ${number(counts[id]??0)} alertas. Filtrar.`});b.append(...card.childNodes);return b;})),el('section',{class:'panel'},el('div',{class:'panel-head'},el('div',{class:'panel-title'},el('h2',{},'Ocorrências registradas'),el('p',{},'Atualizações de status permanecem no histórico')),el('div',{class:'status-counts'},['ABERTO','EM_ANALISE','RESOLVIDO'].map(id=>el('span',{class:'badge'},`${statusLabels[id]}: ${number(counts[id]??0)}`)))),el('div',{class:'panel-spacer'}),alertsTable(list(data),()=>refresh(range,page)),pagination(data,p=>refresh(range,p))),note('Possíveis vazamentos são indícios calculados quando a frequência das leituras permite. Alertas de qualidade usam os limites configurados na sua conta.'));}
  await refresh();return root;
}

/* CSV evita fórmulas de planilha, mantém Unicode e protege delimitadores. */
function csvCell(value) { let text=String(value ?? ''); if (/^[\s\u0000-\u001f]*[=+@-]/.test(text)) text=`'${text}`; return `"${text.replaceAll('"','""')}"`; }
function downloadCSV(name,columns,rows) { try { const content='\uFEFF'+[columns,...rows].map(row=>row.map(csvCell).join(';')).join('\r\n')+'\r\n';const blob=new Blob([content],{type:'text/csv;charset=utf-8'}),url=URL.createObjectURL(blob),link=el('a',{href:url,download:`aquasense-${name}-${localDate()}.csv`});document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),2000);toast('CSV exportado com os dados selecionados.'); } catch(error) {toast('Não foi possível gerar o CSV. Tente novamente.',true);} }
async function collectPages(path,query = {}) { const all=[];let page=1;while(true){const data=await api(path,{query:{...query,page,page_size:100}});all.push(...list(data));if(page>=(Number(data.total_pages)||1))break;if(page>=1000)throw new Error('O período tem muitos registros. Selecione um intervalo menor para exportar.');page++;}return all; }
function exportReadings(type,items) { if(type==='qualidade')downloadCSV('qualidade',['ID','Data UTC confirmada','Horário original','Temperatura °C','pH','Turbidez NTU','Origem','Origem do vínculo'],items.map(r=>[r.id,r.registrado_em_utc,r.registrado_em_original || r.registrado_em,r.temperatura_c,r.ph,r.turbidez_ntu,r.origem,r.origem_vinculo]));else downloadCSV('consumo',['ID','Data UTC','Hidrômetro','Medidor ID','Leitura acumulada m³','Consumo litros','Leitura anterior UTC','Origem','Observações','Tarifa por m³ R$','Vigência da tarifa UTC','Estimativa do intervalo R$'],items.map(r=>[r.id,r.registrado_em_utc,r.medidor_nome,r.medidor_id,r.acumulado_m3,r.consumo_litros,r.anterior_em_utc,r.origem,r.observacoes,r.tarifa_valor_m3,r.tarifa_inicio_utc,r.estimativa_reais])); }
function exportReport(type,data,range) {
  const summary=data.resumo || {}, rows=[['Tipo de relatório',type],['Período inicial UTC',range.inicio],['Período final UTC exclusivo',range.fim],['Responsável',state.user.nome],['Consumo total litros',summary.total_litros],['Média diária litros',summary.media_diaria_litros],['Estimativa financeira R$',summary.estimativa_reais],['Estimativa do período anterior R$',data.comparacao?.estimativa_reais_anterior],['Variação financeira percentual',data.comparacao?.variacao_financeira_percentual],['Situação da estimativa',finite(summary.intervalos_sem_tarifa)>0?'Cobertura incompleta':decimalParts(summary.estimativa_reais)===null?'Sem dados':'Calculada'],['Base da estimativa',data.base_estimativa],['Intervalos sem tarifa',summary.intervalos_sem_tarifa],['Tarifas distintas aplicadas',summary.tarifas_distintas],['Tarifa única por m³ R$',summary.tarifa_valor_m3],['Vigência da tarifa única UTC',summary.tarifa_inicio_utc],[],['SÉRIE','Período','Consumo litros','Intervalos']];
  for(const row of list(data.serie))rows.push(['Série',row.periodo,row.total_litros,row.intervalos]);
  const tariffs=list(data.tarifas_aplicadas);if(tariffs.length){rows.push([],['TARIFAS APLICADAS','ID','Vigência UTC','Valor por m³ R$','Intervalos','Volume litros','Estimativa parcial R$']);for(const rate of tariffs)rows.push(['Tarifa',rate.id,rate.inicio_utc,rate.valor_m3,rate.intervalos,rate.litros,rate.estimativa_reais]);}
  const consumption=list(data.leituras || data.consumo?.leituras);
  if(consumption.length){rows.push([],['CONSUMO','ID','Data UTC','Hidrômetro','Acumulado m³','Consumo litros','Origem','Observações','Tarifa por m³ R$','Vigência da tarifa UTC','Estimativa do intervalo R$']);for(const r of consumption)rows.push(['Consumo',r.id,r.registrado_em_utc,r.medidor_nome,r.acumulado_m3,r.consumo_litros,r.origem,r.observacoes,r.tarifa_valor_m3,r.tarifa_inicio_utc,r.estimativa_reais]);}
  const quality=list(data.qualidade || data.leituras_qualidade);
  if(quality.length){rows.push([],['QUALIDADE','ID','Data UTC confirmada','Horário original','Temperatura °C','pH','Turbidez NTU','Origem']);for(const r of quality)rows.push(['Qualidade',r.id,r.registrado_em_utc,r.registrado_em_original || r.registrado_em,r.temperatura_c,r.ph,r.turbidez_ntu,r.origem]);}
  const alerts=list(data.alertas || (type==='alertas'?data:null));
  if(alerts.length){rows.push([],['ALERTAS','ID','Data UTC','Tipo','Descrição','Severidade','Status']);for(const r of alerts)rows.push(['Alerta',r.id,r.criado_em_utc,r.tipo,r.descricao,r.severidade,r.status]);}
  const meters=list(data.medidores);
  if(meters.length){rows.push([],['HIDRÔMETROS','ID','Nome','Código','Instalação UTC','Status']);for(const r of meters)rows.push(['Hidrômetro',r.id,r.nome,r.codigo,r.instalado_em_utc,r.status]);}
  downloadCSV(`relatorio-${type}`,['Campo / seção','Valor / identificação','Data / volume','Detalhe 1','Detalhe 2','Detalhe 3','Detalhe 4','Detalhe 5','Detalhe 6','Detalhe 7','Detalhe 8'],rows);
}
function financialNotes(data) {
  const summary=data.resumo||{},missing=finite(summary.intervalos_sem_tarifa)||0;
  const parts=[note(data.base_estimativa || 'A estimativa soma o custo de cada intervalo pela tarifa vigente na sua leitura final. Não inclui outras cobranças da concessionária.')];
  if(missing>0)parts.unshift(note(`Cobertura incompleta: ${number(missing)} ${missing===1?'intervalo não possui':'intervalos não possuem'} tarifa vigente na leitura final. A estimativa total permanece indisponível; os valores abaixo representam somente os intervalos cobertos.`));
  else if(decimalParts(summary.total_litros)===null)parts.unshift(note('Sem consumo calculado no período. Registre duas leituras válidas de um hidrômetro para gerar a estimativa.'));
  return el('div',{},parts);
}
function financialBreakdown(data) {
  const rates=list(data.tarifas_aplicadas);
  return panel('Tarifas aplicadas','Vigências usadas nas leituras finais de cada intervalo',table(['Vigência','Tarifa por m³','Intervalos',`Volume (${volumeUnit()})`,'Estimativa parcial'],rates,rate=>[date(rate.inicio_utc),money(rate.valor_m3,4),number(rate.intervalos),displayVolume(rate.litros),money(rate.estimativa_reais)],'Nenhum intervalo do período possui tarifa aplicada.'));
}
async function reportsPage() {
  let type='consumo',generated=null,currentRange=null,requestId=0;const root=el('div'),body=el('div'),types=el('div',{class:'report-selection no-print'});
  const summaries={consumo:['Consumo de água','Volume total, médias e evolução.','drop'],financeiro:['Financeiro','Custo estimado e tarifa vigente.','wallet'],alertas:['Alertas','Ocorrências, severidade e status.','bell'],detalhado:['Detalhado','Consumo, qualidade e histórico.','report']};
  const start=field('Data inicial','inicio',{type:'date',required:true,value:localDate(rangeDefault('30d').inicio)}),end=field('Data final','fim',{type:'date',required:true,value:localDate()}),group=field('Agrupar por','agrupamento',{choices:[['dia','Dia'],['mes','Mês']],value:'dia'});
  const form=el('form',{class:'filter-bar'},start.row,end.row,group.row,el('button',{type:'submit',class:'btn btn-primary'},icon('report'),'Gerar relatório'));
  const exportButton=button('Exportar CSV',()=>{if(generated)exportReport(type,generated,currentRange);},'secondary','download',{disabled:true}),printButton=button('Imprimir / PDF',()=>{if(generated)window.print();},'secondary','print',{disabled:true});
  const refresh=async()=>{const range={inicio:dayUTC(start.input.value),fim:dayUTC(end.input.value,true),agrupamento:group.input.value};if(new Date(range.inicio)>=new Date(range.fim))throw new Error('A data final precisa ser igual ou posterior à data inicial.');const ticket=++requestId,requestedType=type;generated=null;exportButton.disabled=printButton.disabled=true;body.replaceChildren(loading('Gerando relatório com os dados do período…'));try{const result=await api(`/relatorios/${requestedType}`,{query:range});if(ticket!==requestId||requestedType!==type)return;generated=result;currentRange=range;draw();exportButton.disabled=printButton.disabled=false;announce('Relatório gerado. CSV e impressão estão disponíveis.');}catch(error){if(ticket!==requestId)return;body.replaceChildren(errorBlock(error,refresh));throw error;}};
  wireForm(form,refresh);
  Object.entries(summaries).forEach(([id,[title,description,symbol]])=>{const b=el('button',{type:'button',class:`report-type ${id===type?'selected':''}`,'aria-pressed':id===type,onclick:()=>{type=id;requestId++;generated=null;currentRange=null;exportButton.disabled=printButton.disabled=true;for(const item of types.children){const active=item.dataset.type===type;item.classList.toggle('selected',active);item.setAttribute('aria-pressed',String(active));}body.replaceChildren(empty('Pronto para gerar','Escolha um período e gere o relatório com os seus dados.','report'));}},el('span',{class:'metric-icon'},icon(symbol)),el('strong',{},title),el('p',{},description));b.dataset.type=id;types.append(b);});
  root.append(pageHead('Relatórios','Transforme seus registros em uma visão completa do período.',[exportButton,printButton]),types,el('section',{class:'panel no-print'},form),body);
  function draw(){const data=generated,summary=data.resumo || {},alerts=list(data.alertas || (type==='alertas'?data:null)),quality=list(data.qualidade || data.leituras_qualidade),readings=list(data.leituras || data.consumo?.leituras);body.replaceChildren(el('div',{class:'report-print-head'},el('h2',{},`AquaSense · ${summaries[type][0]}`),el('p',{},`Responsável: ${state.user.nome}`),el('p',{},`Período: ${date(currentRange.inicio,false)} a ${date(end.input.value+'T12:00:00',false)} · emitido em ${date(new Date().toISOString())}`)),panel('Resumo do relatório',`${date(currentRange.inicio,false)} até ${date(end.input.value+'T12:00:00',false)}`,type==='alertas'?el('div',{},el('div',{class:'metric-grid'},Object.entries(typeLabels).map(([id,label])=>metric(label,number(data.contagens?.[id] ?? data.alertas?.contagens?.[id] ?? alerts.filter(a=>a.tipo===id).length),null,'Ocorrências no período','bell'))),note('Os alertas mantêm o status e a descrição registrados no momento da consulta.')):el('div',{},consumeMetrics(data),(type==='financeiro'||type==='detalhado')&&financialNotes(data))));
    if(type==='financeiro'||type==='detalhado')body.append(financialBreakdown(data));
    if(type!=='alertas')body.append(panel('Evolução do consumo','Volumes calculados a partir de leituras consecutivas',chart(data.serie,'line',currentRange.agrupamento)));
    if(readings.length || type==='consumo' || type==='detalhado')body.append(el('section',{class:'panel'},el('div',{class:'panel-head'},el('div',{class:'panel-title'},el('h2',{},'Leituras de consumo'),el('p',{},`${number(readings.length)} registros no relatório`))),el('div',{class:'panel-spacer'}),consumptionTable(readings)));
    if(type==='detalhado'){body.append(el('section',{class:'panel'},el('div',{class:'panel-head'},el('h2',{},'Medições de qualidade')),el('div',{class:'panel-spacer'}),qualityTable(quality)));const meters=list(data.medidores);body.append(el('section',{class:'panel'},el('div',{class:'panel-head'},el('h2',{},'Hidrômetros')),el('div',{class:'panel-spacer'}),table(['Nome','Código','Instalado em','Status'],meters,m=>[m.nome,m.codigo,date(m.instalado_em_utc),statusBadge(m.status)])));}
    if(type==='alertas'||type==='detalhado')body.append(el('section',{class:'panel'},el('div',{class:'panel-head'},el('h2',{},'Alertas do período')),el('div',{class:'panel-spacer'}),alertsTable(alerts,()=>nav('alertas'))));
    body.append(note(data.nota_atribuicao || 'Os relatórios usam os registros existentes. Valores financeiros são estimativas e os parâmetros de qualidade não certificam potabilidade.'));
  }
  body.append(empty('Pronto para gerar','Selecione um tipo de relatório e informe o período.','report'));return root;
}
function readLimitPreferences(data, definitions) {
  const preferences={},decimalKeys=['limite_diario_litros','limite_mensal_litros','vazamento_litros_minuto'];
  for(const [,id] of definitions){
    const raw=String(data.get(id) ?? '').trim();
    if(raw===''){preferences[id]=null;continue;}
    if(decimalKeys.includes(id)){
      const parts=decimalParts(raw);
      if(!parts || parts.coefficient<=0n)throw new Error('Informe limites de consumo positivos ou deixe o campo vazio.');
      preferences[id]=raw; // O C valida precisão/faixa; nunca converter DECIMAL a double.
    }else{
      const value=Number(raw);
      if(!Number.isFinite(value))throw new Error('Informe valores numéricos válidos.');
      preferences[id]=value;
    }
  }
  for(const [min,max] of [['ph_min','ph_max'],['temperatura_min','temperatura_max']]){
    if(preferences[min]!==null && preferences[max]!==null && preferences[min]>preferences[max])throw new Error('O limite mínimo não pode ser maior que o máximo.');
  }
  return preferences;
}
async function settingsPage() {
  const config=await api('/configuracoes');state.preferences=config.preferencias || {};state.meterCache=config.medidores || [];state.user=config.usuario || state.user;updateProfile();applyTheme();const root=el('div'),grid=el('div',{class:'settings-grid'});
  root.append(pageHead('Configurações','Personalize seu monitoramento, seus hidrômetros e suas preferências.'),grid);
  const name=field('Nome completo','nome',{required:true,maxLength:100,value:state.user.nome,autoComplete:'name'}),email=field('E-mail','email',{type:'email',required:true,maxLength:254,value:state.user.email,autoComplete:'email'});
  const profileForm=el('form',{},el('div',{class:'form-grid'},name.row,email.row),el('div',{class:'form-actions'},el('button',{type:'submit',class:'btn btn-primary'},icon('check'),'Salvar perfil')));
  wireForm(profileForm,async data=>{await api('/configuracoes',{method:'PATCH',body:{usuario:{nome:data.get('nome').trim(),email:data.get('email').trim()}}});state.user.nome=data.get('nome').trim();state.user.email=data.get('email').trim();updateProfile();toast('Perfil atualizado.');});
  grid.append(panel('Meu perfil','Dados da sua conta',profileForm));
  const unit=field('Unidade de exibição','unidade',{choices:[['litros','Litros (L)'],['m3','Metros cúbicos (m³)']],value:state.preferences.unidade || 'litros'}),theme=field('Aparência','tema',{choices:[['claro','Tema claro'],['escuro','Tema escuro']],value:state.preferences.tema || 'claro'});
  const visualForm=el('form',{},el('div',{class:'form-grid'},unit.row,theme.row),el('label',{class:'checkbox-row margin-top'},el('input',{type:'checkbox',name:'notificacoes',checked:!!state.preferences.notificacoes}),el('span',{},'Destacar notificações na aplicação',el('small',{},'Mostra o indicador de alertas abertos. Os registros continuam disponíveis no histórico.'))),el('div',{class:'form-actions'},el('button',{type:'submit',class:'btn btn-primary'},icon('check'),'Salvar preferências')));
  wireForm(visualForm,async data=>{const preferences={unidade:data.get('unidade'),tema:data.get('tema'),notificacoes:data.has('notificacoes')};await api('/configuracoes',{method:'PATCH',body:{preferencias:preferences}});Object.assign(state.preferences,preferences);applyTheme();updateNotifications();toast('Preferências salvas.');});grid.append(panel('Preferências','Unidade, aparência e acompanhamento',visualForm));
  const definitions=[['Limite diário (L)','limite_diario_litros',0,null],['Limite mensal (L)','limite_mensal_litros',0,null],['Indício contínuo (L/min)','vazamento_litros_minuto',0,null],['pH mínimo','ph_min',0,14],['pH máximo','ph_max',0,14],['Temperatura mínima (°C)','temperatura_min',-20,100],['Temperatura máxima (°C)','temperatura_max',-20,100],['Turbidez máxima (NTU)','turbidez_max',0,null]];
  const limitFields=definitions.map(([label,id,min,max],index)=>field(label,id,{type:'number',step:index<2?'.001':index===2?'.0001':'any',min:index<2?'.001':index===2?'.0001':min,max,value:state.preferences[id],placeholder:'Não configurado'}));
  const limitForm=el('form',{},note('Deixe vazio para não configurar um limite. As referências servem para acompanhar medições e não definem potabilidade.'),el('div',{class:'form-grid'},limitFields.map(f=>f.row)),el('div',{class:'form-actions'},el('button',{type:'submit',class:'btn btn-primary'},icon('check'),'Salvar limites')));
  wireForm(limitForm,async data=>{const preferences=readLimitPreferences(data,definitions);await api('/configuracoes',{method:'PATCH',body:{preferencias:preferences}});Object.assign(state.preferences,preferences);toast('Limites de acompanhamento salvos.');});grid.append(panel('Limites e parâmetros de alerta','Configurações independentes para consumo e qualidade',limitForm));
  const tariffValue=field('Valor da água por m³ (R$)','valor_m3',{type:'number',required:true,min:0,step:'.0001',placeholder:'Ex.: 5,5000'}),tariffStart=field('Início da vigência','inicio_utc',{type:'datetime-local',required:true,value:localInput(),hint:'Horário local, convertido para UTC.'});
  const tariffs=list(config.tarifas),tariffTable=table(['Vigência','Valor por m³'],tariffs,row=>[date(row.inicio_utc),money(row.valor_m3,4)],'Nenhuma tarifa configurada.');
  const tariffForm=el('form',{},el('div',{class:'form-grid'},tariffValue.row,tariffStart.row),el('div',{class:'form-actions'},el('button',{type:'submit',class:'btn btn-primary'},icon('plus'),'Adicionar tarifa')));
  wireForm(tariffForm,async data=>{await api('/tarifas',{method:'POST',body:{valor_m3:data.get('valor_m3'),inicio_utc:utc(data.get('inicio_utc'))}});toast('Nova tarifa registrada. O histórico foi preservado.');await renderPage('configuracoes');});grid.append(panel('Tarifas de água','Histórico das vigências para estimativas de custo',el('div',{},note('A estimativa soma o custo de cada intervalo pela tarifa vigente na sua leitura final. Não inclui outras cobranças da concessionária.'),tariffForm,el('div',{class:'divider'}),tariffTable)));
  const meterPanel=el('section',{class:'panel'},el('div',{class:'panel-head'},el('div',{class:'panel-title'},el('h2',{},'Meus hidrômetros'),el('p',{},'Sequências independentes de leituras acumuladas em m³')),button('Cadastrar',()=>openMeterModal(()=>renderPage('configuracoes')),'primary','plus')),el('div',{class:'panel-spacer'}),table(['Nome / código','Instalado em','Status','Ações'],state.meterCache,m=>[el('span',{},el('span',{class:'table-main'},m.nome),el('span',{class:'table-secondary'},m.codigo)),date(m.instalado_em_utc),statusBadge(m.status),el('div',{class:'table-actions'},m.status==='ATIVO'&&button('Substituir',()=>openMeterModal(()=>renderPage('configuracoes'),m),'quiet',null,{class:'btn btn-quiet btn-small'}),button(m.status==='ATIVO'?'Desativar':'Ativar',()=>{const next=m.status==='ATIVO'?'INATIVO':'ATIVO';modal(next==='INATIVO'?'Desativar hidrômetro':'Ativar hidrômetro',`${m.nome} · ${m.codigo}. O histórico de leituras será preservado.`,close=>{const form=el('form',{},el('div',{class:'form-actions'},button('Cancelar',close),el('button',{type:'submit',class:`btn btn-${next==='INATIVO'?'danger':'primary'}`},next==='INATIVO'?'Desativar':'Ativar')));return wireForm(form,async()=>{await api(`/medidores/${m.id}`,{method:'PATCH',body:{status:next}});state.meterCache=[];close();toast('Status do hidrômetro atualizado.');await renderPage('configuracoes');});});},'secondary',null,{class:'btn btn-secondary btn-small'}))],'Cadastre um hidrômetro para começar a registrar o consumo.'));
  root.append(meterPanel);
  if(state.user.administrador){root.append(panel('Leituras anteriores do terminal','Vínculo explícito de medições legadas sem proprietário',el('div',{},el('p',{class:'small-text'},'As medições originais são preservadas. Vincule apenas os registros que pertencem à sua conta.'),button('Vincular leituras disponíveis',()=>modal('Vincular leituras anteriores','As medições do terminal ainda sem proprietário serão associadas à sua conta. Nenhum valor ou horário original será alterado.',close=>{const form=el('form',{},el('label',{class:'checkbox-row'},el('input',{type:'checkbox',required:true,name:'confirmar'}),el('span',{},'Confirmo que estes registros pertencem à minha conta.')),el('div',{class:'form-actions'},button('Cancelar',close),el('button',{type:'submit',class:'btn btn-primary'},icon('check'),'Vincular registros')));return wireForm(form,async()=>{const data=await api('/qualidade/importar-legado',{method:'POST',body:{confirmar:true}});close();toast(`${number(pick(data,'importadas','vinculadas','quantidade','total') ?? 0)} medições vinculadas à conta.`);});}),'secondary','history',{class:'btn btn-secondary margin-top'}))));}
  return root;
}
async function renderPage(id) {
  if(!state.user)return;state.page=id;const ticket=++state.loadId;const title=routes.find(route=>route.id===id)?.title || 'Início';$('#breadcrumb-title').textContent=title;document.title=`${title} · AquaSense`;
  for(const link of $('#navigation').children){const active=link.dataset.route===id;link.classList.toggle('active',active);if(active)link.setAttribute('aria-current','page');else link.removeAttribute('aria-current');}
  const content=$('#conteudo');content.setAttribute('aria-busy','true');content.replaceChildren(loading());
  try{const pages={inicio:dashboardPage,consumo:consumptionPage,historico:historyPage,relatorios:reportsPage,alertas:alertsPage,configuracoes:settingsPage};const view=await pages[id]();if(ticket!==state.loadId||!state.user)return;content.replaceChildren(view);content.setAttribute('aria-busy','false');announce(`${title} carregado.`);}catch(error){if(ticket!==state.loadId||!state.user)return;content.replaceChildren(pageHead(title,'Os dados não puderam ser carregados.'),errorBlock(error,()=>renderPage(id)));content.setAttribute('aria-busy','false');}
}
async function boot() {
  for(const route of routes){const link=el('a',{class:'nav-link',href:`#${route.id}`,onclick:closeMenu},icon(route.icon),route.title);link.dataset.route=route.id;$('#navigation').append(link);}
  $('#logout').prepend(icon('logout'));$('#mobile-menu').append(icon('menu'));$('#notifications-link').append(icon('bell'),el('span',{id:'notification-count',class:'notification-count',hidden:true,'aria-hidden':true}));
  $('#mobile-menu').addEventListener('click',()=>{const open=$('#sidebar').classList.toggle('open');$('#menu-shade').hidden=!open;$('#mobile-menu').setAttribute('aria-expanded',String(open));syncSidebar();if(open)$('a.nav-link',$('#sidebar')).focus();});
  $('#menu-shade').addEventListener('click',closeMenu);document.addEventListener('keydown',event=>{if(event.key==='Escape'&&$('#sidebar').classList.contains('open')){closeMenu();$('#mobile-menu').focus();}});
  $('#profile-link').addEventListener('click',()=>nav('configuracoes'));$('#notifications-link').addEventListener('click',()=>nav('alertas'));
  $('#logout').addEventListener('click',async()=>{try{await api('/auth/logout',{method:'POST',body:{}});state.user=null;state.csrf=null;state.preferences={};state.meterCache=[];state.alertCount=null;updateNotifications();state.loadId++;showAuth();toast('Sessão encerrada.');}catch(error){toast(safeMessage(error),true);}});
  window.addEventListener('hashchange',routeFromHash);window.matchMedia('(max-width:760px)').addEventListener('change',closeMenu);syncSidebar();
  $('#auth-root').append(loading('Conectando ao AquaSense…'));
  const results=await Promise.allSettled([api('/auth/me'),api('/auth/setup')]);
  if(results[1].status==='fulfilled')state.setup=results[1].value;
  if(results[0].status==='fulfilled'){try{authSession(results[0].value);await openApplication();}catch(error){showAuth();toast(safeMessage(error),true);}}
  else{showAuth();if(results[0].reason.status!==401)toast(safeMessage(results[0].reason),true);}
}
boot();
