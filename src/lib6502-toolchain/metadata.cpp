#include "metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int g_verbose = 0;
#define LOG_V2(...) if (g_verbose >= 2) fprintf(stderr, __VA_ARGS__)
#define LOG_V3(...) if (g_verbose >= 3) fprintf(stderr, __VA_ARGS__)

int load_binary(memory_t *mem, int addr, const char *filename) {
	FILE *bf = fopen(filename, "rb");
	if (!bf) {
		fprintf(stderr, "Warning: cannot open binary '%s': %s\n", filename, strerror(errno));
		return -1;
	}
	fseek(bf, 0, SEEK_END);
	long size = ftell(bf);
	rewind(bf);
	if (mem) {
		for (long i = 0; i < size; i++) {
			int c = fgetc(bf);
			if (c == EOF) { size = i; break; }
			if (addr + (int)i < 65536)
				mem_write(mem, addr + i, (unsigned char)c);
		}
	}
	fclose(bf);
	return (int)size;
}

int load_prg(memory_t *mem, const char *filename, int *out_load_addr) {
    LOG_V2("DEBUG: ENTER load_prg\n");
    LOG_V2("DEBUG: load_prg filename=%s mem=%p\n", filename ? filename : "NULL", (void*)mem);
    if (!filename) return -1;
    FILE *bf = fopen(filename, "rb");
    if (!bf) {
        fprintf(stderr, "Warning: cannot open PRG '%s': %s\n", filename, strerror(errno));
        return -1;
    }
    LOG_V2("DEBUG: PRG opened\n");

    unsigned char lo = fgetc(bf);
    unsigned char hi = fgetc(bf);
    int addr = (hi << 8) | lo;
    if (out_load_addr) *out_load_addr = addr;
    LOG_V2("DEBUG: PRG load address: $%04X\n", addr);

    fseek(bf, 0, SEEK_END);
    long size = ftell(bf) - 2;
    fseek(bf, 2, SEEK_SET);
    LOG_V2("DEBUG: PRG size: %ld\n", size);

    if (mem) {
        for (long i = 0; i < size; i++) {
            int c = fgetc(bf);
            if (c == EOF) { size = i; break; }
            if (addr + (int)i < 65536) {
                mem_write(mem, (unsigned short)(addr + i), (unsigned char)c);
            }
        }
    }
    LOG_V2("DEBUG: PRG loaded\n");
    fclose(bf);
    return (int)size;
}

/* Silently probe whether a file exists without printing warnings. */
static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Strip surrounding quotes and trailing whitespace: "SID #1"\n -> SID #1 */
static void strip_arg_quotes(const char *in, char *out, int outlen) {
    const char *start = (*in == '"') ? in + 1 : in;
    strncpy(out, start, outlen - 1);
    out[outlen - 1] = '\0';
    /* Trim trailing whitespace first */
    int len = (int)strlen(out);
    while (len > 0 && (out[len-1] == ' ' || out[len-1] == '\t' ||
                       out[len-1] == '\r' || out[len-1] == '\n'))
        out[--len] = '\0';
    /* Then remove closing quote if present */
    if (len > 0 && out[len-1] == '"') out[--len] = '\0';
}

/*
 * Preprocess a .asm source file: replace simulator pseudo-ops (.inspect, .trap)
 * with KickAssembler .print statements that embed the current PC in their output.
 * This allows recovering the assembled address for each pseudo-op by parsing
 * KickAssembler's stdout after assembly.
 *
 * Returns the number of pseudo-ops replaced, or 0 if none found (in which case
 * tmp_path is not written).
 */
/*
 * Detect a simulator pseudo-op embedded in a comment: "// .inspect ..." or "//.trap ..."
 * p must point to the start of the non-whitespace portion of the line.
 * If matched, sets *keyword to the keyword start and *kw_len to its length, returns true.
 */
static int detect_pseudoop(char *p, const char **keyword, int *kw_len) {
    if (p[0] != '/' || p[1] != '/') return 0;
    char *q = p + 2;
    while (*q == ' ' || *q == '\t') q++;
    if (strncmp(q, ".inspect", 8) == 0 && (!q[8] || q[8]==' ' || q[8]=='\t' || q[8]=='"')) {
        *keyword = q; *kw_len = 8; return 1;
    }
    if (strncmp(q, ".trap", 5) == 0 && (!q[5] || q[5]==' ' || q[5]=='\t' || q[5]=='"')) {
        *keyword = q; *kw_len = 5; return 1;
    }
    return 0;
}

