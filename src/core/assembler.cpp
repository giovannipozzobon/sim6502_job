#include "assembler.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <strings.h>

int is_branch_opcode(const char *op) {
	if (strcasecmp(op, "BCC") == 0 || strcasecmp(op, "BCS") == 0 || 
	    strcasecmp(op, "BEQ") == 0 || strcasecmp(op, "BNE") == 0 ||
	    strcasecmp(op, "BMI") == 0 || strcasecmp(op, "BPL") == 0 ||
	    strcasecmp(op, "BVC") == 0 || strcasecmp(op, "BVS") == 0 ||
	    strcasecmp(op, "BRA") == 0 || strcasecmp(op, "BSR") == 0 ||
	    strcasecmp(op, "BRL") == 0)
		return 1;
    if (strcasecmp(op, "LBCC") == 0 || strcasecmp(op, "LBCS") == 0 || 
	    strcasecmp(op, "LBEQ") == 0 || strcasecmp(op, "LBNE") == 0 ||
	    strcasecmp(op, "LBMI") == 0 || strcasecmp(op, "LBPL") == 0 ||
	    strcasecmp(op, "LBVC") == 0 || strcasecmp(op, "LBVS") == 0 ||
	    strcasecmp(op, "LBRA") == 0 || strcasecmp(op, "LBSR") == 0 ||
        strcasecmp(op, "LBRL") == 0)
        return 1;
	return 0;
}

int is_long_branch_opcode(const char *op) {
	if (toupper((unsigned char)op[0]) == 'L' && (strcasecmp(op, "LBRL") != 0)) {
		if (strcasecmp(op, "LBCC") == 0 || strcasecmp(op, "LBCS") == 0 || 
		    strcasecmp(op, "LBEQ") == 0 || strcasecmp(op, "LBNE") == 0 ||
		    strcasecmp(op, "LBMI") == 0 || strcasecmp(op, "LBPL") == 0 ||
		    strcasecmp(op, "LBVC") == 0 || strcasecmp(op, "LBVS") == 0 ||
		    strcasecmp(op, "LBRA") == 0)
			return 1;
	}
	if (strcasecmp(op, "BSR") == 0) return 1;
	if (strcasecmp(op, "BRL") == 0) return 1;
	if (strcasecmp(op, "LBSR") == 0) return 1;
	if (strcasecmp(op, "LBRL") == 0) return 1;
	return 0;
}

unsigned short parse_value(const char *str, int *out_digits) {
	unsigned short val;
	int digits;
	if (str[0] == '$') {
		const char *start = str + 1;
		val = (unsigned short)strtol(start, NULL, 16);
		digits = 0;
		while (isxdigit(start[digits])) digits++;
	} else if (str[0] == '%') {
		val = (unsigned short)strtol(str + 1, NULL, 2);
		digits = (val <= 0xFF) ? 2 : 4;
	} else if (str[0] == '\'') {
		val = (unsigned short)(unsigned char)str[1];
		digits = 2;
	} else {
		val = (unsigned short)strtol(str, NULL, 10);
		digits = (val <= 0xFF) ? 2 : 4;
	}
	if (out_digits) *out_digits = digits;
	return val;
}

int load_binary_to_mem(memory_t *mem, int addr, const char *filename) {
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
				mem->mem[addr + i] = (unsigned char)c;
		}
	}
	fclose(bf);
	return (int)size;
}

static bool is_device_available(machine_type_t machine, const char* name) {
    if (strcmp(name, "vic2") == 0) return (machine != MACHINE_RAW6502 && machine != MACHINE_X16);
    if (strcmp(name, "mega65_math") == 0) return (machine == MACHINE_MEGA65);
    if (strcmp(name, "mega65_dma") == 0) return (machine == MACHINE_MEGA65);
    if (strcmp(name, "sid") == 0) return (machine != MACHINE_RAW6502 && machine != MACHINE_X16);
    return true; // Generic or unknown device
}

static const char* machine_name_local(machine_type_t m) {
    switch (m) {
        case MACHINE_RAW6502: return "raw6502";
        case MACHINE_C64: return "c64";
        case MACHINE_C128: return "c128";
        case MACHINE_MEGA65: return "mega65";
        case MACHINE_X16: return "x16";
        default: return "unknown";
    }
}

