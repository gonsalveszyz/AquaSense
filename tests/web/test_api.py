#!/usr/bin/env python3
"""Real HTTP/SQL Server integration, exclusively MonitoramentoAguaTest:8081.

Run: AGUA_WEB_TEST_URL=http://localhost:8081 python3 tests/web/test_api.py
Creates unique test users and records, preserves them for audit, never runs SQL
or deletes history. Does not accept production port, external hosts, or a health
response from a database other than MonitoramentoAguaTest.
"""
from __future__ import annotations

import decimal
import http.client
import json
import os
import secrets
import socket
import unittest
import urllib.parse

D = decimal.Decimal


class Api:
    def __init__(self, url: str):
        parsed = urllib.parse.urlsplit(url)
        self.host, self.port = parsed.hostname, parsed.port
        self.cookie = ""
        self.csrf = ""
        self.user = None
        self.cookie_attributes = ""

    def call(self, method, path, payload=None, *, csrf=True, headers=None, raw=None):
        body = raw if raw is not None else (
            json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            if payload is not None else None
        )
        request_headers = {"Accept": "application/json"}
        if self.cookie:
            request_headers["Cookie"] = self.cookie
        if body is not None:
            request_headers["Content-Type"] = "application/json"
            request_headers["Origin"] = f"http://{self.host}:{self.port}"
            if csrf and self.csrf:
                request_headers["X-CSRF-Token"] = self.csrf
        if headers:
            request_headers.update(headers)
        conn = http.client.HTTPConnection(self.host, self.port, timeout=40)
        try:
            conn.request(method, path, body=body, headers=request_headers)
            response = conn.getresponse()
            status = response.status
            response_headers = dict(response.getheaders())
            content = response.read(2_000_000)
            parsed = json.loads(content.decode("utf-8"))
            cookie = response.getheader("Set-Cookie")
            if cookie:
                self.cookie_attributes = cookie
                self.cookie = cookie.split(";", 1)[0]
            if status in (200, 201) and isinstance(parsed.get("data"), dict):
                data = parsed["data"]
                if "csrf_token" in data:
                    self.csrf = data["csrf_token"]
                    self.user = data.get("usuario")
            return status, parsed, response_headers
        finally:
            conn.close()

    def raw_request(self, request: bytes) -> int:
        with socket.create_connection((self.host, self.port), timeout=8) as sock:
            sock.sendall(request)
            sock.shutdown(socket.SHUT_WR)
            data = bytearray()
            while b"\r\n" not in data:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                data.extend(chunk)
            return int(bytes(data).split(b" ", 2)[1])


class AquaSenseIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.url = os.environ.get("AGUA_WEB_TEST_URL")
        if not cls.url:
            raise unittest.SkipTest("NÃO EXECUTADO: definir AGUA_WEB_TEST_URL explicitamente para localhost:8081.")
        parsed = urllib.parse.urlsplit(cls.url)
        if (parsed.scheme != "http" or parsed.hostname not in {"localhost", "127.0.0.1"}
                or parsed.port != 8081 or parsed.path not in {"", "/"}
                or parsed.query or parsed.fragment or parsed.username or parsed.password):
            raise RuntimeError("Testes de escrita aceitam apenas http://localhost:8081 ou http://127.0.0.1:8081.")
        cls.public = Api(cls.url)
        status, envelope, _ = cls.public.call("GET", "/api/v1/health")
        if status != 200 or envelope.get("data", {}).get("database") != "MonitoramentoAguaTest":
            raise RuntimeError("NÃO EXECUTADO: API não confirmou MonitoramentoAguaTest; nenhuma conta foi criada.")
        cls.run_id = secrets.token_hex(7)
        cls.password = "AquaSense-Teste-" + secrets.token_hex(10)
        cls.a, cls.b = Api(cls.url), Api(cls.url)
        cls.email_a = f"api-a-{cls.run_id}@example.invalid"
        cls.email_b = f"api-b-{cls.run_id}@example.invalid"
        for api, email in ((cls.a, cls.email_a), (cls.b, cls.email_b)):
            status, reply, _ = api.call("POST", "/api/v1/auth/register", {
                "nome": "Integração AquaSense Água", "email": email,
                "senha": cls.password, "importar_legado": False,
            })
            if status != 201:
                raise RuntimeError(f"Cadastro de fixture falhou com HTTP {status}: {reply.get('error', {}).get('code', 'sem código')}")
        status, reply, _ = cls.a.call("POST", "/api/v1/medidores", {
            "nome": "Hidrômetro base", "codigo": "base-" + cls.run_id,
            "instalado_em_utc": "2026-01-01T00:00:00Z",
        })
        if status != 201:
            raise RuntimeError(f"Fixture medidor falhou com HTTP {status}.")
        cls.meter = reply["data"]["id"]
        cls.readings = []
        for timestamp, value in (("2026-01-01T00:00:00Z", "10.000"),
                                 ("2026-01-01T00:01:00Z", "10.001"),
                                 ("2026-01-01T00:02:00Z", "11.250")):
            status, reply, _ = cls.a.call("POST", "/api/v1/consumo/leituras", {
                "medidor_id": cls.meter, "acumulado_m3": value,
                "registrado_em_utc": timestamp, "observacoes": "Medição de teste real",
            })
            if status != 201:
                raise RuntimeError(f"Fixture leitura falhou com HTTP {status}.")
            cls.readings.append(reply["data"])

    def call(self, api, method, path, payload=None, expected=200, **kwargs):
        status, envelope, headers = api.call(method, path, payload, **kwargs)
        code = (envelope.get("error") or {}).get("code", "sem código")
        self.assertEqual(status, expected, f"{method} {path.split('?')[0]}: HTTP {status}, código {code}")
        self.assertIn("data", envelope)
        self.assertIn("error", envelope)
        if expected < 400:
            self.assertIsNone(envelope["error"])
        else:
            self.assertIsNone(envelope["data"])
        return envelope["data"], headers

    def period(self, **extra):
        values = {"inicio": "2026-01-01T00:00:00Z", "fim": "2026-01-02T00:00:00Z",
                  "agrupamento": "dia", "medidor_id": self.meter}
        values.update(extra)
        return "?" + urllib.parse.urlencode(values)

    def new_meter(self, *, name="Hidrômetro teste", **extra):
        payload = {"nome": name, "codigo": "med-" + secrets.token_hex(5),
                   "instalado_em_utc": "2026-01-01T00:00:00Z"}
        payload.update(extra)
        data, _ = self.call(self.a, "POST", "/api/v1/medidores", payload, 201)
        return data["id"]

    def read(self, meter, value, timestamp, expected=201, **extra):
        payload = {"medidor_id": meter, "acumulado_m3": value,
                   "registrado_em_utc": timestamp, "observacoes": "Teste de hidrômetro"}
        payload.update(extra)
        return self.call(self.a, "POST", "/api/v1/consumo/leituras", payload, expected)[0]

    def test_00_health_session_and_cookie(self):
        data, headers = self.call(self.public, "GET", "/api/v1/health")
        self.assertEqual(data["database"], "MonitoramentoAguaTest")
        self.assertEqual(data["esquema_cli"], 3)
        self.assertEqual(data["esquema_web"], 1)
        data, _ = self.call(self.a, "GET", "/api/v1/auth/me")
        self.assertEqual(data["usuario"]["email"], self.email_a)
        self.assertEqual(len(data["csrf_token"]), 64)
        self.assertIn("HttpOnly", self.a.cookie_attributes)
        self.assertIn("SameSite=Strict", self.a.cookie_attributes)
        self.assertIn("no-store", headers.get("Cache-Control", ""))
        self.call(self.public, "GET", "/api/v1/medidores", expected=401)

    def test_10_first_precision_and_zero(self):
        self.assertIsNone(self.readings[0]["consumo_litros"])
        self.assertEqual(D(self.readings[1]["consumo_litros"]), D("1"))
        self.assertEqual(D(self.readings[2]["consumo_litros"]), D("1249"))
        data, _ = self.call(self.a, "GET", "/api/v1/consumo/resumo" + self.period())
        self.assertEqual(D(data["resumo"]["total_litros"]), D("1250"))
        self.assertEqual(data["resumo"]["intervalos"], 2)
        self.assertEqual(data["serie"][0]["periodo"], "2026-01-01")
        meter = self.new_meter()
        self.assertIsNone(self.read(meter, "0", "2026-02-01T00:00:00Z")["consumo_litros"])
        self.assertEqual(D(self.read(meter, "0", "2026-02-01T00:01:00Z")["consumo_litros"]), 0)

    def test_11_filter_preserves_predecessor_and_exclusive_end(self):
        data, _ = self.call(self.a, "GET", "/api/v1/consumo/resumo" + self.period(
            inicio="2026-01-01T00:01:00Z", fim="2026-01-01T00:02:00Z"))
        self.assertEqual(D(data["resumo"]["total_litros"]), 1)
        self.assertEqual(data["resumo"]["leituras"], 1)
        self.assertEqual(data["resumo"]["intervalos"], 1)
        empty, _ = self.call(self.a, "GET", "/api/v1/consumo/resumo" + self.period(
            inicio="2026-01-02T00:00:00Z", fim="2026-01-03T00:00:00Z"))
        self.assertIsNone(empty["resumo"]["total_litros"])
        self.assertIsNone(empty["resumo"]["media_diaria_litros"])
        self.assertEqual(empty["serie"], [])

    def test_12_duplicate_order_counter_and_validation(self):
        self.read(self.meter, "11.250", "2026-01-01T00:02:00Z", 409)
        self.read(self.meter, "11.250", "2026-01-01T00:01:30Z", 409)
        self.read(self.meter, "11.249", "2026-01-01T00:03:00Z", 409)
        for invalid in ("-1", "1.0001", "1e3", "NaN", "1,0"):
            with self.subTest(value=invalid):
                self.read(self.meter, invalid, "2026-01-01T00:03:00Z", 422)
        self.read(self.meter, "12", "2025-12-31T23:59:00Z", 409)
        self.read(self.meter, "12", "2026-02-30T00:00:00Z", 422)
        self.read(self.meter, "12", "2100-01-01T00:00:00Z", 422)
        self.call(self.a, "POST", "/api/v1/medidores", {"nome": "Futuro", "codigo": "future-" + self.run_id,
                  "instalado_em_utc": "2100-01-01T00:00:00Z"}, 422)
        self.call(self.a, "GET", "/api/v1/consumo/historico?page_size=101", expected=422)
        self.call(self.a, "GET", "/api/v1/consumo/historico?page=1&page=2", expected=422)

    def test_13_pagination_and_order(self):
        ascending, _ = self.call(self.a, "GET", "/api/v1/consumo/historico" + self.period(
            ordem="asc", page=1, page_size=1))
        self.assertEqual(ascending["total"], 3)
        self.assertEqual(ascending["total_pages"], 3)
        self.assertEqual(ascending["items"][0]["id"], self.readings[0]["id"])
        descending, _ = self.call(self.a, "GET", "/api/v1/consumo/historico" + self.period(
            ordem="desc", page=1, page_size=1))
        self.assertEqual(descending["items"][0]["id"], self.readings[2]["id"])

    def test_14_replacement_new_sequence_and_inactive(self):
        old = self.new_meter()
        self.read(old, "100.000", "2026-03-01T00:00:00Z")
        new = self.new_meter(substitui_medidor_id=old)
        self.read(old, "101", "2026-03-01T00:01:00Z", 409)
        first = self.read(new, "0.005", "2026-03-01T00:01:00Z")
        self.assertIsNone(first["consumo_litros"])
        self.assertEqual(D(self.read(new, "0.006", "2026-03-01T00:02:00Z")["consumo_litros"]), 1)

    def test_15_large_counter_keeps_one_litre_exact(self):
        meter = self.new_meter()
        self.read(meter, "999999999999999.998", "2026-03-10T00:00:00Z")
        delta = self.read(meter, "999999999999999.999", "2026-03-10T00:01:00Z")
        self.assertEqual(D(delta["consumo_litros"]), 1)
        self.assertEqual(delta["acumulado_m3"], "999999999999999.999")
        summary, _ = self.call(self.a, "GET", "/api/v1/consumo/resumo" + self.period(
            inicio="2026-03-10T00:00:00Z", fim="2026-03-11T00:00:00Z", medidor_id=meter))
        self.assertEqual(D(summary["resumo"]["total_litros"]), 1)

    def test_20_tariff_absence_validity_and_estimate(self):
        data, _ = self.call(self.a, "GET", "/api/v1/consumo/resumo" + self.period())
        self.assertIsNone(data["resumo"]["estimativa_reais"])
        self.call(self.a, "POST", "/api/v1/tarifas", {"valor_m3": "4.1234", "inicio_utc": "2025-12-01T00:00:00Z"}, 201)
        self.call(self.a, "POST", "/api/v1/tarifas", {"valor_m3": "99.0000", "inicio_utc": "2026-02-01T00:00:00Z"}, 201)
        data, _ = self.call(self.a, "GET", "/api/v1/consumo/resumo" + self.period())
        self.assertEqual(D(data["resumo"]["tarifa_valor_m3"]), D("4.1234"))
        self.assertEqual(D(data["resumo"]["estimativa_reais"]), D("5.15"))
        self.call(self.a, "POST", "/api/v1/tarifas", {"valor_m3": "4", "inicio_utc": "2025-12-01T00:00:00Z"}, 409)
        self.call(self.a, "POST", "/api/v1/tarifas", {"valor_m3": "-1", "inicio_utc": "2025-11-01T00:00:00Z"}, 422)

    def test_21_tariff_changes_inside_period_and_missing_coverage(self):
        meter = self.new_meter()
        self.read(meter, "1", "2025-10-01T00:00:00Z", 409)
        # Separate readings in 2026 avoid assumptions about unrecorded daily volumes.
        self.read(meter, "1", "2026-07-01T00:00:00Z")
        self.read(meter, "2", "2026-07-02T00:00:00Z")
        self.read(meter, "3", "2026-07-03T00:00:00Z")
        self.call(self.a, "POST", "/api/v1/tarifas", {"valor_m3": "2", "inicio_utc": "2026-07-01T00:00:00Z"}, 201)
        self.call(self.a, "POST", "/api/v1/tarifas", {"valor_m3": "3", "inicio_utc": "2026-07-03T00:00:00Z"}, 201)
        result, _ = self.call(self.a, "GET", "/api/v1/consumo/resumo" + self.period(
            inicio="2026-07-01T00:00:00Z", fim="2026-07-04T00:00:00Z", medidor_id=meter))
        self.assertEqual(D(result["resumo"]["estimativa_reais"]), 5)
        self.assertEqual(result["resumo"]["tarifas_distintas"], 2)
        self.assertIsNone(result["resumo"]["tarifa_valor_m3"])
        self.assertEqual(len(result["tarifas_aplicadas"]), 2)
        # A meter owned by B has no tariff, despite A's tariff history.
        other, _ = self.call(self.b, "POST", "/api/v1/medidores", {
            "nome": "Sem tarifa", "codigo": "sem-tarifa-" + self.run_id,
            "instalado_em_utc": "2026-01-01T00:00:00Z"}, 201)
        for timestamp, value in (("2026-07-01T00:00:00Z", "1"), ("2026-07-02T00:00:00Z", "2")):
            self.call(self.b, "POST", "/api/v1/consumo/leituras", {
                "medidor_id": other["id"], "acumulado_m3": value, "registrado_em_utc": timestamp}, 201)
        missing, _ = self.call(self.b, "GET", "/api/v1/consumo/resumo" + self.period(
            inicio="2026-07-01T00:00:00Z", fim="2026-07-03T00:00:00Z", medidor_id=other["id"]))
        self.assertIsNone(missing["resumo"]["estimativa_reais"])
        self.assertEqual(missing["resumo"]["intervalos_sem_tarifa"], 1)

    def test_22_financial_comparison_and_line_prices(self):
        meter = self.new_meter()
        for timestamp, amount in (("2026-09-01T00:00:00Z", "1"),
                                  ("2026-09-02T00:00:00Z", "2"),
                                  ("2026-09-03T00:00:00Z", "4")):
            self.read(meter, amount, timestamp)
        result, _ = self.call(self.a, "GET", "/api/v1/relatorios/financeiro" + self.period(
            inicio="2026-09-03T00:00:00Z", fim="2026-09-04T00:00:00Z", medidor_id=meter))
        self.assertEqual(D(result["resumo"]["estimativa_reais"]), 6)
        self.assertEqual(D(result["comparacao"]["estimativa_reais_anterior"]), 3)
        self.assertEqual(D(result["comparacao"]["variacao_financeira_percentual"]), 100)
        self.assertEqual(D(result["leituras"][0]["tarifa_valor_m3"]), 3)
        self.assertEqual(D(result["leituras"][0]["estimativa_reais"]), 6)

    def test_30_categories_real_volume_and_bound(self):
        cats, _ = self.call(self.a, "GET", "/api/v1/categorias" + self.period())
        self.assertEqual(len(cats["items"]), 5)
        self.assertTrue(all(item["litros"] is None for item in cats["items"]))
        reading = self.readings[2]["id"]
        self.call(self.a, "POST", "/api/v1/categorias/registros", {
            "leitura_id": reading, "categoria_id": 1, "litros": "500.001", "origem": "INFORMADO"}, 201)
        self.call(self.a, "POST", "/api/v1/categorias/registros", {
            "leitura_id": reading, "categoria_id": 2, "litros": "748.999", "origem": "ESTIMADO"}, 201)
        self.call(self.a, "POST", "/api/v1/categorias/registros", {
            "leitura_id": reading, "categoria_id": 3, "litros": "0.001", "origem": "MEDIDO"}, 409)
        self.call(self.a, "POST", "/api/v1/categorias/registros", {
            "leitura_id": self.readings[0]["id"], "categoria_id": 1, "litros": "1", "origem": "INFORMADO"}, 409)
        cats, _ = self.call(self.a, "GET", "/api/v1/categorias" + self.period())
        self.assertEqual(D(cats["items"][0]["litros"]), D("500.001"))
        self.assertEqual({item["origem"] for item in cats["registros"]}, {"INFORMADO", "ESTIMADO"})

    def test_40_quality_atomic_ownership_and_validation(self):
        self.call(self.a, "POST", "/api/v1/qualidade/leituras", {
            "temperatura_c": 101, "ph": 7, "turbidez_ntu": 0}, 422)
        quality, _ = self.call(self.a, "POST", "/api/v1/qualidade/leituras", {
            "temperatura_c": 21.5, "ph": 8.2, "turbidez_ntu": 0.7}, 201)
        self.assertEqual(quality["origem_vinculo"], "WEB")
        self.assertTrue(quality["horario_confirmado_utc"])
        history, _ = self.call(self.a, "GET", "/api/v1/qualidade/leituras")
        self.assertIn(quality["id"], {item["id"] for item in history["items"]})
        other, _ = self.call(self.b, "GET", "/api/v1/qualidade/leituras")
        self.assertNotIn(quality["id"], {item["id"] for item in other["items"]})
        self.call(self.b, "POST", "/api/v1/qualidade/importar-legado", {}, 403)

    def test_50_preferences_unicode_utf16_and_persistence(self):
        name = "Água de São Paulo 💧"
        saved, _ = self.call(self.a, "PATCH", "/api/v1/configuracoes", {
            "usuario": {"nome": name, "email": self.email_a},
            "preferencias": {"limite_diario_litros": "1000", "limite_mensal_litros": "30000",
                             "vazamento_litros_minuto": "10", "ph_min": 6, "ph_max": 8,
                             "temperatura_min": None, "temperatura_max": 30, "turbidez_max": 1,
                             "tema": "escuro", "unidade": "m3", "notificacoes": False},
        })
        self.assertEqual(saved["usuario"]["nome"], name)
        self.assertFalse(saved["preferencias"]["notificacoes"])
        again, _ = self.call(self.a, "GET", "/api/v1/configuracoes")
        self.assertEqual(again["usuario"]["nome"], name)
        self.assertEqual(D(again["preferencias"]["limite_diario_litros"]), 1000)
        self.call(self.a, "PATCH", "/api/v1/configuracoes", {"preferencias": {"ph_min": 9, "ph_max": 8}}, 422)
        self.call(self.a, "PATCH", "/api/v1/configuracoes", {"usuario": {"nome": "💧" * 51}}, 422)
        meter = self.new_meter(name="Medidor Unicode 💧")
        observation = "Observação com água 💧\nSegundo parágrafo."
        reading = self.read(meter, "1", "2026-04-01T00:00:00Z", observacoes=observation)
        self.assertEqual(reading["observacoes"], observation)
        self.read(meter, "2", "2026-04-01T00:01:00Z", 422, observacoes="x" * 1001)
        self.read(meter, "2", "2026-04-01T00:01:00Z", 422, observacoes="💧" * 501)
        self.call(self.a, "POST", "/api/v1/medidores", {
            "nome": "Nome", "codigo": "💧" * 33, "instalado_em_utc": "2026-01-01T00:00:00Z"}, 422)

    def test_60_alerts_frequency_no_duplicate_and_history(self):
        leak_meter = self.new_meter()
        for minute, amount in enumerate(("1.000", "1.011", "1.023", "1.035")):
            self.read(leak_meter, amount, f"2026-05-01T00:0{minute}:00Z")
        alerts, _ = self.call(self.a, "GET", "/api/v1/alertas?page_size=100")
        self.assertGreaterEqual(alerts["contagens"]["CONSUMO_ELEVADO"], 1)
        self.assertGreaterEqual(alerts["contagens"]["POSSIVEL_VAZAMENTO"], 1)
        self.assertGreaterEqual(alerts["contagens"]["QUALIDADE"], 1)
        target = next(item for item in alerts["items"] if item["tipo"] == "POSSIVEL_VAZAMENTO")
        self.assertEqual(target["status"], "ABERTO")
        self.assertIn("Indício", target["descricao"])
        daily = next(item for item in alerts["items"] if item["tipo"] == "CONSUMO_ELEVADO")
        self.assertIn("atribuído", daily["descricao"])
        self.assertNotIn("Ã", target["descricao"])
        self.assertEqual(len(target["historico"]), 1)
        self.call(self.a, "PATCH", f"/api/v1/alertas/{target['id']}", {"status": "EM_ANALISE", "observacao": "Inspeção iniciada"})
        self.call(self.a, "PATCH", f"/api/v1/alertas/{target['id']}", {"status": "RESOLVIDO", "observacao": "Uso da torneira identificado"})
        self.call(self.a, "PATCH", f"/api/v1/alertas/{target['id']}", {"status": "RESOLVIDO"})
        repeat, _ = self.call(self.a, "GET", "/api/v1/alertas?page_size=100")
        self.assertEqual(repeat["total"], alerts["total"])
        resolved = next(item for item in repeat["items"] if item["id"] == target["id"])
        self.assertEqual(resolved["status"], "RESOLVIDO")
        self.assertEqual([item["status_novo"] for item in resolved["historico"]], ["ABERTO", "EM_ANALISE", "RESOLVIDO"])
        self.call(self.a, "PATCH", f"/api/v1/alertas/{target['id']}", {"status": "ABERTO", "observacao": "💧" * 251}, 422)
        sparse = self.new_meter()
        self.read(sparse, "1", "2026-06-01T00:00:00Z")
        self.read(sparse, "100", "2026-06-02T00:00:00Z")
        self.read(sparse, "200", "2026-06-03T00:00:00Z")
        self.read(sparse, "300", "2026-06-04T00:00:00Z")
        after, _ = self.call(self.a, "GET", "/api/v1/alertas?page_size=100")
        self.assertEqual(after["contagens"]["POSSIVEL_VAZAMENTO"], repeat["contagens"]["POSSIVEL_VAZAMENTO"])

    def test_70_idor_cannot_choose_other_owner(self):
        self.call(self.b, "PATCH", f"/api/v1/medidores/{self.meter}", {"status": "INATIVO"}, 404)
        self.call(self.b, "POST", "/api/v1/consumo/leituras", {
            "medidor_id": self.meter, "acumulado_m3": "12", "registrado_em_utc": "2026-01-01T00:03:00Z"}, 404)
        self.call(self.b, "GET", "/api/v1/consumo/historico" + self.period(), expected=404)
        self.call(self.b, "POST", "/api/v1/categorias/registros", {
            "leitura_id": self.readings[2]["id"], "categoria_id": 1, "litros": "1", "origem": "MEDIDO"}, 404)
        alerts, _ = self.call(self.a, "GET", "/api/v1/alertas?page_size=100")
        self.call(self.b, "PATCH", f"/api/v1/alertas/{alerts['items'][0]['id']}", {"status": "RESOLVIDO"}, 404)
        rows, _ = self.call(self.b, "GET", f"/api/v1/medidores?usuario_id={self.a.user['id']}")
        self.assertNotIn(self.meter, {item["id"] for item in rows["items"]})
        settings, _ = self.call(self.b, "PATCH", "/api/v1/configuracoes", {
            "usuario_id": self.a.user["id"], "usuario": {"nome": "Conta B permanece B"}})
        self.assertEqual(settings["usuario"]["id"], self.b.user["id"])

    def test_80_reports_dashboard_and_empty_export_inputs(self):
        for kind in ("consumo", "financeiro", "alertas", "detalhado"):
            with self.subTest(report=kind):
                data, _ = self.call(self.a, "GET", f"/api/v1/relatorios/{kind}" + self.period())
                self.assertEqual(data["tipo"], kind)
                self.assertEqual(D(data["resumo"]["total_litros"]), 1250)
        detailed, _ = self.call(self.a, "GET", "/api/v1/relatorios/detalhado" + self.period(
            inicio="2027-01-01T00:00:00Z", fim="2027-01-02T00:00:00Z"))
        self.assertEqual(detailed["leituras"], [])
        self.assertEqual(detailed["qualidade"], [])
        self.assertEqual(detailed["alertas"], [])
        self.assertIsNone(detailed["resumo"]["total_litros"])
        dashboard, _ = self.call(self.a, "GET", "/api/v1/dashboard" + self.period())
        self.assertEqual(dashboard["qualidade"]["estado"], "FORA_LIMITES_CONFIGURADOS")
        self.assertIn("Não determina potabilidade", dashboard["qualidade"]["nota"])

    def test_90_csrf_origin_host_and_parser(self):
        payload = {"status": "INATIVO"}
        self.call(self.a, "PATCH", f"/api/v1/medidores/{self.meter}", payload, 403, csrf=False)
        self.call(self.a, "PATCH", f"/api/v1/medidores/{self.meter}", payload, 403,
                  headers={"X-CSRF-Token": "0" * 64})
        self.call(self.a, "PATCH", f"/api/v1/medidores/{self.meter}", payload, 403,
                  headers={"Origin": "https://example.invalid"})
        self.call(self.public, "GET", "/api/v1/health", expected=400, headers={"Host": "example.invalid"})
        self.call(self.a, "POST", "/api/v1/medidores", expected=400, raw=b'{"nome":"a","nome":"b"}')
        self.call(self.a, "PATCH", "/api/v1/configuracoes", expected=400,
                  raw=b'{"preferencias":{"tema":"claro","tema":"escuro"}}')
        self.call(self.a, "POST", "/api/v1/medidores", expected=400, raw=b'{"nome":"\xff"}')
        raw = b"POST /api/v1/auth/login HTTP/1.1\r\nHost: localhost:8081\r\nContent-Type: application/json\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}"
        self.assertEqual(self.public.raw_request(raw), 400)
        raw = b"POST /api/v1/auth/login HTTP/1.1\r\nHost: localhost:8081\r\nTransfer-Encoding: chunked\r\nContent-Type: application/json\r\n\r\n0\r\n\r\n"
        self.assertEqual(self.public.raw_request(raw), 400)
        expired = Api(self.url)
        expired.cookie = "aqua_session=" + "0" * 64
        self.call(expired, "GET", "/api/v1/auth/me", expected=401)

    def test_98_login_duplicate_email_and_logout(self):
        client = Api(self.url)
        login, _ = self.call(client, "POST", "/api/v1/auth/login", {
            "email": self.email_b.upper(), "senha": self.password})
        self.assertEqual(login["usuario"]["id"], self.b.user["id"])
        self.call(Api(self.url), "POST", "/api/v1/auth/register", {
            "nome": "Duplicação", "email": self.email_b.upper(), "senha": self.password}, 409)
        revoked_cookie = client.cookie
        self.call(client, "POST", "/api/v1/auth/logout", {})
        client.cookie = revoked_cookie
        self.call(client, "GET", "/api/v1/auth/me", expected=401)

    def test_99_rate_limit_does_not_echo_password(self):
        client = Api(self.url)
        email = f"unknown-{self.run_id}@example.invalid"
        for _ in range(5):
            self.call(client, "POST", "/api/v1/auth/login", {"email": email, "senha": "senha-invalida-teste"}, 401)
        self.call(client, "POST", "/api/v1/auth/login", {"email": email, "senha": "senha-invalida-teste"}, 429)


if __name__ == "__main__":
    unittest.main(verbosity=2)
