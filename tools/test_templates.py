#!/usr/bin/env python3
import os
import json
import subprocess
import tempfile
import shutil

SIM = "./sim6502"
KICKASS = "tools/KickAss65CE02.jar"

def test_template(template_path):
    print(f"Testing template: {os.path.basename(template_path)}...", end=" ", flush=True)
    
    with open(template_path, 'r') as f:
        tmpl = json.load(f)
    
    variables = tmpl.get('variables', {})
    template_id = os.path.splitext(os.path.basename(template_path))[0]
    
    with tempfile.TemporaryDirectory() as tmpdir:
        # 1. Create project using simulator env create
        # We'll simulate what ProjectManager::create_project does
        # or just run the simulator command if it supports it.
        # Actually, let's just use the simulator to create it.
        try:
            cmd = [SIM, "-c", f"env create {template_id} test_proj {tmpdir}", "-c", "quit"]
            subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        except Exception as e:
            print(f"FAIL (create error: {e})")
            return False
            
        # 2. Find any .asm files in the created project
        asm_files = []
        for root, dirs, files in os.walk(tmpdir):
            for file in files:
                if file.endswith('.asm'):
                    asm_files.append(os.path.join(root, file))
        
        if not asm_files:
            print("PASS (no asm to verify)")
            return True
            
        # 3. Try to assemble each .asm file with KickAss
        # Note: We might need to handle includes if the template uses them.
        for asm in asm_files:
            try:
                # Detect processor from variables or default to 6502
                # In actual usage, the .asm should have .cpu directive
                res = subprocess.run(['java', '-jar', KICKASS, asm, '-o', asm + ".prg"],
                                    capture_output=True, text=True, timeout=10)
                if res.returncode != 0:
                    print(f"FAIL (assembly error in {os.path.relpath(asm, tmpdir)})")
                    print(res.stdout)
                    print(res.stderr)
                    return False
            except Exception as e:
                print(f"FAIL (assembler error: {e})")
                return False
                
    print("PASS")
    return True

def main():
    if not os.path.exists("templates"):
        print("Templates directory not found.")
        return 1
        
    templates = [os.path.join("templates", f) for f in os.listdir("templates") if f.endswith(".json")]
    templates.sort()
    
    passed = 0
    for t in templates:
        if test_template(t):
            passed += 1
            
    print(f"\nTemplate tests: {passed}/{len(templates)} passed.")
    return 0 if passed == len(templates) else 1

if __name__ == "__main__":
    import sys
    sys.exit(main())
