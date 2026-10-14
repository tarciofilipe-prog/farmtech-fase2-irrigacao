"""
FarmTech Solutions - Fase 2 - Ir além (opcional 1)
Integração Python com API pública de previsão do tempo.

Consulta a previsão de chuva para as próximas 24 horas na API pública
Open-Meteo (gratuita, sem necessidade de chave) e informa o valor que deve
ser digitado no Monitor Serial do ESP32 no Wokwi. O ESP32 usa esse valor
para suspender a irrigação quando a chuva prevista for de 5 mm ou mais.

Opcionalmente, se a variável de ambiente OPENWEATHER_API_KEY estiver
definida, a consulta é feita na API da OpenWeather.

Como executar:
    python previsao_chuva.py                      (Recife/PE, padrão)
    python previsao_chuva.py -8.05 -34.90 "Recife/PE"

Autor: Tárcio Filipe de Almeida Barros - RM576489
"""

import json
import os
import sys
import urllib.error
import urllib.request
from datetime import datetime

LIMIAR_CHUVA_MM = 5.0          # mesmo limiar usado no ESP32
LOCAL_PADRAO = (-8.05, -34.90, "Recife/PE (Zona da Mata - polo canavieiro)")


def ler_argumentos(argumentos):
    """Lê latitude, longitude e nome do local da linha de comando (com validação)."""
    if len(argumentos) >= 2:
        try:
            lat, lon = float(argumentos[0]), float(argumentos[1])
        except ValueError:
            sys.exit("Latitude e longitude devem ser números. Ex.: -8.05 -34.90")
        if not (-90 <= lat <= 90 and -180 <= lon <= 180):
            sys.exit("Coordenadas fora da faixa válida.")
        nome = argumentos[2] if len(argumentos) >= 3 else f"{lat}, {lon}"
        return lat, lon, nome
    return LOCAL_PADRAO


def buscar_json(url):
    """Faz a requisição HTTP e devolve o JSON (dicionário)."""
    requisicao = urllib.request.Request(url, headers={"User-Agent": "FarmTech-FIAP"})
    with urllib.request.urlopen(requisicao, timeout=15) as resposta:
        return json.loads(resposta.read().decode("utf-8"))


def previsao_open_meteo(lat, lon):
    """Chuva prevista (mm) e maior probabilidade de chuva (%) nas próximas 24 h."""
    url = ("https://api.open-meteo.com/v1/forecast"
           f"?latitude={lat}&longitude={lon}"
           "&hourly=precipitation,precipitation_probability"
           "&forecast_hours=24&timezone=America%2FSao_Paulo")
    dados = buscar_json(url)
    horas = dados["hourly"]["time"]
    chuva = [v or 0.0 for v in dados["hourly"]["precipitation"]]
    probabilidade = [v or 0 for v in dados["hourly"]["precipitation_probability"]]
    return horas, chuva, probabilidade


def previsao_openweather(lat, lon, chave):
    """Mesma informação usando a OpenWeather (blocos de 3 h, 8 blocos = 24 h)."""
    url = ("https://api.openweathermap.org/data/2.5/forecast"
           f"?lat={lat}&lon={lon}&units=metric&cnt=8&appid={chave}")
    dados = buscar_json(url)
    horas, chuva, probabilidade = [], [], []
    for bloco in dados["list"]:
        horas.append(bloco["dt_txt"])
        chuva.append(bloco.get("rain", {}).get("3h", 0.0))
        probabilidade.append(round(bloco.get("pop", 0) * 100))
    return horas, chuva, probabilidade


def main():
    lat, lon, nome = ler_argumentos(sys.argv[1:])
    chave = os.environ.get("OPENWEATHER_API_KEY")
    fonte = "OpenWeather" if chave else "Open-Meteo"

    print("=" * 60)
    print(" FarmTech - Previsão de chuva para a irrigação")
    print("=" * 60)
    print(f" Local : {nome} ({lat}, {lon})")
    print(f" Fonte : {fonte}")
    print(f" Data  : {datetime.now():%d/%m/%Y %H:%M}")

    try:
        if chave:
            horas, chuva, prob = previsao_openweather(lat, lon, chave)
        else:
            horas, chuva, prob = previsao_open_meteo(lat, lon)
    except (urllib.error.URLError, KeyError, ValueError) as erro:
        print(f"\nNão foi possível consultar a API: {erro}")
        print("Sem previsão: digite 0 no Monitor Serial (o ESP32 decide só pelos sensores).")
        return

    total = round(sum(chuva), 1)
    prob_max = max(prob) if prob else 0
    horas_com_chuva = sum(1 for v in chuva if v > 0)

    print("-" * 60)
    print(f" Chuva prevista (24 h)        : {total:.1f} mm")
    print(f" Maior probabilidade de chuva : {prob_max}%")
    print(f" Períodos com chuva           : {horas_com_chuva} de {len(chuva)}")
    print("-" * 60)

    if total >= LIMIAR_CHUVA_MM:
        print(f" Decisão sugerida: SUSPENDER a irrigação (chuva ≥ {LIMIAR_CHUVA_MM:.0f} mm)")
    else:
        print(" Decisão sugerida: manter a irrigação conforme os sensores")
    print("\n >>> Digite no Monitor Serial do Wokwi e pressione Enter:")
    print(f"     {total}")

    # Guarda a consulta em JSON para registro/histórico
    registro = {"local": nome, "latitude": lat, "longitude": lon, "fonte": fonte,
                "consultado_em": datetime.now().isoformat(timespec="seconds"),
                "chuva_24h_mm": total, "probabilidade_maxima": prob_max,
                "suspender_irrigacao": total >= LIMIAR_CHUVA_MM}
    caminho = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ultima_previsao.json")
    with open(caminho, "w", encoding="utf-8") as arquivo:
        json.dump(registro, arquivo, ensure_ascii=False, indent=2)
    print(f"\n Consulta salva em {caminho}")


if __name__ == "__main__":
    main()
