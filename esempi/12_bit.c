/*
 * 12 · Operazioni sui bit  (livello 6 di 6)
 *
 * Cosa impari: si possono accendere, spegnere e leggere singoli bit.
 *   a & b  AND: 1 solo dove entrambi sono 1 (per leggere/spegnere)
 *   a | b  OR:  1 dove almeno uno è 1 (per accendere)
 *   a ^ b  XOR: 1 dove sono diversi (per invertire)
 *   a << n sposta i bit a sinistra (moltiplica per 2 alla n)
 *
 * Cosa guardare: dopo ogni riga, nel registro usato e nello stack,
 * i bit colorati sono proprio quelli che l'operazione ha cambiato.
 *
 * Prova tu: conta quanti bit valgono 1 in un numero.
 */
#include <stdio.h>

int main(void) {
    unsigned int flags = 0;

    flags = flags | (1u << 3);      /* accende il bit 3 */
    flags = flags | (1u << 0);      /* accende il bit 0 */
    flags = flags ^ 0xFFu;          /* inverte gli 8 bit bassi */
    flags = flags & ~(1u << 7);     /* spegne il bit 7 */

    int bit5 = (flags >> 5) & 1;    /* legge il bit 5 */
    printf("flags = 0x%X, bit 5 = %d\n", flags, bit5);
    return (int)flags;
}
