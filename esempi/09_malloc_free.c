/*
 * 09 · Memoria dinamica: malloc e free  (livello 5 di 6)
 *
 * Cosa impari: lo stack ha spazio limitato e sparisce quando la
 * funzione finisce. Con malloc chiedi memoria all'heap, che resta
 * finché non la restituisci con free. malloc dà un puntatore.
 *
 * Cosa guardare: nel pannello RAM, sotto "Heap", compare un blocco
 * di 24 byte (6 int) che si riempie a ogni giro del ciclo; dopo
 * free il blocco risulta "liberato".
 *
 * Prova tu: togli la riga con free: il blocco resta occupato
 * (è un "memory leak").
 */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int n = 6;
    int *fib = malloc(n * sizeof(int));
    if (fib == NULL)
        return 1;

    fib[0] = 0;
    fib[1] = 1;
    for (int i = 2; i < n; i++)
        fib[i] = fib[i - 1] + fib[i - 2];

    printf("fib[%d] = %d\n", n - 1, fib[n - 1]);
    free(fib);
    return 0;
}
