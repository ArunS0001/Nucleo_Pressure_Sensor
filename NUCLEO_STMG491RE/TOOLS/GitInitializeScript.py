import os

TARGET_DIR = r"D:\0cc_views\1409_SEP\TEST_1409"
gitignore_path = os.path.join(TARGET_DIR, ".gitignore")

gitignore_content = """# Compilations and Build Outputs
build/
*.elf
*.bin
*.hex
*.map
*.o
*.d
*.su

# VS Code local states
.vscode/.history/
.vscode/ipch/
.vscode/*.log
"""

with open(gitignore_path, "w", encoding="utf-8") as f:
    f.write(gitignore_content.strip())
print("Successfully created .gitignore file!")

