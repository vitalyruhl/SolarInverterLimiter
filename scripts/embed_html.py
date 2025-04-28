Import("env")
import os

def convert_html_to_header():
    # Pfade aus build_flags lesen
    src_path = os.path.join(env['PROJECT_DIR'], env['BUILD_FLAGS']['HTML_SOURCE_PATH'])
    dst_path = os.path.join(env['PROJECT_DIR'], env['BUILD_FLAGS']['HTML_OUTPUT_PATH'])
    
    # HTML-Datei lesen
    with open(src_path, 'r') as f:
        html = f.read()
    
    # Header-Datei generieren
    with open(dst_path, 'w') as f:
        f.write('#pragma once\n#include <Arduino.h>\n\n')
        f.write('const char WEB_HTML[] PROGMEM = R"=====(\n')
        f.write(html)
        f.write('\n)=====";')
    print(f"HTML converted: {src_path} -> {dst_path}")

# Hook vor dem Build einfügen
env.AddPreAction("build", convert_html_to_header)