# TarcioFilipeDeAlmeidaBarros_RM576489_fase2_irrigacao
###############################################################################
# FarmTech Solutions - Fase 2 - Ir além (opcional 2)
# Análise estatística em R para decidir se a bomba de irrigação deve ligar
#
# Entrada: data/leituras_exemplo.csv (mesmo formato das linhas "CSV;" que o
#          ESP32 imprime no Monitor Serial do Wokwi).
#          Para usar leituras reais: copie as linhas que começam com "CSV;"
#          do Monitor Serial para um arquivo .csv (com o cabeçalho) e informe
#          o caminho na variável ARQUIVO abaixo.
#
# Método:
#   1) Estatística descritiva da umidade do solo
#   2) Regressão linear da umidade em função do tempo, usando apenas os
#      períodos com a bomba desligada -> taxa de secagem do solo (%/hora)
#   3) Projeção: em quantas horas o solo chega a 60% (ponto de irrigação)
#      e a 30% (ponto crítico), com intervalo de confiança de 95%
#   4) Decisão: LIGAR agora, PROGRAMAR irrigação ou AGUARDAR
#
# Como executar (na pasta do projeto):  Rscript src/r/analise_irrigacao.R
###############################################################################

ARQUIVO          <- "data/leituras_exemplo.csv"
UMIDADE_LIGAR    <- 60    # mesmos limiares do ESP32
UMIDADE_CRITICA  <- 30
HORIZONTE_HORAS  <- 6     # se o solo atingir 60% nesse prazo, programar irrigação

# Aceita o arquivo com ou sem a coluna "CSV" que vem do Monitor Serial
dados <- read.csv2(ARQUIVO, header = TRUE, dec = ".", stringsAsFactors = FALSE)
if (names(dados)[1] == "CSV") dados <- dados[, -1]
dados$horas <- dados$tempo_s / 3600

br <- function(x, casas = 2) trimws(format(round(x, casas), decimal.mark = ",", nsmall = casas))

cat("============================================================\n")
cat(" FarmTech - Análise da umidade do solo para irrigação\n")
cat("============================================================\n")
cat("Leituras:", nrow(dados), "| Período:", br(min(dados$horas), 1), "a",
    br(max(dados$horas), 1), "horas\n")

# ---------------------------------------------------------------------------
# 1) Estatística descritiva
# ---------------------------------------------------------------------------
u <- dados$umidade
cat("\n--- Umidade do solo (%) ---\n")
cat("Média ........:", br(mean(u)), "\n")
cat("Mediana ......:", br(median(u)), "\n")
cat("Desvio padrão :", br(sd(u)), "\n")
cat("Mínimo/Máximo :", br(min(u)), "/", br(max(u)), "\n")
cat("Quartis ......:", paste(br(quantile(u, c(.25, .5, .75))), collapse = " | "), "\n")
cat("Tempo com bomba ligada:", br(100 * mean(dados$bomba), 1), "% das leituras\n")

# ---------------------------------------------------------------------------
# 2) Regressão linear: taxa de secagem com a bomba desligada
# ---------------------------------------------------------------------------
# Usa o último trecho contínuo de secagem (bomba desligada até a última leitura)
ultimo_ligado <- suppressWarnings(max(which(dados$bomba == 1)))
trecho <- if (is.finite(ultimo_ligado)) dados[(ultimo_ligado + 1):nrow(dados), ] else dados
if (nrow(trecho) < 5) trecho <- dados[dados$bomba == 0, ]   # garante pontos suficientes

modelo <- lm(umidade ~ horas, data = trecho)
taxa <- coef(modelo)["horas"]
r2 <- summary(modelo)$r.squared
p_valor <- summary(modelo)$coefficients["horas", "Pr(>|t|)"]

cat("\n--- Regressão linear (trecho de secagem: ", nrow(trecho), " leituras) ---\n", sep = "")
print(summary(modelo)$coefficients)
cat("Taxa de variação da umidade:", br(taxa, 3), "% por hora\n")
cat("R² ..........................:", br(r2, 3), "\n")
cat("p-valor da inclinação .......:", format.pval(p_valor, digits = 3), "\n")

