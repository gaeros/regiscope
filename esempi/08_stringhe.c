/*
 * 08 · Stringhe  (livello 4 di 6)
 *
 * Cosa impari: in C una stringa è un array di char che finisce con
 * il carattere 0 ('\0'). Ogni lettera è un numero (codice ASCII):
 * 'C' = 67 = 01000011 in binario.
 *
 * Cosa guardare: i byte della parola nello stack, una lettera per
 * byte, e lo 0 finale. Il ciclo conta le lettere fino allo 0.
 *
 * Prova tu: trasforma la parola in maiuscolo sottraendo 32 alle
 * lettere minuscole.
 */
#include <stdio.h>

int lunghezza(const char *s) {
    int n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

int main(void) {
    char parola[] = "Ciao!";
    int n = lunghezza(parola);

    for (int i = 0; i < n; i++)
        printf("%c = %d\n", parola[i], parola[i]);

    printf("lunghezza = %d\n", n);
    return n;
}
