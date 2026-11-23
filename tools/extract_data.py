#!/usr/bin/env python3
"""
Extract DATA_* byte arrays from NASM assembly into binary files.
Generates a C++ header with extern declarations and offsets.
"""

import re
import sys
import os
from pathlib import Path
from collections import OrderedDict

def parse_nasm_file(filepath):
    """Parse NASM assembly file and extract DATA_ labels with their byte arrays"""
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    data_blocks = OrderedDict()
    current_label = None
    current_bytes = []
    
    db_pattern = re.compile(r'^\s*db\s+(.+)$', re.IGNORECASE)
    dd_pattern = re.compile(r'^\s*dd\s+(.+)$', re.IGNORECASE)
    dw_pattern = re.compile(r'^\s*dw\s+(.+)$', re.IGNORECASE)
    times_pattern = re.compile(r'^\s*times\s+(\d+)\s+db\s+(.+)$', re.IGNORECASE)

    label_pattern = re.compile(r'^(DATA_\d+)\s*:', re.IGNORECASE)
    global_pattern = re.compile(r'^GLOBAL\s+(DATA_\d+)', re.IGNORECASE)
    
    global_data = set()
    
    for line in lines:
        # Track GLOBAL declarations
        gm = global_pattern.match(line)
        if gm:
            global_data.add(gm.group(1))
        
        # Check for new label
        lm = label_pattern.match(line)
        if lm:
            # Save previous block
            if current_label and current_bytes:
                data_blocks[current_label] = bytes(current_bytes)
            
            current_label = lm.group(1)
            current_bytes = []
            continue
        
        if current_label is None:
            continue
        
        # Skip non-data lines
        if not line.strip() or ';' == line.strip()[0]:
            continue
        
        # Check for end of data section (next section change or code)
        if any(line.startswith(k) for k in ['SECTION', 'BITS', 'EXTERN', 'GLOBAL', 'FUNC_', 'JUMP_', '.']):
            if 'SECTION' in line or 'BITS' in line or 'EXTERN' in line or 'GLOBAL' in line:
                continue
        
        # Parse db statements
        dbm = db_pattern.match(line)
        if dbm:
            values_str = dbm.group(1)
            values = parse_values(values_str)
            current_bytes.extend(values)
            continue
        
        # Parse dd statements
        ddm = dd_pattern.match(line)
        if ddm:
            values_str = ddm.group(1)
            values = parse_dd_values(values_str)
            current_bytes.extend(values)

            continue
        # Handle dw (define word - 2 bytes each)
        dwm = dw_pattern.match(line)
        if dwm:
            values_str = dwm.group(1)
            values = parse_dw_values(values_str)
            current_bytes.extend(values)
            continue

        # Handle times N db value (repeat byte N times)
        tm = times_pattern.match(line)
        if tm:
            count = int(tm.group(1))
            value_str = tm.group(2)
            values = parse_times_values(value_str, count)
            current_bytes.extend(values)

            continue
        
        # If we hit code-like content while in a data block, end it
        if any(k in line for k in ['push', 'mov ', 'call ', 'add ', 'ret', 'cmp ', 'jmp', 'je ', 'jne ']):
            if current_label and current_bytes:
                data_blocks[current_label] = bytes(current_bytes)
                current_label = None
                current_bytes = []
    
    # Save last block
    if current_label and current_bytes:
        data_blocks[current_label] = bytes(current_bytes)
    
    return data_blocks, global_data


def split_operands(values_str):
    """Split a db/dd operand list on commas outside quotes, dropping a ';' comment.

    Commas and ';' inside a quoted string are string bytes (ffedat.asm:5660
    `0xfd, ',Jettison', 0x0`), so both are only recognised outside quotes."""
    parts, cur, i = [], '', 0
    while i < len(values_str):
        c = values_str[i]
        if c in '"\'':
            j = values_str.find(c, i + 1)
            if j < 0:
                j = len(values_str) - 1
            cur += values_str[i:j + 1]
            i = j + 1
            continue
        if c == ';':
            break
        if c == ',':
            parts.append(cur)
            cur = ''
        else:
            cur += c
        i += 1
    parts.append(cur)
    return parts


