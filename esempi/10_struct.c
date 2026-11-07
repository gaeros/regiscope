/*
 * 10 · Struct  (livello 5 di 6)
 *
 * Cosa impari: una struct raggruppa più valori sotto un solo nome,
 * come una scheda con dei campi. Si accede ai campi con il punto
 * (p.x) o, da un puntatore, con la freccia (q->x).
 *
 * Cosa guardare: i due campi x e y stanno vicini nello stack, 4 byte
 * ciascuno. sposta() li modifica tramite il puntatore.
 *
 * Prova tu: aggiungi un campo "colore" di tipo char e guarda come
 * cambia sizeof(struct punto).
 */
#include <stdio.h>

struct punto {
    int x;
    int y;
};

void sposta(struct punto *q, int dx, int dy) {
    q->x = q->x + dx;
    q->y = q->y + dy;
}

int main(void) {
    struct punto p = {2, 3};
    sposta(&p, 10, 20);
    printf("p = (%d, %d), dimensione = %d byte\n", p.x, p.y, (int)sizeof(p));
    return p.x;
}
