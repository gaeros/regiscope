/*
 * 07 · Puntatori  (livello 4 di 6)
 *
 * Cosa impari: un puntatore contiene un indirizzo di memoria.
 * &x vuol dire "l'indirizzo di x"; *p vuol dire "il valore che sta
 * all'indirizzo p". Scrivendo *p cambi la variabile a cui p punta.
 *
 * Cosa guardare: il valore esadecimale di p nel pannello RAM è un
 * indirizzo vicino a RSP, cioè proprio dentro lo stack. La funzione
 * scambia() modifica le variabili di main attraverso i puntatori.
 *
 * Prova tu: scrivi una funzione raddoppia(int *n).
 */
#include <stdio.h>

void scambia(int *a, int *b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

int main(void) {
    int x = 1;
    int y = 2;
    int *p = &x;

    *p = 10;               /* adesso x vale 10 */
    scambia(&x, &y);       /* adesso x = 2, y = 10 */

    printf("x = %d, y = %d, p = %p\n", x, y, (void *)p);
    return x;
}