def split_by_comma(s):
    """Split string by commas, respecting quoted strings"""
    result = []
    current = []
    in_quotes = False
    
    for ch in s:
        if ch == '"':
            in_quotes = not in_quotes
            current.append(ch)
        elif ch == ',' and not in_quotes:
            result.append(''.join(current))
            current = []
        else:
            current.append(ch)
    
    if current:
        result.append(''.join(current))
    
    return result


def parse_values(values_str):
    """Parse NASM db values into byte list"""
    result = []
    # Handle hex: 0x5, 0xa, etc
    # Handle decimal: 5, 10
    # Handle char: 'a', 'A'
    # Handle string: "hello"
    parts = split_operands(values_str)
    
    for part in parts:
        part = part.strip()
        if not part:
            continue
        
        # String
        if (part.startswith('"') and part.endswith('"')) or (part.startswith("'") and part.endswith("'")):
            s = part[1:-1]
            # Handle escape sequences
            s = s.replace('\\n', '\n').replace('\\t', '\t').replace('\\0', '\0')
            for c in s:
                result.append(ord(c) & 0xFF)
        # Hex
        elif part.startswith('0x') or part.startswith('0X'):
            try:
                val = int(part, 16)
                result.append(val & 0xFF)
            except ValueError:
                pass
        # Decimal
        else:
            try:
                val = int(part)
                result.append(val & 0xFF)
            except ValueError:
                pass
    
    return result


def parse_dd_values(values_str):
    """Parse NASM dd values into 4-byte LE byte sequence"""
    result = []
    parts = split_operands(values_str)
    
    for part in parts:
        part = part.strip()
        if not part:
            continue
        
        if part.startswith('DATA_'):
            # Reference to another DATA label (placeholder)
            result.extend([0, 0, 0, 0])
        elif part.startswith('0x'):
            try:
                val = int(part, 16)
                result.extend([val & 0xFF, (val >> 8) & 0xFF, (val >> 16) & 0xFF, (val >> 24) & 0xFF])
            except ValueError:
                result.extend([0, 0, 0, 0])
        else:
            try:
                val = int(part)
                result.extend([val & 0xFF, (val >> 8) & 0xFF, (val >> 16) & 0xFF, (val >> 24) & 0xFF])
            except ValueError:
                result.extend([0, 0, 0, 0])
    
    return result


def parse_dw_values(values_str):
    """Parse dw (define word) values - each value is 2 bytes, little-endian"""
    result = []
    # Split by comma, handling quoted strings
    parts = split_by_comma(values_str)
    
    for part in parts:
        part = part.strip()
        if not part:
            continue
        
        # Handle quoted strings in dw
        if part.startswith('"') and part.endswith('"'):
            # String in dw - each char gets 2 bytes (char + 0x00)
            string_val = part[1:-1]
            for ch in string_val:
                result.append(ord(ch) & 0xFF)
                result.append(0x00)
        else:
            # Numeric value - convert to 2-byte little-endian
            try:
                val = int(part, 0)  # Auto-detect hex (0x), binary (0b), etc.
                result.append(val & 0xFF)
                result.append((val >> 8) & 0xFF)
            except ValueError:
                pass  # Skip unparseable values
    
    return result


def parse_times_values(value_str, count):
    """Parse 'times N db value' - repeat byte value N times"""
    result = []
    value_str = value_str.strip()
    
    # Parse the value (can be hex like 0xFF or decimal)
    try:
        val = int(value_str, 0)
        byte_val = val & 0xFF
        result.extend([byte_val] * count)
    except ValueError:
        pass  # Skip if can't parse
    
    return result



