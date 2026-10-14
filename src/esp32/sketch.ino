/*
 * ============================================================================
 *  FarmTech Solutions - Fase 2
 *  Sistema de Irrigação Inteligente - Cana-de-açúcar (ESP32 / Wokwi)
 * ============================================================================
 *  Sensores (substituições didáticas do Wokwi):
 *    - Botões verdes N, P e K  -> nível de cada nutriente (presente / ausente)
 *    - LDR (photoresistor)     -> pH do solo (escala 0 a 14)
 *    - DHT22                   -> umidade do solo (%)
 *  Atuador:
 *    - Relé azul               -> bomba d'água (LED azul acende quando liga)
 *  Ir além:
 *    - Previsão de chuva (mm nas próximas 24 h) digitada no Monitor Serial,
 *      obtida pelo script Python (src/python/previsao_chuva.py).
 *
 *  Autor: Tárcio Filipe de Almeida Barros - RM576489
 * ============================================================================
 */

#include <Arduino.h>
#include "DHT.h"

// ---------------------------------------------------------------------------
// Pinos
// ---------------------------------------------------------------------------
const int PINO_BOTAO_N = 13;
const int PINO_BOTAO_P = 14;
const int PINO_BOTAO_K = 27;
const int PINO_LDR     = 34;   // entrada analógica (ADC1)
const int PINO_DHT     = 4;
const int PINO_RELE    = 26;

// ---------------------------------------------------------------------------
// Parâmetros agronômicos da cana-de-açúcar (ver README)
// ---------------------------------------------------------------------------
const float UMIDADE_CRITICA   = 30.0;  // abaixo: irriga sempre (estresse hídrico)
const float UMIDADE_LIGAR     = 60.0;  // abaixo: irrigação desejável
const float UMIDADE_DESLIGAR  = 70.0;  // a partir daqui: solo na capacidade ideal
const float PH_MINIMO         = 5.0;   // faixa aceitável para a cana
const float PH_MAXIMO         = 7.5;
const float CHUVA_SUSPENDE_MM = 5.0;   // chuva prevista que dispensa a irrigação

const unsigned long INTERVALO_LEITURA_MS = 2000;
const unsigned long DEBOUNCE_MS = 200;

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
DHT dht(PINO_DHT, DHT22);

bool nitrogenio = false;   // true = nutriente presente em nível adequado
bool fosforo    = false;
bool potassio   = false;
bool bombaLigada = false;
float chuvaPrevistaMm = 0.0;

unsigned long ultimaLeitura = 0;
unsigned long ultimoToqueN = 0, ultimoToqueP = 0, ultimoToqueK = 0;
bool estadoAnteriorN = HIGH, estadoAnteriorP = HIGH, estadoAnteriorK = HIGH;
String bufferSerial = "";

// ---------------------------------------------------------------------------
// Lógica de decisão (função pura: não lê pinos, só decide)
// ---------------------------------------------------------------------------
enum Motivo {
  M_ERRO_SENSOR,
  M_UMIDADE_CRITICA,
  M_CHUVA_PREVISTA,
  M_PH_INADEQUADO,
  M_FALTA_NUTRIENTE,
  M_SOLO_SECO_NUTRIDO,
  M_SOLO_UMIDO,
  M_HISTERESE
};

struct Decisao {
  bool ligar;
  Motivo motivo;
};

