/*
 * 06 · Array  (livello 3 di 6)
 *
 * Cosa impari: un array è una fila di variabili dello stesso tipo,
 * una accanto all'altra in memoria. voti[0] è il primo elemento,
 * voti[4] il quinto. Ogni int occupa 4 byte, quindi in una riga di
 * 8 byte del pannello RAM ci stanno due elementi.
 *
 * Cosa guardare: nello stack i cinque valori uno dopo l'altro;
 * durante il ciclo vedi cambiare massimo.
 *
 * Prova tu: calcola anche la media dei voti.
 */
#include <stdio.h>

int main(void) {
    int voti[5] = {24, 30, 18, 27, 21};
    int massimo = voti[0];

    for (int i = 1; i < 5; i++) {
        if (voti[i] > massimo)
            massimo = voti[i];
    }

    printf("voto massimo = %d\n", massimo);
    printf("l'array occupa %d byte\n", (int)sizeof(voti));
    return massimo;
}
