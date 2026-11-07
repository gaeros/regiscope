/*
 * 13 · Fotocamera: pixel e colori  (periferiche)
 *
 * Cosa impari: una foto in memoria è solo una lunga fila di byte.
 * Ogni pixel occupa 3 byte (rosso, verde, blu, da 0 a 255) e i pixel
 * sono messi riga dopo riga: il pixel (x, y) inizia al byte (y * L + x) * 3.
 * Il programma scatta una foto piccola (64 x 48), ne calcola la luminosità
 * media e poi la trasforma nel suo negativo (255 - valore).
 *
 * Cosa guardare: si apre la finestra Periferiche con la foto. Nel pannello
 * RAM, sotto "Heap", c'è il blocco malloc da 9216 byte: sono i pixel.
 * Con Avanti riga dentro il ciclo vedi i byte cambiare uno alla volta.
 * Se il PC non ha una fotocamera il programma disegna una sfumatura.
 *
 * Prova tu: trasforma la foto in bianco e nero (media dei 3 colori).
 */
#include <stdio.h>
#include <stdlib.h>
#include <periferiche.h>

#define L 64
#define A 48

int main(void) {
    unsigned char *foto = malloc(L * A * 3);
    int x, y;

    if (!camera_scatta(foto, L, A)) {
        /* niente fotocamera: disegniamo noi una sfumatura rosso-blu */
        for (y = 0; y < A; y++)
            for (x = 0; x < L; x++) {
                unsigned char *p = foto + (y * L + x) * 3;
                p[0] = x * 4;      /* rosso cresce verso destra */
                p[1] = 60;         /* un po' di verde */
                p[2] = y * 5;      /* blu cresce verso il basso */
            }
        mostra_immagine(foto, L, A);
    }
    camera_chiudi();

    /* luminosità media: somma di tutti i byte diviso il loro numero */
    long somma = 0;
    int i;
    for (i = 0; i < L * A * 3; i++)
        somma += foto[i];
    printf("Luminosita' media: %ld su 255\n", somma / (L * A * 3));

    /* negativo: ogni byte diventa 255 meno se stesso */
    for (i = 0; i < L * A * 3; i++)
        foto[i] = 255 - foto[i];
    mostra_immagine(foto, L, A);
    printf("Ecco il negativo nella finestra Periferiche.\n");

    free(foto);
    return 0;
}