Decisao decidirIrrigacao(float umidade, float ph, bool n, bool p, bool k,
                         float chuvaMm, bool bombaAtual) {
  (void)p;  // fósforo é monitorado e informado, mas não bloqueia a irrigação

  // 1) Falha de leitura: desliga por segurança
  if (isnan(umidade)) return {false, M_ERRO_SENSOR};

  // 2) Umidade crítica: irriga de qualquer forma para salvar a lavoura
  if (umidade < UMIDADE_CRITICA) return {true, M_UMIDADE_CRITICA};

  // 3) Solo já úmido: não precisa irrigar
  if (umidade >= UMIDADE_DESLIGAR) return {false, M_SOLO_UMIDO};

  // 4) Chuva prevista suficiente: economiza água
  if (chuvaMm >= CHUVA_SUSPENDE_MM) return {false, M_CHUVA_PREVISTA};

  // 5) pH fora da faixa: a planta não absorve os nutrientes; corrigir o solo antes
  if (ph < PH_MINIMO || ph > PH_MAXIMO) return {false, M_PH_INADEQUADO};

  // 6) Solo abaixo do ponto de irrigação
  if (umidade < UMIDADE_LIGAR) {
    if (n && k) return {true, M_SOLO_SECO_NUTRIDO};
    return {false, M_FALTA_NUTRIENTE};   // adubar antes de irrigar
  }

  // 7) Entre 60% e 70%: mantém o estado atual (histerese evita liga/desliga)
  return {bombaAtual, M_HISTERESE};
}

const char* textoMotivo(Motivo m) {
  switch (m) {
    case M_ERRO_SENSOR:       return "Falha na leitura do DHT22 - bomba desligada por seguranca";
    case M_UMIDADE_CRITICA:   return "Umidade CRITICA (<30%) - irrigacao de emergencia";
    case M_CHUVA_PREVISTA:    return "Chuva prevista >= 5 mm - irrigacao suspensa para economizar agua";
    case M_PH_INADEQUADO:     return "pH fora da faixa 5,0-7,5 - corrigir o solo (calagem) antes de irrigar";
    case M_FALTA_NUTRIENTE:   return "Solo seco, mas falta N ou K - adubar antes de irrigar";
    case M_SOLO_SECO_NUTRIDO: return "Solo seco (<60%) com N e K adequados - irrigando";
    case M_SOLO_UMIDO:        return "Solo umido (>=70%) - irrigacao desnecessaria";
    case M_HISTERESE:         return "Umidade entre 60% e 70% - mantem o estado atual (histerese)";
  }
  return "";
}

// ---------------------------------------------------------------------------
// Sensores
// ---------------------------------------------------------------------------
float lerPh() {
  // LDR: mais luz -> leitura analógica menor. Invertemos para que
  // aumentar a luz no simulador aumente o pH (escala 0 a 14).
  int bruto = analogRead(PINO_LDR);          // 0 a 4095
  return (4095 - bruto) * 14.0 / 4095.0;
}

// Ao mudar o NPK, o pH do solo também muda. Como não há sensor de pH real,
// o sistema orienta o operador a ajustar o LDR manualmente na direção esperada.
void orientarAjustePh(const char *nome, bool presente) {
  switch (nome[0]) {
    case 'N':
      Serial.println(presente
        ? "   pH: adubo nitrogenado (ureia/amonio) ACIDIFICA o solo -> DIMINUA a luz do LDR (pH cai ~0,5)"
        : "   pH: sem a adubacao nitrogenada o solo acidifica menos -> AUMENTE um pouco a luz do LDR");
      break;
    case 'P':
      Serial.println(presente
        ? "   pH: fosfato acidifica LEVEMENTE o solo -> diminua um pouco a luz do LDR (pH cai ~0,2)"
        : "   pH: retirar o fosfato quase nao altera o pH -> ajuste minimo no LDR");
      break;
    case 'K':
      Serial.println("   pH: cloreto de potassio e praticamente NEUTRO -> o LDR pode ficar como esta");
      break;
  }
}

// Cada toque no botão alterna o nível do nutriente (presente / ausente)
void verificarBotao(int pino, bool &nutriente, bool &estadoAnterior,
                    unsigned long &ultimoToque, const char *nome) {
  bool estado = digitalRead(pino);
  if (estado == LOW && estadoAnterior == HIGH && millis() - ultimoToque > DEBOUNCE_MS) {
    nutriente = !nutriente;
    ultimoToque = millis();
    Serial.printf(">> Nutriente %s: %s\n", nome, nutriente ? "PRESENTE" : "AUSENTE");
    orientarAjustePh(nome, nutriente);
  }
  estadoAnterior = estado;
}

