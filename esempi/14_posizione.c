/*
 * 14 · Posizione e numeri con la virgola  (periferiche)
 *
 * Cosa impari: la posizione arriva come due numeri "double" (8 byte,
 * 64 bit ciascuno). Un double non è scritto come un intero: ha 1 bit di
 * segno, 11 bit di esponente e 52 bit di "mantissa" (le cifre).
 * Leggendo gli stessi 8 byte come intero vediamo come è fatto davvero.
 *
 * Cosa guardare: nella finestra Periferiche la latitudine compare con
 * i suoi 64 bit. Nel pannello RAM, sullo stack, ci sono gli stessi byte
 * nelle variabili lat e lon. Per funzionare servono la posizione attiva
 * in Impostazioni → Privacy e sicurezza → Posizione e un po' di pazienza
 * (fino a 15 secondi).
 *
 * Da dove arriva? gps_presente() dice se il PC ha un ricevitore GPS;
 * posizione_fonte() dice se Windows ha usato il GPS
 * (satelliti), le reti Wi-Fi vicine, la rete cellulare o solo
 * l'indirizzo IP. Confronta la fonte con la precisione: GPS e Wi-Fi
 * danno pochi metri, l'IP anche diversi chilometri.
 *
 * Prova tu: calcola quanti chilometri sei dall'equatore (1 grado = 111 km).
 */
#include <stdio.h>
#include <string.h>
#include <periferiche.h>

int main(void) {
    double lat = 0, lon = 0, precisione = 0;

    if (gps_presente())
        printf("Questo PC ha un ricevitore GPS.\n");
    else
        printf("Questo PC non ha il GPS: Windows usa Wi-Fi, rete o IP.\n");

    if (!posizione_leggi(&lat, &lon, &precisione)) {
        printf("Posizione non disponibile: uso Roma come esempio.\n");
        lat = 41.902782;
        lon = 12.496366;
    }
    printf("Latitudine  %f\n", lat);
    printf("Longitudine %f\n", lon);
    printf("Precisione  %.0f metri\n", precisione);

    switch (posizione_fonte()) {
    case FONTE_GPS:       printf("Fonte       satellite (GPS)\n"); break;
    case FONTE_WIFI:      printf("Fonte       reti Wi-Fi vicine\n"); break;
    case FONTE_CELLULARE: printf("Fonte       rete cellulare\n"); break;
    case FONTE_IP:        printf("Fonte       indirizzo IP\n"); break;
    case -1:              printf("Fonte       nessuna (posizione non letta)\n"); break;
    default:              printf("Fonte       altra o sconosciuta\n"); break;
    }

    /* gli stessi 8 byte di lat, letti come intero senza segno */
    unsigned long long bit;
    memcpy(&bit, &lat, 8);
    int segno = (int)(bit >> 63);
    int esponente = (int)((bit >> 52) & 0x7FF) - 1023;
    printf("Bit di lat: 0x%016llX\n", bit);
    printf("segno %d, esponente %d (cioe' tra 2^%d e 2^%d)\n", segno, esponente, esponente, esponente + 1);
    return 0;
}
