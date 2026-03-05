#include "condition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int parse_mon_value(const char **pp, unsigned long *out) {
    const char *p = *pp;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return 0;
    char *end;
    unsigned long val;
    if (*p == '$') {
        if (!isxdigit((unsigned char)p[1])) return 0;
        val = strtoul(p + 1, &end, 16);
    } else if (*p == '%') {
        if (p[1] != '0' && p[1] != '1') return 0;
        val = strtoul(p + 1, &end, 2);
    } else if (isdigit((unsigned char)*p)) {
        val = strtoul(p, &end, 10);
    } else {
        return 0;
    }
    *pp = end;
    *out = val;
    return 1;
}

unsigned long get_reg_val(const char *name, cpu_t *cpu) {
    if      (strcasecmp(name, "A")  == 0) return cpu->a;
    else if (strcasecmp(name, "X")  == 0) return cpu->x;
    else if (strcasecmp(name, "Y")  == 0) return cpu->y;
    else if (strcasecmp(name, "Z")  == 0) return cpu->z;
    else if (strcasecmp(name, "B")  == 0) return cpu->b;
    else if (strcasecmp(name, "S")  == 0 || strcasecmp(name, "SP") == 0) return cpu->s;
    else if (strcasecmp(name, "P")  == 0) return cpu->p;
    else if (strcasecmp(name, "PC") == 0) return cpu->pc;
    else if (strcasecmp(name, ".C") == 0) return (cpu->p & FLAG_C) ? 1 : 0;
    else if (strcasecmp(name, ".Z") == 0) return (cpu->p & FLAG_Z) ? 1 : 0;
    else if (strcasecmp(name, ".I") == 0) return (cpu->p & FLAG_I) ? 1 : 0;
    else if (strcasecmp(name, ".D") == 0) return (cpu->p & FLAG_D) ? 1 : 0;
    else if (strcasecmp(name, ".B") == 0) return (cpu->p & FLAG_B) ? 1 : 0;
    else if (strcasecmp(name, ".V") == 0) return (cpu->p & FLAG_V) ? 1 : 0;
    else if (strcasecmp(name, ".N") == 0) return (cpu->p & FLAG_N) ? 1 : 0;
    return 0;
}

int evaluate_condition(const char *cond, cpu_t *cpu) {
    if (!cond || *cond == '\0') return 1;
    char buf[128]; strncpy(buf, cond, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
    char *saveptr;
    char *part = strtok_r(buf, "&&", &saveptr);
    while (part) {
        char left[32], op[8], right[32];
        if (sscanf(part, "%31s %7s %31s", left, op, right) == 3) {
            unsigned long lval = 0, rval = 0;
            const char *p = left;
            if (!parse_mon_value(&p, &lval)) lval = get_reg_val(left, cpu);
            p = right;
            if (!parse_mon_value(&p, &rval)) rval = get_reg_val(right, cpu);
            int ok = 0;
            if      (strcmp(op, "==") == 0) ok = (lval == rval);
            else if (strcmp(op, "!=") == 0) ok = (lval != rval);
            else if (strcmp(op, "<")  == 0) ok = (lval < rval);
            else if (strcmp(op, ">")  == 0) ok = (lval > rval);
            else if (strcmp(op, "<=") == 0) ok = (lval <= rval);
            else if (strcmp(op, ">=") == 0) ok = (lval >= rval);
            if (!ok) return 0;
        }
        part = strtok_r(NULL, "&&", &saveptr);
    }
    return 1;
}
