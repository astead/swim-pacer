#!/usr/bin/env python3
"""
Update ESP32 HTML content from standalone HTML file
"""

import re

def extract_html_content(file_path):
    """Read the entire standalone HTML file"""
    with open(file_path, 'r', encoding='utf-8') as f:
        return f.read().strip()

def update_esp32_html(ino_file, html_content):
    """Update the ESP32 .ino file with new HTML content"""
    with open(ino_file, 'r', encoding='utf-8') as f:
        ino_content = f.read()

    # Find the R"( and )" boundaries around the HTML
    start_pattern = r'String html = R"\('
    end_pattern = r'\)";'

    # Find the start of the HTML raw string
    start_match = re.search(start_pattern, ino_content)
    if not start_match:
        raise ValueError("Could not find HTML string start in ESP32 file")

    start_pos = start_match.end()

    # Find the end of the HTML raw string (look for )"; after the end marker)
    end_marker_in_ino = ino_content.find("<!-- ========== SYNC MARKER: END ESP32 HTML ========== -->", start_pos)
    if end_marker_in_ino == -1:
        raise ValueError("Could not find end marker in ESP32 file")

    # Find the closing )"; after the end marker
    closing_pos = ino_content.find(')";\n', end_marker_in_ino)
    if closing_pos == -1:
        raise ValueError("Could not find HTML string end in ESP32 file")

    # Replace the HTML content
    new_content = (
        ino_content[:start_pos] +
        "\n" + html_content + "\n" +
        ino_content[closing_pos:]
    )

    # Write back the updated content
    with open(ino_file, 'w', encoding='utf-8') as f:
        f.write(new_content)

def main():
    standalone_html = "config_interface.html"
    esp32_ino = "swim_pacer.ino"

    try:
        print("Extracting HTML content from standalone file...")
        html_content = extract_html_content(standalone_html)

        print(f"Extracted {len(html_content)} characters")

        print("Updating ESP32 .ino file...")
        update_esp32_html(esp32_ino, html_content)

        print("✓ Successfully updated ESP32 HTML content!")
        print("The files should now be synchronized.")

    except Exception as e:
        print(f"✗ Error: {e}")
        return 1

    return 0

if __name__ == "__main__":
    exit(main())