bool handle_pseudo_op(const char *line, machine_type_t *machine_type, cpu_type_t *cpu_type, int *pc,
                              memory_t *mem, symbol_table_t *symbols,
                              struct source_stack *ss) {
    (void)ss;
	while (*line && isspace(*line)) line++;
	if (*line != '.') return true;
	line++;
	if (strncmp(line, "target", 6) == 0 && (!line[6] || isspace(line[6]))) {
		line += 6;
		while (*line && isspace(*line)) line++;
		if (machine_type && cpu_type) {
			if (strncmp(line, "raw6502", 7) == 0)      { *machine_type = MACHINE_RAW6502; *cpu_type = CPU_6502; }
			else if (strncmp(line, "c64", 3) == 0)     { *machine_type = MACHINE_C64;     *cpu_type = CPU_6502; }
			else if (strncmp(line, "c128", 4) == 0)    { *machine_type = MACHINE_C128;    *cpu_type = CPU_6502; }
			else if (strncmp(line, "mega65", 6) == 0)  { *machine_type = MACHINE_MEGA65;  *cpu_type = CPU_45GS02; }
			else if (strncmp(line, "x16", 3) == 0)     { *machine_type = MACHINE_X16;     *cpu_type = CPU_65C02; }
		}
		return true;
	}
	if (strncmp(line, "requires", 8) == 0 && (!line[8] || isspace(line[8]))) {
		line += 8;
		while (*line && isspace(*line)) line++;
		char device[64]; int j = 0;
		while (*line && !isspace(*line) && *line != ';' && j < 63) device[j++] = *line++;
		device[j] = '\0';
		if (machine_type && !is_device_available(*machine_type, device)) {
			fprintf(stderr, "Error: Machine '%s' does not provide required device '%s'\n",
			        machine_name_local(*machine_type), device);
			return false;
		}
		return true;
	}
	if (strncmp(line, "inspect", 7) == 0 && (!line[7] || isspace(line[7]))) {
		line += 7;
		while (*line && isspace(*line)) line++;
		if (symbols) {
			char name[64]; int j = 0;
			if (*line == '"') {
				line++;
				while (*line && *line != '"' && j < 63) name[j++] = *line++;
				if (*line == '"') line++;
			} else {
				while (*line && !isspace(*line) && *line != ';' && j < 63) name[j++] = *line++;
			}
			name[j] = '\0';
			symbol_add(symbols, name, (unsigned short)*pc, SYM_INSPECT, "Inspect device/memory");
		}
		return true;
	}
	if (strncmp(line, "processor", 9) == 0 && (!line[9] || isspace(line[9]))) {
		line += 9;
		while (*line && isspace(*line)) line++;
		if (strncmp(line, "45gs02", 6) == 0 || strncmp(line, "45GS02", 6) == 0)
			*cpu_type = CPU_45GS02;
		else if (strncmp(line, "65ce02", 6) == 0 || strncmp(line, "65CE02", 6) == 0)
			*cpu_type = CPU_65CE02;
		else if (strncmp(line, "65c02", 5) == 0 || strncmp(line, "65C02", 5) == 0)
			*cpu_type = CPU_65C02;
		else if (strncmp(line, "6502", 4) == 0) {
			line += 4;
			while (*line && isspace(*line)) line++;
			if (strncmp(line, "undoc", 5) == 0)
				*cpu_type = CPU_6502_UNDOCUMENTED;
			else
				*cpu_type = CPU_6502;
		}
		return true;
	}
	if (strncmp(line, "org", 3) == 0 && (!line[3] || isspace(line[3]))) {
		line += 3;
		while (*line && isspace(*line)) line++;
		*pc = (int)parse_value(line, NULL);
		return true;
	}
	if (strncmp(line, "byte", 4) == 0 && (!line[4] || isspace(line[4]))) {
		line += 4;
		while (*line && isspace(*line)) line++;
		while (*line && *line != ';' && *line != '\n' && *line != '\r') {
			while (*line && isspace(*line)) line++;
			if (!*line || *line == ';' || *line == '\n' || *line == '\r') break;
			unsigned char val;
			if (isalpha((unsigned char)*line) || *line == '_') {
				char lbl[64]; int j = 0;
				while ((isalnum((unsigned char)*line) || *line == '_') && j < 63)
					lbl[j++] = *line++;
				lbl[j] = 0;
				unsigned short addr = 0;
				if (symbols) symbol_lookup_name(symbols, lbl, &addr);
				val = (unsigned char)(addr & 0xFF);
			} else {
				val = (unsigned char)parse_value(line, NULL);
				while (*line && !isspace((unsigned char)*line) &&
				       *line != ',' && *line != ';') line++;
			}
			if (mem) mem->mem[(*pc)++] = val;
			else (*pc)++;
			while (*line && isspace((unsigned char)*line)) line++;
			if (*line == ',') line++;
		}
		return true;
	}
	if (strncmp(line, "word", 4) == 0 && (!line[4] || isspace(line[4]))) {
		line += 4;
		while (*line && isspace(*line)) line++;
		while (*line && *line != ';' && *line != '\n' && *line != '\r') {
			while (*line && isspace(*line)) line++;
			if (!*line || *line == ';' || *line == '\n' || *line == '\r') break;
			unsigned short val;
			if (isalpha((unsigned char)*line) || *line == '_') {
				char lbl[64]; int j = 0;
				while ((isalnum((unsigned char)*line) || *line == '_') && j < 63)
					lbl[j++] = *line++;
				lbl[j] = 0;
				val = 0;
				if (symbols) symbol_lookup_name(symbols, lbl, &val);
			} else {
				val = parse_value(line, NULL);
				while (*line && !isspace((unsigned char)*line) &&
				       *line != ',' && *line != ';') line++;
			}
			if (mem) {
				mem->mem[(*pc)++] = val & 0xFF;
				mem->mem[(*pc)++] = (val >> 8) & 0xFF;
			} else *pc += 2;
			while (*line && isspace((unsigned char)*line)) line++;
			if (*line == ',') line++;
		}
		return true;
	}
	if (strncmp(line, "text", 4) == 0 && (!line[4] || isspace(line[4]))) {
		line += 4;
		while (*line && isspace(*line)) line++;
		if (*line == '"') {
			line++;
			while (*line && *line != '"') {
				if (mem) mem->mem[(*pc)++] = (unsigned char)*line;
				else (*pc)++;
				line++;
			}
		}
		return true;
	}
	if (strncmp(line, "align", 5) == 0 && (!line[5] || isspace(line[5]))) {
		line += 5;
		while (*line && isspace(*line)) line++;
		int alignment = (int)parse_value(line, NULL);
		if (alignment > 0) {
			int offset = (*pc) % alignment;
			if (offset != 0) *pc += (alignment - offset);
		}
		return true;
	}
	if (strncmp(line, "bin", 3) == 0 && (!line[3] || isspace(line[3]))) {
		line += 3;
		while (*line && isspace(*line)) line++;
		if (*line == '"') {
			line++;
			char fname[256]; int j = 0;
			while (*line && *line != '"' && j < 255) fname[j++] = *line++;
			fname[j] = 0;
			int n = load_binary_to_mem(mem, *pc, fname);
			if (n > 0) *pc += n;
		}
		return true;
	}
	return false;
}

