/*
 * 02 · Tipi e overflow  (livello 1 di 6)
 *
 * Cosa impari: ogni tipo ha una dimensione fissa. "unsigned char"
 * ha 8 bit: va da 0 a 255. Se sommi 1 a 255 i bit "traboccano" e
 * il valore torna a 0 (overflow). "char" con segno va da -128 a 127.
 *
 * Cosa guardare: le righe dello stack nel pannello RAM. 255 è
 * 11111111 in binario; dopo +1 tutti gli 8 bit tornano a 0.
 *
 * Prova tu: usa "short" (16 bit) al posto di unsigned char e
 * parti da 32767.
 */
#include <stdio.h>

int main(void) {
    unsigned char piccolo = 255;
    char con_segno = 127;
    long long grande = 1;

    piccolo = piccolo + 1;       /* 255 + 1 = 0 */
    con_segno = con_segno + 1;   /* 127 + 1 = -128 */
    grande = grande << 40;       /* 2 elevato alla 40 */

    printf("piccolo = %d, con_segno = %d\n", piccolo, con_segno);
    printf("grande = %lld (%d byte)\n", grande, (int)sizeof(grande));
    return 0;
}
