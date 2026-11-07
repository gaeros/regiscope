/*
 * 03 · Decisioni con if / else  (livello 2 di 6)
 *
 * Cosa impari: "if" sceglie quale codice eseguire. Per decidere la
 * CPU confronta due numeri (istruzione cmp) e accende dei flag:
 * ZF = il risultato era zero (numeri uguali), SF = negativo.
 * Poi un salto condizionato (jl, jge, jne...) legge i flag.
 *
 * Cosa guardare: i riquadri CF, ZF, SF nel pannello CPU subito dopo
 * l'istruzione cmp, e quale ramo dell'if viene eseguito.
 *
 * Prova tu: cambia voto in 18 e poi in 30.
 */
#include <stdio.h>

int main(void) {
    int voto = 24;

    if (voto < 18) {
        printf("Non superato\n");
    } else if (voto == 30) {
        printf("Trenta!\n");
    } else {
        printf("Superato con %d\n", voto);
    }
    return voto;
}
