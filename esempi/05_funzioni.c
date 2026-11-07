/*
 * 05 · Funzioni e stack  (livello 3 di 6)
 *
 * Cosa impari: una funzione è un pezzo di codice con un nome, che
 * riceve parametri e restituisce un risultato. Su Windows x64 i
 * primi parametri viaggiano nei registri RCX, RDX, R8, R9 e il
 * risultato torna in RAX.
 *
 * Cosa guardare: prima di "call" i valori 3 e 4 sono in RCX e RDX.
 * Dentro la funzione RSP scende (lo stack cresce verso il basso) e
 * RIP salta al nuovo codice; dopo "ret" il risultato è in RAX.
 *
 * Prova tu: aggiungi una funzione media(a, b) e chiamala.
 */
#include <stdio.h>

int somma(int a, int b) {
    int risultato = a + b;
    return risultato;
}

int quadrato(int n) {
    return n * n;
}

int main(void) {
    int s = somma(3, 4);
    int q = quadrato(s);
    printf("somma = %d, quadrato = %d\n", s, q);
    return q;
}
