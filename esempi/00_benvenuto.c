/*
 * 00 · Benvenuto in RegiScope  (esempio iniziale)
 *
 * Cosa fa: mette 5 in x e lo incrementa a 6, poi chiede al sistema
 * un blocco di memoria per 4 interi (malloc) e ci scrive i quadrati
 * di 0, 1, 2, 3 calcolati dalla funzione quadrato(). Infine stampa
 * x e v[3], libera il blocco (free) e restituisce x.
 *
 * Cosa guardare con Avanti (F10) e Avanti riga (F11):
 *  - CPU: RAX cambia quando x passa da 5 a 6 e quando quadrato()
 *    restituisce il risultato; RSP e RIP seguono la chiamata.
 *  - RAM: x e i (il contatore del ciclo) sono nei bit dello stack, il
 *    blocco di malloc compare sotto "Heap" e si riempie a ogni giro.
 */
#include <stdio.h>
#include <stdlib.h>

int quadrato(int n) {
    return n * n;
}

int main(void) {
    int x = 5;
    x = x + 1;

    int *v = malloc(4 * sizeof(int));
    for (int i = 0; i < 4; i++)
        v[i] = quadrato(i);

    printf("x = %d, v[3] = %d\n", x, v[3]);
    free(v);
    return x;
}