void parse_line(const char *line, instruction_t *instr, symbol_table_t *symbols, int pc) {
	int i = 0;
	const char *semi  = strchr(line, ';');
	const char *colon = strchr(line, ':');
	if (colon && (!semi || colon < semi)) line = colon + 1;
	while (*line && isspace(*line)) line++;
	if (!*line || *line == ';' || *line == '.') {
		instr->op[0] = 0;
		return;
	}
	while (*line && !isspace(*line) && i < (int)sizeof(instr->op) - 1)
		instr->op[i++] = toupper(*line++);
	instr->op[i] = 0;
	while (*line && isspace(*line)) line++;
	instr->mode = MODE_IMPLIED;
	instr->arg = 0;
	instr->arg_overflow = 0;
	int is_branch = is_branch_opcode(instr->op);
	if (*line == '#') {
		if (strcmp(instr->op, "PHW") == 0) instr->mode = MODE_IMMEDIATE_WORD;
		else instr->mode = MODE_IMMEDIATE;
		line++;
		instr->arg = parse_value(line, NULL);
		if (instr->mode == MODE_IMMEDIATE && instr->arg > 0xFF)
			instr->arg_overflow = 1;
	} else if (*line == '$' || *line == '%' || *line == '\'' || isdigit(*line) || *line == '*') {
		int digits = 4;
		if (*line == '*') {
			instr->arg = (unsigned short)pc;
			line++;
			if (*line == '+') { line++; instr->arg += parse_value(line, NULL); }
			else if (*line == '-') { line++; instr->arg -= parse_value(line, NULL); }
		} else {
			instr->arg = parse_value(line, &digits);
		}
		
		/* Robust detection of indexed mode */
		int idx_x = 0, idx_y = 0;
		const char *p = line;
        if (*p == '$' || *p == '%' || *p == '\'' || *p == '*') p++;
        while (*p && (isxdigit((unsigned char)*p) || isdigit((unsigned char)*p))) p++;
		while (*p && isspace((unsigned char)*p)) p++;
		if (*p == ',') {
			p++;
			while (*p && isspace((unsigned char)*p)) p++;
			if (toupper((unsigned char)*p) == 'X') idx_x = 1;
			else if (toupper((unsigned char)*p) == 'Y') idx_y = 1;
		}

		if (idx_x) instr->mode = (digits <= 2) ? MODE_ZP_X : MODE_ABSOLUTE_X;
		else if (idx_y) instr->mode = (digits <= 2) ? MODE_ZP_Y : MODE_ABSOLUTE_Y;
		else if (is_branch) {
			unsigned short target = instr->arg;
			if (is_long_branch_opcode(instr->op)) {
				instr->mode = MODE_RELATIVE_LONG;
				instr->arg = (unsigned short)(target - (pc + 3));
			} else {
				instr->mode = MODE_RELATIVE;
				instr->arg = (unsigned short)(target - (pc + 2));
			}
		} else instr->mode = (digits <= 2) ? MODE_ZP : MODE_ABSOLUTE;
	} else if (*line == '(') {
		line++;
		if (*line == '$' || *line == '%' || *line == '\'' || isdigit(*line)) {
			instr->arg = parse_value(line, NULL);
			while (*line && *line != ')' && *line != ',') line++;
			if (*line == ',') {
				line++;
				while (*line && isspace(*line)) line++;
				if (toupper(*line) == 'X') {
					if (instr->arg > 0xFF) instr->mode = MODE_ABS_INDIRECT_X;
					else instr->mode = MODE_INDIRECT_X;
				} else if (toupper(*line) == 'S') {
					if (toupper(line[1]) == 'P') line += 2; else line++;
					while (*line && isspace(*line)) line++;
					if (*line == ')') {
						line++;
						while (*line && isspace(*line)) line++;
						if (*line == ',') {
							line++;
							while (*line && isspace(*line)) line++;
							if (toupper(*line) == 'Y') instr->mode = MODE_SP_INDIRECT_Y;
						}
					}
				}
			} else if (*line == ')') {
				line++;
				while (*line && isspace(*line)) line++;
				if (*line == ',') {
					line++;
					while (*line && isspace(*line)) line++;
					if (toupper(*line) == 'Y') instr->mode = (instr->arg > 0xFF) ? MODE_ABS_INDIRECT_Y : MODE_INDIRECT_Y;
					else if (toupper(*line) == 'Z') instr->mode = MODE_ZP_INDIRECT_Z;
				} else {
					if (instr->arg > 0xFF) instr->mode = MODE_INDIRECT;
					else instr->mode = MODE_ZP_INDIRECT;
				}
			}
		} else if (isalpha(*line) || *line == '_') {
			char lbl[64]; int j = 0;
			while ((*line && (isalnum(*line) || *line == '_')) && j < 63) lbl[j++] = *line++;
			lbl[j] = 0;
			instr->arg = 0;
			if (symbols) symbol_lookup_name(symbols, lbl, &instr->arg);
			while (*line && isspace(*line)) line++;
			if (*line == ',') {
				line++;
				while (*line && isspace(*line)) line++;
				if (toupper(*line) == 'X') {
					if (instr->arg > 0xFF) instr->mode = MODE_ABS_INDIRECT_X;
					else instr->mode = MODE_INDIRECT_X;
				} else if (toupper(*line) == 'S') {
					if (toupper(line[1]) == 'P') line += 2; else line++;
					while (*line && isspace(*line)) line++;
					if (*line == ')') {
						line++;
						while (*line && isspace(*line)) line++;
						if (*line == ',') {
							line++;
							while (*line && isspace(*line)) line++;
							if (toupper(*line) == 'Y') instr->mode = MODE_SP_INDIRECT_Y;
						}
					}
				}
			} else if (*line == ')') {
				line++;
				while (*line && isspace(*line)) line++;
				if (*line == ',') {
					line++;
					while (*line && isspace(*line)) line++;
					if (toupper(*line) == 'Y') instr->mode = (instr->arg > 0xFF) ? MODE_ABS_INDIRECT_Y : MODE_INDIRECT_Y;
					else if (toupper(*line) == 'Z') instr->mode = MODE_ZP_INDIRECT_Z;
				} else {
					if (instr->arg > 0xFF) instr->mode = MODE_INDIRECT;
					else instr->mode = MODE_ZP_INDIRECT;
				}
			}
		}
	} else if (*line == '[') {
		line++;
		if (*line == '$' || *line == '%' || *line == '\'' || isdigit(*line)) {
			instr->arg = parse_value(line, NULL);
			while (*line && *line != ']') line++;
			if (*line == ']') line++;
			while (*line && isspace(*line)) line++;
			if (*line == ',') {
				line++;
				while (*line && isspace(*line)) line++;
				if (toupper(*line) == 'Z') instr->mode = MODE_ZP_INDIRECT_Z_FLAT;
			} else instr->mode = MODE_ZP_INDIRECT_FLAT;
		}
	} else if (instr->op[0]) {
		if (isalpha(*line) || *line == '_' || *line == '.') {
			char label[64]; int j = 0;
			if (*line == '.' && j < 63) label[j++] = *line++;
			while ((*line && (isalnum(*line) || *line == '_')) && j < 63) label[j++] = *line++;
			label[j] = 0;
			
			int idx_x = 0, idx_y = 0;
			const char *p = line;
			while (*p && isspace((unsigned char)*p)) p++;
			if (*p == ',') {
				p++;
				while (*p && isspace((unsigned char)*p)) p++;
				if (toupper((unsigned char)*p) == 'X') idx_x = 1;
				else if (toupper((unsigned char)*p) == 'Y') idx_y = 1;
			}

			if (is_branch) {
				if (is_long_branch_opcode(instr->op)) instr->mode = MODE_RELATIVE_LONG;
				else                                  instr->mode = MODE_RELATIVE;
                /* Advance past label */
                while (*line && !isspace((unsigned char)*line) && *line != ';') line++;
			} else {
				if      (idx_x) instr->mode = MODE_ABSOLUTE_X;
				else if (idx_y) instr->mode = MODE_ABSOLUTE_Y;
				else            instr->mode = MODE_ABSOLUTE;
			}

			if (symbols) {
				unsigned short addr;
				if (symbol_lookup_name(symbols, label, &addr)) {
					if (instr->mode == MODE_RELATIVE) instr->arg = (unsigned short)(addr - (pc + 2));
					else if (instr->mode == MODE_RELATIVE_LONG) instr->arg = (unsigned short)(addr - (pc + 3));
					else {
						instr->arg = addr;
						/* NOTE: We always keep label-based operands as ABSOLUTE (3 bytes)
						 * to ensure PC consistency between Pass 1 and Pass 2.
						 * Zero-page optimization is only for literal $00-$FF. */
					}
				} else instr->arg = 0;
			}
		} else instr->mode = MODE_IMPLIED;
	}
}