/*
 * Preprocess a .asm source file: simulator pseudo-ops embedded in comments
 * ("//.inspect ..." or "// .trap ...") are replaced with KickAssembler .print
 * statements that emit the current PC to stdout.  This lets files assemble
 * directly with KickAssembler (the comment is ignored) while our pipeline
 * recovers the assembled addresses by parsing stdout after assembly.
 *
 * Returns the number of pseudo-ops replaced, or 0 if none (tmp_path not written).
 */
static int preprocess_asm_pseudoops(const char *asm_path, const char *tmp_path) {
    FILE *in = fopen(asm_path, "r");
    if (!in) return 0;

    /* First pass: count pseudo-ops */
    int count = 0;
    char line[4096];
    while (fgets(line, sizeof(line), in)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        const char *kw; int kw_len;
        if (detect_pseudoop(p, &kw, &kw_len)) count++;
    }
    if (count == 0) { fclose(in); return 0; }

    /* Second pass: rewrite */
    rewind(in);
    FILE *out = fopen(tmp_path, "w");
    if (!out) { fclose(in); return 0; }

    while (fgets(line, sizeof(line), in)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        const char *kw; int kw_len;
        if (detect_pseudoop(p, &kw, &kw_len)) {
            char *arg = (char *)kw + kw_len;
            while (*arg == ' ' || *arg == '\t') arg++;
            char clean[256];
            strip_arg_quotes(arg, clean, sizeof(clean));
            const char *tag = (kw_len == 8) ? "SIM_INSPECT" : "SIM_TRAP";
            fprintf(out, "    .print \"%s:\"+toHexString(*)+\":%s\"\n", tag, clean);
        } else {
            fputs(line, out);
        }
    }
    fclose(in);
    fclose(out);
    return count;
}

/*
 * Parse KickAssembler stdout (captured to a file) for SIM_INSPECT:/SIM_TRAP:
 * markers emitted by .print statements injected during preprocessing.
 * Adds matching SYM_INSPECT/SYM_TRAP symbols to the symbol table.
 */
static void apply_pseudoop_output(symbol_table_t *st, const char *output_file) {
    if (!st) return;
    FILE *f = fopen(output_file, "r");
    if (!f) return;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        unsigned int addr;
        char device[128];
        if (strncmp(p, "SIM_INSPECT:", 12) == 0 &&
                sscanf(p + 12, "%x:%127[^\n\r]", &addr, device) == 2) {
            char name[64];
            snprintf(name, sizeof(name), "inspect_%04X", addr);
            symbol_add(st, name, (unsigned short)addr, SYM_INSPECT, device);
        } else if (strncmp(p, "SIM_TRAP:", 9) == 0 &&
                   sscanf(p + 9, "%x:%127[^\n\r]", &addr, device) == 2) {
            char name[64];
            snprintf(name, sizeof(name), "trap_%04X", addr);
            symbol_add(st, name, (unsigned short)addr, SYM_TRAP, device);
        }
    }
    fclose(f);
}