// Lê do Monitor Serial a chuva prevista (ex.: digite 12.5 e Enter)
void lerChuvaSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      bufferSerial.trim();
      if (bufferSerial.length() > 0) {
        bufferSerial.replace(',', '.');
        float valor = bufferSerial.toFloat();
        bool numerico = (valor != 0.0) || bufferSerial.startsWith("0");
        if (numerico && valor >= 0 && valor <= 500) {
          chuvaPrevistaMm = valor;
          Serial.printf(">> Chuva prevista atualizada: %.1f mm nas proximas 24 h\n", chuvaPrevistaMm);
        } else {
          Serial.println(">> Valor invalido. Digite os mm de chuva previstos (ex.: 12.5)");
        }
      }
      bufferSerial = "";
    } else {
      bufferSerial += c;
    }
  }
}

// ---------------------------------------------------------------------------
// Setup e loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PINO_BOTAO_N, INPUT_PULLUP);
  pinMode(PINO_BOTAO_P, INPUT_PULLUP);
  pinMode(PINO_BOTAO_K, INPUT_PULLUP);
  pinMode(PINO_RELE, OUTPUT);
  digitalWrite(PINO_RELE, LOW);
  dht.begin();

  Serial.println("==============================================");
  Serial.println(" FarmTech - Irrigacao Inteligente (Cana)");
  Serial.println("==============================================");
  Serial.println("Botoes verdes: alternam N, P e K (presente/ausente)");
  Serial.println("LDR: ajuste a luz para simular o pH (0 a 14)");
  Serial.println("Ao mudar N, P ou K, siga a orientacao de pH e ajuste o LDR");
  Serial.println("DHT22: ajuste a umidade para simular o solo");
  Serial.println("Monitor Serial: digite a chuva prevista em mm e Enter");
  Serial.println("CSV;tempo_s;umidade;ph;N;P;K;chuva_mm;bomba");
}

void loop() {
  verificarBotao(PINO_BOTAO_N, nitrogenio, estadoAnteriorN, ultimoToqueN, "N");
  verificarBotao(PINO_BOTAO_P, fosforo,    estadoAnteriorP, ultimoToqueP, "P");
  verificarBotao(PINO_BOTAO_K, potassio,   estadoAnteriorK, ultimoToqueK, "K");
  lerChuvaSerial();
  delay(10);  // alivia o processador (e acelera a simulação no Wokwi)

  if (millis() - ultimaLeitura < INTERVALO_LEITURA_MS) return;
  ultimaLeitura = millis();

  float umidade = dht.readHumidity();
  float ph = lerPh();

  Decisao d = decidirIrrigacao(umidade, ph, nitrogenio, fosforo, potassio,
                               chuvaPrevistaMm, bombaLigada);
  bombaLigada = d.ligar;
  digitalWrite(PINO_RELE, bombaLigada ? HIGH : LOW);

  Serial.println("----------------------------------------------");
  Serial.printf("Umidade do solo : %.1f %%\n", umidade);
  Serial.printf("pH do solo      : %.2f\n", ph);
  Serial.printf("N / P / K       : %s / %s / %s\n",
                nitrogenio ? "OK" : "--", fosforo ? "OK" : "--", potassio ? "OK" : "--");
  Serial.printf("Chuva prevista  : %.1f mm\n", chuvaPrevistaMm);
  Serial.printf("BOMBA           : %s\n", bombaLigada ? "LIGADA" : "DESLIGADA");
  Serial.printf("Motivo          : %s\n", textoMotivo(d.motivo));
  if (!fosforo) Serial.println("Aviso           : fosforo ausente - programar adubacao fosfatada");
  // Linha em CSV para análise posterior no R (src/r/analise_irrigacao.R)
  Serial.printf("CSV;%lu;%.1f;%.2f;%d;%d;%d;%.1f;%d\n", millis() / 1000, umidade, ph,
                nitrogenio, fosforo, potassio, chuvaPrevistaMm, bombaLigada);
}