def extract_data(nasm_dir, output_dir):
    """Extract data from all NASM files into binary blobs"""
    nasm_dir = Path(nasm_dir)
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    all_data = OrderedDict()
    all_globals = set()
    
    for nasm_file in sorted(nasm_dir.glob('*.asm')):
        print(f"Parsing {nasm_file.name}...")
        data_blocks, globals_set = parse_nasm_file(str(nasm_file))
        all_data.update(data_blocks)
        all_globals.update(globals_set)
    
    print(f"Found {len(all_data)} data blocks, {len(all_globals)} global declarations")
    
    # Write binary blob
    blob_path = output_dir / 'data_blob.bin'
    offset_map = {}
    current_offset = 0
    
    with open(blob_path, 'wb') as f:
        for label, data in all_data.items():
            offset_map[label] = current_offset
            f.write(data)
            current_offset += len(data)
    
    print(f"Wrote {current_offset} bytes to {blob_path}")
    
    # Write offset map
    map_path = output_dir / 'data_offset_map.txt'
    with open(map_path, 'w') as f:
        for label, offset in offset_map.items():
            size = len(all_data[label])
            f.write(f"{label}\t{offset}\t{size}\n")
    
    print(f"Wrote offset map to {map_path}")
    
    # Generate C++ header
    header_path = output_dir / 'data_blob.h'
    with open(header_path, 'w') as f:
        f.write('#ifndef JJFFE_DATA_BLOB_H\n')
        f.write('#define JJFFE_DATA_BLOB_H\n\n')
        f.write('// Auto-generated data blob offsets\n\n')
        f.write('namespace jjffe {\n')
        f.write('namespace data {\n\n')
        
        for label, offset in offset_map.items():
            size = len(all_data[label])
            f.write(f'// {label}: offset={offset}, size={size}\n')
            f.write(f'constexpr uint32_t OFFSET_{label} = {offset};\n')
            f.write(f'constexpr uint32_t SIZE_{label} = {size};\n\n')
        
        f.write(f'constexpr uint32_t BLOB_TOTAL_SIZE = {current_offset};\n\n')
        f.write('} // namespace data\n')
        f.write('} // namespace jjffe\n\n')
        f.write('#endif // JJFFE_DATA_BLOB_H\n')
    
    print(f"Wrote C++ header to {header_path}")
    
    # Print summary of large data blocks
    large_blocks = [(label, len(data)) for label, data in all_data.items() if len(data) > 100]
    large_blocks.sort(key=lambda x: x[1], reverse=True)
    
    print(f"\nLargest data blocks:")
    for label, size in large_blocks[:20]:
        is_global = "GLOBAL" if label in all_globals else "local"
        print(f"  {label}: {size} bytes ({is_global})")
    
    return len(all_data), current_offset


def main():
    import os
    
    if len(sys.argv) < 2:
        nasm_dir = '.'
        output_dir = 'data_blob'
    else:
        nasm_dir = sys.argv[1]
        output_dir = sys.argv[2] if len(sys.argv) > 2 else 'data_blob'
    
    num_blocks, total_size = extract_data(nasm_dir, output_dir)
    
    # Validate blob size against expected value from data_blob.h
    blob_path = os.path.join(output_dir, 'data_blob.bin')
    expected_size = 1583634  # BLOB_TOTAL_SIZE from data_blob.h
    
    if not validate_blob_size(blob_path, expected_size):
        print("
ERROR: Blob size validation failed. Extraction incomplete.")
        sys.exit(1)
    
    print("
Extraction completed successfully.")
    sys.exit(0)


def validate_blob_size(blob_path, expected_size):
    """Validate that the generated blob matches the expected size from data_blob.h"""
    import os
    
    if not os.path.exists(blob_path):
        print(f"ERROR: Blob file not found: {blob_path}")
        return False
    
    actual_size = os.path.getsize(blob_path)
    
    if actual_size != expected_size:
        print(f"\nWARNING: Blob size mismatch!")
        print(f"  Expected: {expected_size} bytes (from data_blob.h)")
        print(f"  Actual:   {actual_size} bytes")
        print(f"  Difference: {actual_size - expected_size} bytes")
        print("\nThe generated blob may not work correctly with the game.")
        print("Check that all dw/dq/times directives were parsed correctly.")
        return False
    else:
        print(f"\n✓ Blob size verified: {actual_size} bytes matches expected size")
        return True


if __name__ == '__main__':
    main()
