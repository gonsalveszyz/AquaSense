#!/usr/bin/env node
/* Execute os formatadores reais da UI sem navegador, servidor ou dependências. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const source = fs.readFileSync(path.join(root, 'frontend/app.js'), 'utf8');
assert.match(source, /\nboot\(\);\s*$/, 'A inicialização de DOM deve permanecer explícita.');
const context = vm.createContext({ Intl });
vm.runInContext(source.replace(/\nboot\(\);\s*$/, '\n'), context, { filename: 'frontend/app.js' });
const ui = vm.runInContext('({ number, money, decimalShift, decimalSum, decimalPercentage, displayVolume, csvCell, exportReadings, exportReport, consumeMetrics, readLimitPreferences, state })', context);
let checks = 0;
function equal(actual, expected, description) {
  assert.equal(actual, expected, description);
  checks++;
}
const reais = 'R$\u00a0';

// Duas leituras no limite DECIMAL(18,3), separadas por exatamente um litro.
equal(ui.number('999999999999999.999', 3), '999.999.999.999.999,999', 'Contador máximo exato');
equal(ui.number('999999999999999.998', 3), '999.999.999.999.999,998', 'Leitura anterior conserva diferença de 1 L');
equal(ui.number('999999999999999.999', 0), '1.000.000.000.000.000', 'Transporte após arredondar a unidade');
equal(ui.number('12345678901234567890123.4567890', 3), '12.345.678.901.234.567.890.123,457', 'DECIMAL agregado maior que binary64');
equal(ui.number('9007199254740993', 0), '9.007.199.254.740.993', 'Inteiro além do limite seguro de Number');

// Arredondamento decimal de meio centavo e transporte sem erro binário.
equal(ui.money('999999999999999.994'), reais+'999.999.999.999.999,99', 'Centavo inferior no máximo');
equal(ui.money('999999999999999.995'), reais+'1.000.000.000.000.000,00', 'Transporte de centavos no máximo');
equal(ui.money('-999999999999999.995'), '-'+reais+'1.000.000.000.000.000,00', 'Transporte negativo');
equal(ui.money('10000000000000000.01'), reais+'10.000.000.000.000.000,01', 'Centavo além do inteiro seguro');
equal(ui.money('12345678901234567890123.4567890'), reais+'12.345.678.901.234.567.890.123,46', 'Custo DECIMAL(30,7)');
equal(ui.money('0.0050000'), reais+'0,01', 'Meio centavo arredonda para cima');
equal(ui.money('-0.0050000'), '-'+reais+'0,01', 'Meio centavo negativo arredonda afastando de zero');
equal(ui.money('0.0049999'), reais+'0,00', 'Custo abaixo de meio centavo');
equal(ui.money('1234.9950000'), reais+'1.235,00', 'Transporte no preço normal');
equal(ui.money('5.5000'), reais+'5,50', 'Custo normal com duas casas');
equal(ui.money('0.0055', 4), reais+'0,0055', 'Tarifa conserva as quatro casas de sua unidade');
equal(ui.money('99999999.9999', 4), reais+'99.999.999,9999', 'Tarifa DECIMAL(12,4) máxima');
equal(ui.money(-12.5), '-'+reais+'12,50', 'Valor numérico negativo');

// Separadores pt-BR, parâmetros FLOAT de qualidade e números científicos reais.
equal(ui.number(-12.345, 2), '-12,35', 'Temperatura negativa');
equal(ui.number('-19.995', 2), '-20', 'Transporte em temperatura negativa');
equal(ui.number('1.005', 2), '1,01', 'Arredondamento evita o erro clássico de 1.005');
equal(ui.number('1234.500', 3), '1.234,5', 'Casas máximas não inventam zeros adicionais');
equal(ui.number(7.2, 2), '7,2', 'pH normal');
equal(ui.number(1e-7, 8), '0,0000001', 'FLOAT pequeno em notação científica');
equal(ui.number(-1e-7, 8), '-0,0000001', 'FLOAT negativo pequeno');
equal(ui.number(1e21, 0), '1.000.000.000.000.000.000.000', 'FLOAT finito em notação científica grande');

for (const value of [null, undefined, '', NaN, Infinity, -Infinity, 'NaN', '1,2', 'texto', true]) {
  equal(ui.number(value, 3), 'Sem dados', `Número ausente ou inválido: ${String(value)}`);
  equal(ui.money(value), 'Sem tarifa', `Dinheiro ausente ou inválido: ${String(value)}`);
}
equal(ui.number(0, 3), '0', 'Zero numérico é dado');
equal(ui.number('0.000', 3), '0', 'Zero decimal é dado');
equal(ui.money('0.0000'), reais+'0,00', 'Tarifa zero válida');
equal(ui.money(0), reais+'0,00', 'Custo zero válido');
equal(ui.number('-0.000', 3), '0', 'Zero negativo normalizado');

// A divisão por mil e a soma precisam preservar o litro após uma base enorme.
equal(ui.decimalShift('1000000000000000001.000', 3), '1000000000000000.001000', 'Divisão exata L para m³');
equal(ui.decimalSum(['999999999999999999.999', '0.001']), '1000000000000000000.000', 'Soma exata com transporte');
equal(ui.decimalSum(['-2.500', '1.25']), '-1.250', 'Soma decimal negativa');
equal(ui.decimalPercentage('1.005', '100.0', 2), '1.01', 'Percentual com empate decimal exato');
equal(ui.decimalPercentage('1000000000000000001', '2000000000000000002', 1), '50.0', 'Percentual de volumes grandes');
ui.state.preferences = { unidade: 'litros' };
equal(ui.displayVolume('1000000000000000001.000'), '1.000.000.000.000.000.001 L', 'Exibição exata em litros');
ui.state.preferences.unidade = 'm3';
equal(ui.displayVolume('1000000000000000001.000'), '1.000.000.000.000.000,001 m³', 'Exibição exata do litro adicional em m³');
equal(ui.displayVolume('1000000000000000000.000'), '1.000.000.000.000.000 m³', 'Volume anterior continua diferente');
equal(ui.displayVolume('0.000'), '0 m³', 'Volume zero');
equal(ui.displayVolume(null), 'Sem dados', 'Volume ausente');

// Rejeitar conversão de strings por Number comprova o caminho exato dos cards.
context.Number = new Proxy(Number, {
  apply(target, receiver, args) {
    assert.notEqual(typeof args[0], 'string', 'DECIMAL não deve passar pelo construtor Number.');
    return Reflect.apply(target, receiver, args);
  }
});
vm.runInContext('metric=(label,value,unit,hint)=>({label,value,unit,hint}); el=(tag,attrs,...children)=>({tag,attrs,children});', context);
const fixture = {
  resumo: {
    total_litros:'1000000000000000001.000', media_diaria_litros:'1000000000000000001.000',
    estimativa_reais:'12345678901234567890123.4567890', tarifa_valor_m3:'5.5000',
    dias_observados:'1.000000', intervalos_sem_tarifa:0, tarifas_distintas:1
  }, comparacao:{variacao_percentual:'0.5',variacao_financeira_percentual:'0.25'}
};
const cards = ui.consumeMetrics(fixture, false).children[0];
equal(cards[0].value, '1.000.000.000.000.000,001', 'Card conserva o volume original');
equal(cards[1].value, reais+'12.345.678.901.234.567.890.123,46', 'Card conserva o custo original');
equal(cards[2].value, '1.000.000.000.000.000,001', 'Card conserva a média original');
equal(ui.money('999999999999999.995'), reais+'1.000.000.000.000.000,00', 'Money não usa Number para string');

// O escape CSV trata texto de planilha sem arredondar valores do DTO.
for (const formula of ['=2+2', '+SUM(A1)', '@SUM(A1)', '-2+3', ' \t=CMD()', '\r\n=CMD()']) {
  equal(ui.csvCell(formula), `"'${formula}"`, 'Prefixo contra fórmula de planilha');
}
equal(ui.csvCell('a;"b"\nç'), '"a;""b""\nç"', 'Delimitador, aspas, linha e Unicode');
equal(ui.csvCell('999999999999999.999'), '"999999999999999.999"', 'CSV conserva contador máximo');
equal(ui.csvCell('12345678901234567890123.4567890'), '"12345678901234567890123.4567890"', 'CSV conserva todas as casas do custo');
equal(ui.csvCell(null), '""', 'CSV ausente');
let exported;
context.capture = (name, columns, rows) => { exported={name,columns,rows}; };
vm.runInContext('downloadCSV=(name,columns,rows)=>capture(name,columns,rows);', context);
const reading = {
  id:1, medidor_id:2, medidor_nome:'Hidrômetro de teste', registrado_em_utc:'2026-10-14T10:00:00Z',
  acumulado_m3:'999999999999999.999', consumo_litros:'1.000', origem:'MANUAL',
  tarifa_valor_m3:'5.5000', tarifa_inicio_utc:'2026-10-01T00:00:00Z', estimativa_reais:'0.0055000'
};
ui.exportReadings('consumo', [reading]);
equal(exported.rows[0][4], reading.acumulado_m3, 'Histórico CSV preserva o acumulado');
equal(exported.rows[0][9], reading.tarifa_valor_m3, 'Histórico CSV preserva a tarifa');
equal(exported.rows[0][11], reading.estimativa_reais, 'Histórico CSV preserva sete casas do custo');
ui.state.user={nome:'Teste isolado'};
ui.exportReport('financeiro', {...fixture,leituras:[reading]}, {inicio:'2026-10-01T00:00:00Z',fim:'2026-11-01T00:00:00Z'});
equal(exported.rows.find(row=>row[0]==='Estimativa financeira R$')[1], fixture.resumo.estimativa_reais, 'Relatório CSV mantém a precisão financeira');
equal(exported.rows.find(row=>row[0]==='Consumo')[10], reading.estimativa_reais, 'Relatório CSV mantém a parcela individual');
// PATCH de limites conserva DECIMAL; apenas referências FLOAT viram Number.
const definitionsMatch=source.match(/const definitions=(\[\[.*?\]\]);/);
assert(definitionsMatch, 'Definições dos campos de configuração encontradas');
const definitions=vm.runInContext(definitionsMatch[1], context);
const inputs=new Map(definitions.map(([,id])=>[id,'']));
inputs.set('limite_diario_litros','999999999999999.999');
inputs.set('limite_mensal_litros','999999999999999.998');
inputs.set('vazamento_litros_minuto','99999999.9999');
inputs.set('ph_min','6.5');inputs.set('ph_max','8.5');
inputs.set('temperatura_min','-20');inputs.set('temperatura_max','100');
inputs.set('turbidez_max','0.05');
const floatInputs=new Set(['6.5','8.5','-20','100','0.05']);
context.Number=new Proxy(Number,{apply(target,receiver,args){
  if(typeof args[0]==='string')assert(floatInputs.has(args[0]), 'Somente o campo FLOAT pode converter texto para Number.');
  return Reflect.apply(target,receiver,args);
}});
const changes=ui.readLimitPreferences(inputs,definitions);
equal(changes.limite_diario_litros,'999999999999999.999','PATCH diário preserva DECIMAL(18,3)');
equal(changes.limite_mensal_litros,'999999999999999.998','PATCH mensal preserva diferença de um litro');
equal(changes.vazamento_litros_minuto,'99999999.9999','PATCH do limiar conserva quatro casas');
equal(changes.ph_min,6.5,'PATCH de pH continua FLOAT');
equal(changes.temperatura_min,-20,'PATCH de temperatura negativa continua FLOAT');
const blankChanges=ui.readLimitPreferences(new Map(definitions.map(([,id])=>[id,''])),definitions);
equal(blankChanges.limite_diario_litros,null,'Limite ausente continua null');
equal(blankChanges.ph_max,null,'Referência ausente continua null');
const saved=JSON.parse(JSON.stringify({preferencias:changes}));
equal(saved.preferencias.limite_diario_litros,inputs.get('limite_diario_litros'),'JSON mantém limite como string exata');

console.log(`${checks} verificações de formatos/CSV/limites reais aprovadas (sem DOM/API).`);
