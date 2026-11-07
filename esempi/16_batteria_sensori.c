/*
 * 16 · Batteria e sensori  (periferiche)
 *
 * Cosa impari: il programma chiede all'hardware qualche misura e decide
 * cosa fare con degli if. Le funzioni restituiscono 1 se la misura c'è
 * e 0 se manca (un PC fisso non ha batteria, pochi PC hanno sensori).
 * I valori arrivano attraverso dei puntatori: passiamo l'indirizzo
 * della variabile (&percentuale) e la funzione ci scrive dentro.
 *
 * Cosa guardare: nel pannello RAM, sullo stack, le variabili cambiano
 * subito dopo ogni chiamata; nella finestra Periferiche gli stessi
 * valori sono nei riquadri Batteria e Sensori.
 *
 * Prova tu: stampa "Inclinato!" se x o y dell'accelerometro superano 0.5.
 */
#include <stdio.h>
#include <periferiche.h>

int main(void) {
    int percentuale = 0, in_carica = 0;
    double lux = 0, x = 0, y = 0, z = 0;

    if (batteria_leggi(&percentuale, &in_carica)) {
        printf("Batteria al %d%%%s\n", percentuale, in_carica ? " (in carica)" : "");
        if (percentuale < 20 && !in_carica)
            printf("Attenzione: collega il caricabatterie!\n");
    }

    if (luce_leggi(&lux))
        printf("Luce: %.0f lux (%s)\n", lux, lux < 50 ? "buio" : lux < 1000 ? "stanza" : "pieno sole");

    if (accelerometro_leggi(&x, &y, &z))
        printf("Accelerometro: x %.2f  y %.2f  z %.2f g\n", x, y, z);

    return 0;
}
