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
 * da 160000 byte contiene i campioni (i primi byte sono il silenzio iniziale).
 *
 * Alla fine il programma ti fa riascoltare la registrazione al contrario.
 *
 * Prova tu: calcola il volume medio (media dei valori assoluti), oppure
 * raddoppia il volume (attento a non superare 32767!).
 */
#include <stdio.h>
#include <stdlib.h>
#include <periferiche.h>

#define CAMPIONI (16000 * 5)   /* 5 secondi */

int main(void) {
    short *suono = malloc(CAMPIONI * sizeof(short));
    printf("Registro 5 secondi: parla o fischia!\n");
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
    printf("Frequenza stimata: circa %d Hz\n", (int)((long long)passaggi / 2 * 16000 / n));

    /* al contrario: scambiamo il primo campione con l'ultimo, e così via */
    for (i = 0; i < n / 2; i++) {
        short t = suono[i];
        suono[i] = suono[n - 1 - i];
        suono[n - 1 - i] = t;
    }
    printf("Ora ascolta la registrazione al contrario...\n");
    altoparlante_suona(suono, n);

    free(suono);
    return 0;
}