bool load_toolchain_bundle(memory_t *mem, symbol_table_t *st, source_map_t *sm, const char *base_path, int *out_load_addr) {
    LOG_V2("DEBUG: ENTER load_toolchain_bundle mem=%p base=%s\n", (void*)mem, base_path);
    char path[512];

    int load_addr = 0x0801;
    int n = -1;

    /* Try .prg first (silently — not finding it is expected when only .asm exists) */
    snprintf(path, sizeof(path), "%s.prg", base_path);
    LOG_V2("DEBUG: Attempting to load PRG: %s\n", path);
    if (file_exists(path)) {
        n = load_prg(mem, path, &load_addr);
        LOG_V2("DEBUG: load_prg returned %d\n", n);
    }
    if (out_load_addr) *out_load_addr = load_addr;

    if (n < 0) {
        /* Try .bin */
        snprintf(path, sizeof(path), "%s.bin", base_path);
        if (file_exists(path))
            n = load_binary(mem, 0x0801, path);
    }

    if (n < 0) {
        /* Try assembling .asm with KickAssembler when no pre-built binary exists.
         * The jar is resolved relative to CWD (project root when running sim6502/sim6502-gui).
         *
         * Simulator pseudo-ops (.inspect, .trap) are preprocessed: each is replaced with a
         * KickAssembler .print statement that emits the current PC to stdout.  After assembly
         * we parse that output to register the pseudo-op addresses as SYM_INSPECT/SYM_TRAP
         * symbols, without KickAssembler ever needing to understand them. */
        char asm_path[512], prg_path[512];
        snprintf(asm_path, sizeof(asm_path), "%s.asm", base_path);
        snprintf(prg_path, sizeof(prg_path), "%s.prg", base_path);

        if (file_exists(asm_path)) {
            /* Preprocess for simulator pseudo-ops */
            char tmp_asm[512], tmp_out[512], tmp_sym[512];
            snprintf(tmp_asm, sizeof(tmp_asm), "%s.asm.tmp",     base_path);
            snprintf(tmp_out, sizeof(tmp_out), "%s.asm.tmp.out", base_path);
            /* KickAssembler derives the .sym name by stripping the last extension
             * from its input path, so "base.asm.tmp" → sym at "base.asm.sym". */
            snprintf(tmp_sym, sizeof(tmp_sym), "%s.asm.sym", base_path);

            int has_pseudoops = preprocess_asm_pseudoops(asm_path, tmp_asm);
            const char *src = has_pseudoops ? tmp_asm : asm_path;

            char cmd[2048];
            snprintf(cmd, sizeof(cmd),
                     "java -jar tools/KickAss65CE02.jar \"%s\" -symbolfile -o \"%s\" > \"%s\" 2>&1",
                     src, prg_path, tmp_out);
            LOG_V2("DEBUG: Invoking assembler: %s\n", cmd);
            int rc = system(cmd);

            if (rc == 0 && file_exists(prg_path)) {
                n = load_prg(mem, prg_path, &load_addr);
                if (out_load_addr) *out_load_addr = load_addr;
                if (has_pseudoops && st) apply_pseudoop_output(st, tmp_out);
                /* KickAssembler writes .sym next to its input file.  When using a temp
                 * file rename the result so normal .sym loading below finds it. */
                if (has_pseudoops && file_exists(tmp_sym)) {
                    char sym_dest[512];
                    snprintf(sym_dest, sizeof(sym_dest), "%s.sym", base_path);
                    rename(tmp_sym, sym_dest);
                }
            } else {
                fprintf(stderr, "Warning: assembly of '%s' failed (rc=%d)\n", asm_path, rc);
                if (g_verbose >= 1) {
                    FILE *ef = fopen(tmp_out, "r");
                    if (ef) {
                        char ebuf[512];
                        while (fgets(ebuf, sizeof(ebuf), ef))
                            fprintf(stderr, "  %s", ebuf);
                        fclose(ef);
                    }
                }
            }
            if (has_pseudoops) remove(tmp_asm);
            remove(tmp_out);
        }
    }

    if (n < 0) return false;

    load_companion_files(st, sm, base_path);
    return true;
}

void load_companion_files(symbol_table_t *st, source_map_t *sm, const char *base_path) {
    char path[512];

    /* Try .list (ACME-format source map) */
    snprintf(path, sizeof(path), "%s.list", base_path);
    if (sm) source_map_load_acme_list(sm, st, path);

    /* Try .sym (KickAssembler symbol file) */
    snprintf(path, sizeof(path), "%s.sym", base_path);
    if (st) symbol_load_file(st, path);

    /* Try .sym_add — supplemental annotations file that accepts both KickAssembler
     * symbol format (.label name=$addr) and simulator pseudo-op markers
     * (SIM_INSPECT:<hex>:<name>, SIM_TRAP:<hex>:<name>).  Both parsers run on the
     * same file; each handles the lines it understands and ignores the rest.
     * Useful for .prg/.bin files built by any assembler that need .inspect/.trap
     * annotations.  Produce one by redirecting KickAssembler stdout:
     *   java -jar tools/KickAss65CE02.jar prog.asm -symbolfile -o prog.prg > prog.sym_add */
    snprintf(path, sizeof(path), "%s.sym_add", base_path);
    if (st && file_exists(path)) {
        symbol_load_file(st, path);        /* picks up .label name=$addr entries */
        apply_pseudoop_output(st, path);   /* picks up SIM_INSPECT/SIM_TRAP lines */
    }
}
