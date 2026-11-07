/* Compila tutta la libreria TinyCC in questa unità (ONE_SOURCE) e aggiunge
   la ricerca "indirizzo -> riga di C", riusando il lettore stabs di tccrun.c. */
#include "libtcc.c"
#include "tcc_lines.h"

struct RegiScopeLines {
    unsigned char *stab;
    unsigned long stab_len;
    char *str;
    unsigned long str_len;
    long long main_off; /* posizione di main dentro .text, o -1 */
};

/* Posizione (offset dentro .text) di una funzione, letta dalla tabella dei
   simboli PRIMA di tcc_relocate: dopo, le sezioni di TinyCC vengono liberate
   e non si possono più leggere (con Visual Studio contengono 0xDDDD...). */
static long long text_offset_of(struct TCCState *s1, const char *name)
{
    int i = find_elf_sym(symtab_section, name);
    ElfW(Sym) *sym;
    if (!i)
        return -1;
    sym = &((ElfW(Sym) *)symtab_section->data)[i];
    if (sym->st_shndx != text_section->sh_num)
        return -1;
    return (long long)sym->st_value;
}

RegiScopeLines *regiscope_capture_lines(struct TCCState *s1)
{
    RegiScopeLines *l;
#ifdef CONFIG_TCC_BACKTRACE
    s1->do_backtrace = 0;
#endif
    if (!s1->do_debug || s1->dwarf || !stab_section || !stab_section->link)
        return NULL;
    l = (RegiScopeLines *)tcc_mallocz(sizeof *l);
    if (!l)
        return NULL;
    l->stab_len = stab_section->data_offset;
    l->str_len = stab_section->link->data_offset;
    l->stab = (unsigned char *)tcc_malloc(l->stab_len ? l->stab_len : 1);
    l->str = (char *)tcc_malloc(l->str_len + 1);
    if (!l->stab || !l->str) {
        regiscope_free_lines(l);
        return NULL;
    }
    memcpy(l->stab, stab_section->data, l->stab_len);
    memcpy(l->str, stab_section->link->data, l->str_len);
    l->str[l->str_len] = 0;

    /* Gli indirizzi delle funzioni (N_FUN) qui valgono ancora 0: li riempiamo
       con la loro posizione dentro .text. */
    l->main_off = text_offset_of(s1, "main");
    {
        Stab_Sym *sym = (Stab_Sym *)l->stab, *end = (Stab_Sym *)(l->stab + l->stab_len);
        char name[128];
        for (; sym < end; ++sym) {
            const char *str, *colon;
            size_t len;
            long long off;
            if (sym->n_type != N_FUN || sym->n_strx == 0 || sym->n_strx >= l->str_len)
                continue;
            str = l->str + sym->n_strx;
            colon = strchr(str, ':');
            len = colon ? (size_t)(colon - str) : strlen(str);
            if (len == 0 || len >= sizeof name)
                continue;
            memcpy(name, str, len);
            name[len] = 0;
            off = text_offset_of(s1, name);
            if (off >= 0)
                sym->n_value = (unsigned)off;
        }
    }
    return l;
}

unsigned long long regiscope_finish_lines(RegiScopeLines *l, struct TCCState *s1)
{
    /* Inizio reale di .text = indirizzo finale di main - posizione di main in .text.
       (Non usiamo text_section->sh_addr: dopo tcc_relocate la sezione è già liberata.) */
    void *main_addr;
    if (!l || l->main_off < 0)
        return 0;
    main_addr = tcc_get_symbol(s1, "main");
    if (!main_addr)
        return 0;
    return (unsigned long long)(addr_t)main_addr - (unsigned long long)l->main_off;
}

int regiscope_line_for_pc(const RegiScopeLines *l, unsigned long long text_base, unsigned long long pc,
                       char *file, int file_size)
{
    if (file && file_size > 0)
        file[0] = 0;
#ifdef CONFIG_TCC_BACKTRACE
    {
        rt_context rc;
        bt_info bi;
        if (!l)
            return 0;
        memset(&rc, 0, sizeof rc);
        memset(&bi, 0, sizeof bi);
        rc.stab_sym = (Stab_Sym *)l->stab;
        rc.stab_sym_end = (Stab_Sym *)(l->stab + l->stab_len);
        rc.stab_str = l->str;
        rc.prog_base = (addr_t)text_base;
        /* rt_printline cerca indirizzi di ritorno (pc dopo la call): +1 per
           ottenere la riga dell'istruzione che inizia esattamente a pc */
        rt_printline(&rc, (addr_t)pc + 1, &bi);
        if (file && file_size > 0)
            pstrcpy(file, file_size, bi.file);
        return bi.line;
    }
#else
    (void)l; (void)text_base; (void)pc;
    return 0;
#endif
}

void regiscope_free_lines(RegiScopeLines *l)
{
    if (!l)
        return;
    tcc_free(l->stab);
    tcc_free(l->str);
    tcc_free(l);
}

/* Diagnostica: riassume la tabella delle righe (per capire perché una riga non si trova). */
void regiscope_lines_debug(const RegiScopeLines *l, unsigned long long text_base, unsigned long long pc,
                           char *out, int out_size)
{
    Stab_Sym *sym, *end;
    int n = 0, nfun = 0, nline = 0, line;
    char file[100], names[160];
    names[0] = 0;
    if (!l) {
        snprintf(out, out_size, "tabella righe assente");
        return;
    }
    end = (Stab_Sym *)(l->stab + l->stab_len);
    for (sym = (Stab_Sym *)l->stab; sym < end; ++sym, ++n) {
        if (sym->n_type == N_FUN && sym->n_strx) {
            char tmp[40];
            ++nfun;
            if (sym->n_strx < l->str_len && strlen(names) < 100) {
                snprintf(tmp, sizeof tmp, " %.16s@%x", l->str + sym->n_strx, sym->n_value);
                pstrcat(names, sizeof names, tmp);
            }
        }
        if (sym->n_type == N_SLINE) ++nline;
        if (sym->n_type == N_SO && sym->n_strx && sym->n_strx < l->str_len && strlen(names) < 100) {
            char tmp[60];
            snprintf(tmp, sizeof tmp, " SO'%.40s'", l->str + sym->n_strx);
            pstrcat(names, sizeof names, tmp);
        }
    }
    line = regiscope_line_for_pc(l, text_base, pc, file, sizeof file);
    snprintf(out, out_size, "stabs=%d fun=%d sline=%d base=%llx pc-base=%llx file='%s' riga=%d |%s", n, nfun,
             nline, text_base, pc - text_base, file, line, names);
}
