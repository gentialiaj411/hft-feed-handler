import re
import os

def strip_comments(text):
    # Regex for C++ comments that respects string literals
    def replacer(match):
        s = match.group(0)
        if s.startswith('/'):
            # It's a comment
            return ""
        else:
            # It's a string or character literal
            return s

    pattern = re.compile(
        r'//.*?$|/\*.*?\*/|\'(?:\\.|[^\\\'])\'|"(?:\\.|[^\\"])*"',
        re.DOTALL | re.MULTILINE
    )
    return re.sub(pattern, replacer, text)

def process_directory(directory):
    extensions = {'.cpp', '.h', '.c', '.hpp'}
    for root, dirs, files in os.walk(directory):
        for file in files:
            if os.path.splitext(file)[1] in extensions:
                path = os.path.join(root, file)
                with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                
                stripped = strip_comments(content)
                
                # Further cleanup: remove excessive blank lines and trailing whitespace
                lines = [line.rstrip() for line in stripped.splitlines()]
                # Optional: Filter out empty lines if they are sequential
                final_lines = []
                last_empty = False
                for line in lines:
                    if line == "":
                        if not last_empty:
                            final_lines.append(line)
                            last_empty = True
                    else:
                        final_lines.append(line)
                        last_empty = False
                
                with open(path, 'w', encoding='utf-8') as f:
                    f.write('\n'.join(final_lines))
                print(f"Processed: {path}")

if __name__ == "__main__":
    target_dir = os.getcwd()
    print(f"Stripping comments in: {target_dir}")
    process_directory(target_dir)