int encode_to_mem(memory_t *mem, int pc_base, const instruction_t *instr, const opcode_handler_t *handlers, int n, cpu_type_t cpu_type) {
	if (instr->arg_overflow) return -1;
	if (strcmp(instr->op, "BRK") == 0) {
		mem->mem[pc_base] = 0x00;
		if (cpu_type != CPU_45GS02) { mem->mem[pc_base + 1] = 0x00; return 2; }
		return 1;
	}
	int pc = pc_base;
	int i;
	for (i = 0; i < n; i++) {
		if (strcmp(instr->op, handlers[i].mnemonic) == 0 && handlers[i].mode == instr->mode && handlers[i].opcode_len > 0) break;
	}
	instruction_t adjusted_instr = *instr;
	if (i >= n && (instr->mode == MODE_RELATIVE || instr->mode == MODE_RELATIVE_LONG)) {
		unsigned char other_mode = (instr->mode == MODE_RELATIVE) ? MODE_RELATIVE_LONG : MODE_RELATIVE;
		for (i = 0; i < n; i++) {
			if (strcmp(instr->op, handlers[i].mnemonic) == 0 && handlers[i].mode == other_mode && handlers[i].opcode_len > 0) {
				int orig_len = (instr->mode == MODE_RELATIVE) ? 2 : 3;
				int new_len = (other_mode == MODE_RELATIVE) ? 2 : 3;
				unsigned short target = (unsigned short)(pc_base + orig_len + (short)instr->arg);
				adjusted_instr.mode = other_mode;
				adjusted_instr.arg = (unsigned short)(target - (pc_base + new_len));
				break;
			}
		}
	}
	if (i >= n) return -1;
	const instruction_t *final_instr = (i < n && adjusted_instr.mode != instr->mode) ? &adjusted_instr : instr;
	unsigned char olen = handlers[i].opcode_len;
	for (int j = 0; j < olen; j++) mem->mem[pc_base + j] = handlers[i].opcode_bytes[j];
	pc = pc_base + olen;
	switch (handlers[i].mode) {
	case MODE_IMPLIED: break;
	case MODE_IMMEDIATE: case MODE_ZP: case MODE_ZP_X: case MODE_ZP_Y:
	case MODE_INDIRECT_X: case MODE_INDIRECT_Y:
	case MODE_ZP_INDIRECT: case MODE_ZP_INDIRECT_Z:
	case MODE_ZP_INDIRECT_Z_FLAT: case MODE_ZP_INDIRECT_FLAT:
	case MODE_SP_INDIRECT_Y: case MODE_RELATIVE:
		mem->mem[pc++] = final_instr->arg & 0xFF; break;
	case MODE_ABSOLUTE: case MODE_ABSOLUTE_X: case MODE_ABSOLUTE_Y:
	case MODE_INDIRECT: case MODE_ABS_INDIRECT_X: case MODE_ABS_INDIRECT_Y:
	case MODE_RELATIVE_LONG: case MODE_IMMEDIATE_WORD:
		mem->mem[pc++] = final_instr->arg & 0xFF; mem->mem[pc++] = (final_instr->arg >> 8) & 0xFF; break;
	}
	return pc - pc_base;
}

