import os
import re

mnemonics = {'lda','ldx','ldy','ldz','sta','stx','sty','stz','stq','ldq','adc','sbc','cmp','cpx','cpy','cpz','cpq','bit','and','ora','eor','asl','lsr','rol','ror','inc','dec','inx','dex','iny','dey','inz','dez','inw','dew','asw','row','phw','phz','plz','pha','pla','phx','plx','phy','ply','php','plp','tax','txa','tay','tya','tsx','txs','taz','tza','tab','tba','tsy','tys','cle','see','neg','map','eom','rtn','bsr','lbsr','jsr','jmp','rts','rti','brk','nop','clc','sec','cld','sed','cli','sei','clv','bcc','bcs','beq','bne','bmi','bpl','bvc','bvs','bra','brl','lbra','lbcc','lbcs','lbeq','lbne','lbmi','lbpl','lbvc','lbvs', 'adcq', 'sbcq', 'andq', 'orq', 'eorq', 'bitq', 'cpq', 'aslq', 'lsrq', 'rolq', 'rorq', 'inq', 'deq'}

def convert_asm(file_path):
    print(f"Converting {file_path}...")
    with open(file_path, 'r') as f:
        lines = f.readlines()

    new_lines = []
    has_org = any('.org' in line or '* =' in line or '*=' in line for line in lines)
    if not has_org:
        new_lines.append('* = $0200\n')

    for line in lines:
        comment = ""
        orig_line = line
        if '//' in line:
            idx = line.find('//')
            comment = line[idx:]
            line = line[:idx]
        elif ';' in line:
            idx = line.find(';')
            comment = '//' + line[idx+1:]
            line = line[:idx]
        
        # CPU type
        line = line.replace('.processor 45gs02', '.cpu _45gs02')
        line = line.replace('.processor 6502', '.cpu _6502')
        line = line.replace('.processor 65c02', '.cpu _65c02')
        line = line.replace('.processor 65ce02', '.cpu _65ce02')
        line = line.replace('.target c64', '')
        line = line.replace('.target raw6502', '')
        line = line.replace('.target mega65', '')
        line = line.replace('.org', '* =')
        
        # 1. Handle [zp],z -> ((zp)),z
        line = re.sub(r'\[([^\]]+)\]', r'((\1))', line)
        
        line = line.lower()
        
        # 2. Handle ina/dea -> inc/dec (accumulator)
        line = re.sub(r'\bina\b', 'inc', line)
        line = re.sub(r'\bdea\b', 'dec', line)
        
        # 3. Handle (zp) -> (zp),z for specific instructions that use 65CE02 indirect mode
        # Only if NOT already followed by , or already part of double parents
        indirect_opcodes = {'lda', 'sta', 'adc', 'sbc', 'cmp', 'ora', 'and', 'eor', 'adcq', 'sbcq', 'andq', 'orq', 'eorq', 'ldq', 'stq'}
        for op in indirect_opcodes:
            # Match op followed by (zp) but not (zp), and not inside ((zp))
            # Negative lookbehind for ( to avoid ((zp)) -> ((zp)),z),z
            line = re.sub(rf'(?<!\()\b{op}\s+\(([^),]+)\)(?!\s*,)', rf'{op} (\1),z', line)

        # 4. Long branch
        line = re.sub(r'\bbrl\b', 'lbra', line)
        
        # 5. .import binary
        line = re.sub(r'\.bin\s+', '.import binary ', line)
        
        parts = line.split()
        if parts:
            is_indented = orig_line.startswith(' ') or orig_line.startswith('\t')
            first_lp = parts[0]
            
            # Label detection: not a mnemonic, not a directive, not indented
            if not is_indented and first_lp not in mnemonics and not first_lp.startswith('.') and not first_lp.startswith('*'):
                if not first_lp.endswith(':'):
                    parts[0] = first_lp + ':'
                else:
                    parts[0] = first_lp
            
            # Split multiple mnemonics per line
            final_parts = []
            for p in parts:
                if p in mnemonics:
                    if final_parts and not final_parts[-1].endswith(':'):
                        new_lines.append('    ' + ' '.join(final_parts) + '\n')
                        final_parts = []
                final_parts.append(p)
            
            line = '    ' + ' '.join(final_parts)
        else:
            line = ""
        
        if comment:
            line = line.rstrip() + ' ' + comment
        new_lines.append(line + '\n')

    with open(file_path, 'w') as f:
        f.writelines(new_lines)

for root, dirs, files in os.walk('tests'):
    for file in files:
        if file.endswith('.asm') and not file.endswith('.tmp.asm'):
            convert_asm(os.path.join(root, file))

print("Bulk conversion of tests completed.")
