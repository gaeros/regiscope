/* Periferiche del PC, fornite da RegiScope (finestra "Periferiche").
   Tutte restituiscono 1 se è andata bene, 0 se no (il motivo compare nell'output). */
#ifndef REGISCOPE_PERIFERICHE_H
#define REGISCOPE_PERIFERICHE_H

/* Fotocamera. Le immagini sono RGB: 3 byte per pixel (rosso, verde, blu),
   riga per riga dall'alto in basso: il pixel (x, y) inizia a rgb[(y * larghezza + x) * 3]. */
int  camera_apri(void);
int  camera_scatta(unsigned char *rgb, int larghezza, int altezza); /* foto ridotta a larghezza x altezza */
void camera_chiudi(void);
void mostra_immagine(const unsigned char *rgb, int larghezza, int altezza);

/* Posizione: gradi di latitudine e longitudine, precisione in metri. */
int posizione_leggi(double *latitudine, double *longitudine, double *precisione);

/* Da dove arriva l'ultima posizione letta con posizione_leggi
   (-1 se non ne è stata letta nessuna). */
#define FONTE_CELLULARE    0  /* celle della rete mobile (SIM) */
#define FONTE_GPS          1  /* satellite (GPS/GNSS) */
#define FONTE_WIFI         2  /* reti Wi-Fi vicine */
#define FONTE_IP           3  /* indirizzo IP: molto approssimativa */
#define FONTE_SCONOSCIUTA  4
#define FONTE_PREDEFINITA  5  /* posizione impostata a mano in Windows */
#define FONTE_APPROSSIMATA 6  /* resa imprecisa da Windows per privacy */
int posizione_fonte(void);

/* 1 se nel PC c'è un ricevitore GPS (satellitare), 0 se no:
   senza GPS la posizione arriva da Wi-Fi, rete cellulare o indirizzo IP. */
int gps_presente(void);

/* Microfono: 16000 campioni al secondo, ognuno da -32768 a 32767.
   Restituisce quanti campioni ha registrato (0 se errore). */
int microfono_registra(short *campioni, int quanti);

/* Altoparlante: fa ascoltare dei campioni (16000 al secondo), per esempio
   quelli registrati dal microfono. Non aspetta la fine del suono. */
void altoparlante_suona(const short *campioni, int quanti);

/* Batteria: percentuale 0-100, in_carica 1 se collegato alla corrente. */
int batteria_leggi(int *percentuale, int *in_carica);

/* Sensori (presenti soprattutto su tablet e portatili 2-in-1). */
int luce_leggi(double *lux);
int accelerometro_leggi(double *x, double *y, double *z); /* in g: 1 = gravità terrestre */

#endif
