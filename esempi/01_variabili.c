/*
 * 01 · Variabili e assegnamento  (livello 1 di 6)
 *
 * Cosa impari: una variabile è un nome per un pezzo di memoria.
 * "int" occupa 4 byte (32 bit). Il segno = copia un valore dentro
 * la variabile; x = x + 1 legge x, somma 1 e riscrive il risultato.
 *
 * Cosa guardare: con Avanti (F10) vedi il valore passare per EAX
 * (la metà bassa di RAX) e poi finire nello stack, nel pannello RAM.
 *
 * Prova tu: cambia 5 in 255 e guarda quali bit si accendono.
 */
#include <stdio.h>

int main(void) {
    int x = 5;
    int y = 10;
    x = x + 1;
    y = x * 2;
    printf("x = %d, y = %d\n", x, y);
    return y;
}
