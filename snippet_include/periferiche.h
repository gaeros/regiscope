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