# ---------------------------------------------------------------------------
# 3) Projeção do momento em que o solo chega aos limiares
# ---------------------------------------------------------------------------
agora <- max(dados$horas)
umidade_atual <- tail(dados$umidade, 1)
futuro <- data.frame(horas = seq(agora, agora + 48, by = 0.25))
prev <- predict(modelo, newdata = futuro, interval = "prediction", level = 0.95)
futuro <- cbind(futuro, prev)

horas_ate <- function(limiar) {
  idx <- which(futuro$fit <= limiar)
  if (length(idx) == 0) return(NA)
  futuro$horas[min(idx)] - agora
}
h60 <- horas_ate(UMIDADE_LIGAR)
h30 <- horas_ate(UMIDADE_CRITICA)

cat("\n--- Projeção (", br(umidade_atual, 1), "% agora) ---\n", sep = "")
cat("Horas até 60% (ponto de irrigação):", ifelse(is.na(h60), "mais de 48 h", br(h60, 1)), "\n")
cat("Horas até 30% (ponto crítico) .....:", ifelse(is.na(h30), "mais de 48 h", br(h30, 1)), "\n")

# ---------------------------------------------------------------------------
# 4) Decisão
# ---------------------------------------------------------------------------
if (umidade_atual < UMIDADE_LIGAR) {
  decisao <- "LIGAR A BOMBA AGORA (umidade abaixo de 60%)"
} else if (taxa < 0 && p_valor < 0.05 && !is.na(h60) && h60 <= HORIZONTE_HORAS) {
  decisao <- paste0("PROGRAMAR IRRIGAÇÃO: o solo deve chegar a 60% em cerca de ",
                    br(h60, 1), " h")
} else {
  decisao <- paste0("AGUARDAR: sem necessidade de irrigar nas próximas ", HORIZONTE_HORAS, " h",
                    ifelse(is.na(h60), "", paste0(" (60% previsto em cerca de ", br(h60, 1), " h)")))
}
cat("\n>>> DECISÃO:", decisao, "\n")
cat("(A secagem é estatisticamente significativa se p-valor < 0,05.)\n")

# ---------------------------------------------------------------------------
# 5) Gráficos
# ---------------------------------------------------------------------------
par(mfrow = c(1, 2), mar = c(5, 4.5, 3, 1))

cores <- ifelse(dados$bomba == 1, "#1E88E5", "#8D6E63")
plot(dados$horas, dados$umidade, pch = 19, col = cores,
     xlim = c(0, agora + 24), ylim = c(min(25, min(futuro$lwr[futuro$horas <= agora + 24])), 80),
     xlab = "Tempo (horas)", ylab = "Umidade do solo (%)",
     main = "Umidade do solo e projeção")
sel <- futuro$horas <= agora + 24
lines(futuro$horas[sel], futuro$fit[sel], col = "#C0392B", lwd = 2, lty = 2)
polygon(c(futuro$horas[sel], rev(futuro$horas[sel])),
        c(futuro$lwr[sel], rev(futuro$upr[sel])),
        col = adjustcolor("#C0392B", 0.15), border = NA)
abline(h = UMIDADE_LIGAR, col = "#F9A825", lwd = 2)
abline(h = UMIDADE_CRITICA, col = "#B71C1C", lwd = 2)
legend("topright", bty = "n", cex = 0.7,
       legend = c("Bomba desligada", "Bomba ligada", "Projeção (IC 95%)", "Ligar (60%)", "Crítico (30%)"),
       col = c("#8D6E63", "#1E88E5", "#C0392B", "#F9A825", "#B71C1C"),
       pch = c(19, 19, NA, NA, NA), lty = c(NA, NA, 2, 1, 1), lwd = 2)

boxplot(umidade ~ factor(bomba, labels = c("Desligada", "Ligada")), data = dados,
        col = c("#BCAAA4", "#90CAF9"), xlab = "Estado da bomba",
        ylab = "Umidade do solo (%)", main = "Umidade x estado da bomba")

par(mfrow = c(1, 1))