const char *mode_name(unsigned char mode) {
	switch (mode) {
	case MODE_IMPLIED:            return "implied";
	case MODE_IMMEDIATE:          return "immediate";
	case MODE_IMMEDIATE_WORD:     return "immediate_word";
	case MODE_ZP:                 return "zp";
	case MODE_ZP_X:               return "zp_x";
	case MODE_ZP_Y:               return "zp_y";
	case MODE_INDIRECT_X:         return "indirect_x";
	case MODE_INDIRECT_Y:         return "indirect_y";
	case MODE_ZP_INDIRECT:        return "zp_indirect";
	case MODE_ZP_INDIRECT_Z:      return "zp_indirect_z";
	case MODE_ZP_INDIRECT_FLAT:   return "zp_indirect_flat";
	case MODE_ZP_INDIRECT_Z_FLAT: return "zp_indirect_z_flat";
	case MODE_SP_INDIRECT_Y:      return "sp_indirect_y";
	case MODE_RELATIVE:           return "relative";
	case MODE_RELATIVE_LONG:      return "relative_long";
	case MODE_ABSOLUTE:           return "absolute";
	case MODE_ABSOLUTE_X:         return "absolute_x";
	case MODE_ABSOLUTE_Y:         return "absolute_y";
	case MODE_INDIRECT:           return "indirect";
	case MODE_ABS_INDIRECT_X:     return "abs_indirect_x";
	case MODE_ABS_INDIRECT_Y:     return "abs_indirect_y";
	default:                      return "unknown";
	}
}

