#!/usr/bin/env python3
"""
HTML Sync Validation Script
Checks if config_interface.html and swim_pacer.ino contain identical HTML content
"""

import re
import sys
from pathlib import Path

def extract_html_from_standalone():
    """Extract HTML content from standalone config_interface.html between sync markers"""
    html_file = Path("config_interface.html")
    if not html_file.exists():
        print("❌ config_interface.html not found")
        return None
    
    content = html_file.read_text(encoding='utf-8')
    
    # Extract content between sync markers
    start_marker = "<!-- ========== SYNC MARKER: START ESP32 HTML ========== -->"
    end_marker = "<!-- ========== SYNC MARKER: END ESP32 HTML ========== -->"
    
    start_idx = content.find(start_marker)
    end_idx = content.find(end_marker)
    
    if start_idx == -1 or end_idx == -1:
        print("❌ Sync markers not found in config_interface.html")
        return None
    
    # Extract HTML content after start marker, before end marker
    start_idx = content.find('\n', start_idx) + 1  # Start after marker line
    html_content = content[start_idx:end_idx].strip()
    
    return html_content

def extract_html_from_ino():
    """Extract HTML content from swim_pacer.ino handleRoot() function"""
    ino_file = Path("swim_pacer.ino")
    if not ino_file.exists():
        print("❌ swim_pacer.ino not found")
        return None
    
    content = ino_file.read_text(encoding='utf-8')
    
    # Find the handleRoot function and extract HTML between R"( and )"
    pattern = r'void handleRoot\(\) \{[\s\S]*?String html = R"\(\s*([\s\S]*?)\s*\)";'
    match = re.search(pattern, content)
    
    if not match:
        print("❌ Could not find HTML content in swim_pacer.ino handleRoot() function")
        return None
    
    html_content = match.group(1).strip()
    return html_content

def normalize_html(html_content):
    """Normalize HTML content for comparison (remove extra whitespace, etc.)"""
    if not html_content:
        return ""
    
    # Remove comments that aren't sync markers
    html_content = re.sub(r'<!--(?!.*SYNC MARKER).*?-->', '', html_content, flags=re.DOTALL)
    
    # Normalize whitespace
    html_content = re.sub(r'\s+', ' ', html_content)
    html_content = re.sub(r'>\s+<', '><', html_content)
    
    return html_content.strip()

def compare_html():
    """Compare HTML content from both files"""
    print("🔍 Checking HTML synchronization...")
    print("=" * 50)
    
    standalone_html = extract_html_from_standalone()
    ino_html = extract_html_from_ino()
    
    if standalone_html is None or ino_html is None:
        return False
    
    normalized_standalone = normalize_html(standalone_html)
    normalized_ino = normalize_html(ino_html)
    
    if normalized_standalone == normalized_ino:
        print("✅ HTML content is synchronized!")
        print(f"📊 Content length: {len(normalized_standalone)} characters")
        return True
    else:
        print("❌ HTML content is NOT synchronized!")
        print(f"📊 Standalone HTML: {len(normalized_standalone)} characters")
        print(f"📊 ESP32 INO HTML: {len(normalized_ino)} characters")
        
        # Show first difference
        for i, (a, b) in enumerate(zip(normalized_standalone, normalized_ino)):
            if a != b:
                start = max(0, i - 50)
                end = min(len(normalized_standalone), i + 50)
                print(f"\n📍 First difference at position {i}:")
                print(f"Standalone: ...{normalized_standalone[start:end]}...")
                print(f"ESP32:      ...{normalized_ino[start:end]}...")
                break
        
        return False

def main():
    """Main sync check function"""
    print("🏊‍♂️ Swim Pacer HTML Sync Checker")
    print("=" * 50)
    
    success = compare_html()
    
    if success:
        print("\n🎉 Files are in sync! Ready for development.")
        sys.exit(0)
    else:
        print("\n⚠️  Files need synchronization.")
        print("💡 Update the ESP32 code or standalone HTML to match.")
        sys.exit(1)

if __name__ == "__main__":
    main()