#pragma once
// Collega un indirizzo del codice compilato alla riga di C da cui proviene,
// usando le informazioni di debug (-g, formato stabs) di TinyCC.
#ifdef __cplusplus
extern "C" {
#endif

struct TCCState;
typedef struct RegiScopeLines RegiScopeLines;

// Da chiamare dopo aver compilato e PRIMA di tcc_relocate: copia la tabella
// delle righe (tcc_relocate la libera) e spegne i backtrace di TinyCC, che con
// -g in memoria richiederebbero file di runtime che non usiamo.
RegiScopeLines *regiscope_capture_lines(struct TCCState *s);

// Dopo tcc_relocate: completa la tabella con gli indirizzi reali delle
// funzioni e restituisce l'indirizzo di partenza del codice (sezione .text).
unsigned long long regiscope_finish_lines(RegiScopeLines *lines, struct TCCState *s);

// Numero di riga (da 1) per l'indirizzo pc, o 0. In 'file' il nome del
// sorgente ("<string>" per lo snippet).
int regiscope_line_for_pc(const RegiScopeLines *lines, unsigned long long text_base, unsigned long long pc,
                       char *file, int file_size);

void regiscope_free_lines(RegiScopeLines *lines);

// Diagnostica testuale della tabella delle righe.
void regiscope_lines_debug(const RegiScopeLines *lines, unsigned long long text_base, unsigned long long pc,
                           char *out, int out_size);

#ifdef __cplusplus
}
#endif