const char *mode_operand_template(unsigned char mode) {
	switch (mode) {
	case MODE_IMPLIED:            return "";
	case MODE_IMMEDIATE:          return " #$vv";
	case MODE_IMMEDIATE_WORD:     return " #$vvvv";
	case MODE_ZP:                 return " $vv";
	case MODE_ZP_X:               return " $vv,X";
	case MODE_ZP_Y:               return " $vv,Y";
	case MODE_INDIRECT_X:         return " ($vv,X)";
	case MODE_INDIRECT_Y:         return " ($vv),Y";
	case MODE_ZP_INDIRECT:        return " ($vv)";
	case MODE_ZP_INDIRECT_Z:      return " ($vv),Z";
	case MODE_ZP_INDIRECT_FLAT:   return " [$vv]";
	case MODE_ZP_INDIRECT_Z_FLAT: return " [$vv],Z";
	case MODE_SP_INDIRECT_Y:      return " ($vv,SP),Y";
	case MODE_RELATIVE:           return " $vvvv";
	case MODE_RELATIVE_LONG:      return " $vvvv";
	case MODE_ABSOLUTE:           return " $vvvv";
	case MODE_ABSOLUTE_X:         return " $vvvv,X";
	case MODE_ABSOLUTE_Y:         return " $vvvv,Y";
	case MODE_INDIRECT:           return " ($vvvv)";
	case MODE_ABS_INDIRECT_X:     return " ($vvvv,X)";
	case MODE_ABS_INDIRECT_Y:     return " ($vvvv),Y";
	default:                      return " ???";
	}
}

