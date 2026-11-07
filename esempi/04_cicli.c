/*
 * 04 · Cicli for e while  (livello 2 di 6)
 *
 * Cosa impari: un ciclo ripete un blocco finché una condizione è
 * vera. "for" raccoglie in una riga inizio, condizione e passo;
 * "while" controlla solo la condizione.
 *
 * Cosa guardare: con Avanti riga (F11) la freccia verde torna su
 * alla riga del for a ogni giro. Nello stack vedi crescere somma.
 *
 * Prova tu: calcola la somma dei soli numeri pari da 1 a 10.
 */
#include <stdio.h>

int main(void) {
    int somma = 0;
    for (int i = 1; i <= 10; i++) {
        somma = somma + i;
    }

    int n = 100;
    int passi = 0;
    while (n > 1) {
        n = n / 2;
        passi++;
    }

    printf("somma 1..10 = %d, dimezzamenti di 100 = %d\n", somma, passi);
    return somma;
}
