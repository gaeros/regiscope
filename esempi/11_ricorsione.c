/*
 * 11 · Ricorsione  (livello 6 di 6)
 *
 * Cosa impari: una funzione ricorsiva chiama se stessa su un
 * problema più piccolo, finché arriva a un caso semplice che
 * conosce già (qui n <= 1). fattoriale(4) = 4 * 3 * 2 * 1.
 *
 * Cosa guardare: a ogni chiamata RSP scende ancora: lo stack si
 * riempie di "cornici", una per chiamata. Al ritorno risale e RAX
 * porta il risultato parziale.
 *
 * Prova tu: scrivi la versione ricorsiva della somma 1..n.
 */
#include <stdio.h>

int fattoriale(int n) {
    if (n <= 1)
        return 1;
    return n * fattoriale(n - 1);
}

int main(void) {
    int r = fattoriale(4);
    printf("4! = %d\n", r);
    return r;
}