int get_instruction_length(unsigned char mode) {
	switch (mode) {
	case MODE_IMPLIED:            return 1;
	case MODE_IMMEDIATE:          return 2;
	case MODE_ZP:                 return 2;
	case MODE_ZP_X:               return 2;
	case MODE_ZP_Y:               return 2;
	case MODE_INDIRECT_X:         return 2;
	case MODE_INDIRECT_Y:         return 2;
	case MODE_ZP_INDIRECT:        return 2;
	case MODE_ZP_INDIRECT_Z:      return 2;
	case MODE_ZP_INDIRECT_FLAT:   return 2;
	case MODE_ZP_INDIRECT_Z_FLAT: return 2;
	case MODE_SP_INDIRECT_Y:      return 2;
	case MODE_RELATIVE:           return 2;
	case MODE_ABSOLUTE:           return 3;
	case MODE_ABSOLUTE_X:         return 3;
	case MODE_ABSOLUTE_Y:         return 3;
	case MODE_INDIRECT:           return 3;
	case MODE_ABS_INDIRECT_X:     return 3;
	case MODE_ABS_INDIRECT_Y:     return 3;
	case MODE_RELATIVE_LONG:      return 3;
	case MODE_IMMEDIATE_WORD:     return 3;
	default:                      return 1;
	}
}

int get_encoded_length(const char *op, unsigned char mode, const opcode_handler_t *handlers, int n, cpu_type_t cpu_type) {
	if (strcmp(op, "BRK") == 0) {
		return (cpu_type == CPU_45GS02) ? 1 : 2;
	}
	for (int i = 0; i < n; i++) {
		if (strcmp(op, handlers[i].mnemonic) == 0 && handlers[i].mode == mode && handlers[i].opcode_len > 0) {
			int instr_bytes = get_instruction_length(mode);
			return (int)handlers[i].opcode_len - 1 + instr_bytes;
		}
	}
	/* Fallback for relative modes */
	if (mode == MODE_RELATIVE || mode == MODE_RELATIVE_LONG) {
		unsigned char other = (mode == MODE_RELATIVE) ? MODE_RELATIVE_LONG : MODE_RELATIVE;
		for (int i = 0; i < n; i++) {
			if (strcmp(op, handlers[i].mnemonic) == 0 && handlers[i].mode == other && handlers[i].opcode_len > 0) {
				return (int)handlers[i].opcode_len - 1 + get_instruction_length(other);
			}
		}
	}
	return -1;
}
