/*
 * 15 · Microfono: il suono come numeri  (periferiche)
 *
 * Cosa impari: il microfono misura la pressione dell'aria 16000 volte
 * al secondo. Ogni misura (campione) è uno "short": 2 byte con segno,
 * da -32768 a 32767. Un secondo di suono sono quindi 32000 byte.
 * Contando quante volte l'onda passa per lo zero si stima la frequenza:
 * più passaggi al secondo = suono più acuto.
 *
 * Cosa guardare: durante la registrazione parla o fischia. Nella finestra
 * Periferiche compare la forma d'onda; nel pannello RAM il blocco malloc
 * da 32000 byte contiene i campioni (i primi byte sono il silenzio iniziale).
 *
 * Prova tu: calcola il volume medio (media dei valori assoluti).
 */
#include <stdio.h>
#include <stdlib.h>
#include <periferiche.h>

#define CAMPIONI 16000   /* 1 secondo */

int main(void) {
    short *suono = malloc(CAMPIONI * sizeof(short));
    printf("Registro 1 secondo: parla o fischia!\n");
    int n = microfono_registra(suono, CAMPIONI);
    if (n == 0) {
        free(suono);
        return 1;
    }

    int picco = 0, passaggi = 0, i;
    for (i = 0; i < n; i++) {
        int v = suono[i] < 0 ? -suono[i] : suono[i];
        if (v > picco) picco = v;
        if (i > 0 && (suono[i - 1] < 0) != (suono[i] < 0))
            passaggi++;
    }
    printf("Campioni registrati: %d\n", n);
    printf("Picco: %d su 32767 (%d%%)\n", picco, picco * 100 / 32767);
    /* ogni onda completa passa per lo zero 2 volte */
    printf("Frequenza stimata: circa %d Hz\n", passaggi / 2 * 16000 / n);

    free(suono);
    return 0;
}
