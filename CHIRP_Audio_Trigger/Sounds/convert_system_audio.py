import os
import glob
import re

def convert_audio_files():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    sys_dir = os.path.join(base_dir, '0_system')
    out_dir = os.path.join(base_dir, 'CHIRP_Audio')
    
    if not os.path.exists(sys_dir):
        print(f"Error: 0_system directory not found at {sys_dir}")
        return
        
    mp3_files = glob.glob(os.path.join(sys_dir, '*.mp3'))
    mp3_files.sort()
    
    if not mp3_files:
        print("No .mp3 files found in 0_system")
        return
        
    print(f"Found {len(mp3_files)} MP3 files. Converting...")
    
    h_path = os.path.join(out_dir, 'system_audio_data.h')
    cpp_path = os.path.join(out_dir, 'system_audio_data.cpp')
    
    # 1. Generate system_audio_data.h
    with open(h_path, 'w') as hf:
        hf.write('// Auto-generated file. Do not edit.\n')
        hf.write('#ifndef SYSTEM_AUDIO_DATA_H\n')
        hf.write('#define SYSTEM_AUDIO_DATA_H\n\n')
        hf.write('#include <Arduino.h>\n\n')
        
        hf.write('struct SysAudioFile {\n')
        hf.write('    const char* filename;\n')
        hf.write('    const uint8_t* data;\n')
        hf.write('    size_t size;\n')
        hf.write('};\n\n')
        
        hf.write('extern const SysAudioFile sysAudioFiles[];\n')
        hf.write(f'extern const int numSysAudioFiles;\n\n')
        
        # Declare arrays
        for path in mp3_files:
            fname = os.path.splitext(os.path.basename(path))[0]
            # Replace spaces and special chars with underscores
            safe_name = re.sub(r'[^a-zA-Z0-9_]', '_', fname)
            hf.write(f'extern const uint8_t sys_audio_{safe_name}[];\n')
            
        hf.write('\n#endif // SYSTEM_AUDIO_DATA_H\n')
        
    # 2. Generate system_audio_data.cpp
    with open(cpp_path, 'w') as cf:
        cf.write('// Auto-generated file. Do not edit manually.\n')
        cf.write('//\n')
        cf.write('// ============================================================================\n')
        cf.write('// HOW TO ADD NEW VOICE CHUNKS OR SOUNDS TO THIS FILE:\n')
        cf.write('// ============================================================================\n')
        cf.write('// 1. Create your original audio file (e.g. .wav).\n')
        cf.write('// 2. Convert it to a highly compressed Mono MP3:\n')
        cf.write('//    Format: 16 kb/s Bitrate, 22050 Hz Sample Rate, 1 Channel (Mono).\n')
        cf.write('//    FFmpeg Example (PowerShell):\n')
        cf.write('//      foreach ($f in Get-ChildItem -Path *.wav) { ffmpeg -i $f -b:a 16k -ac 1 -ar 22050 ( $f.BaseName + ".mp3") }\n')
        cf.write('// 3. Place the resulting .mp3 file into the "0_system" folder in the project root.\n')
        cf.write('// 4. Run the python script "convert_system_audio.py" located in the project root.\n')
        cf.write('// 5. This script will automatically convert all .mp3 files in the "0_system"\n')
        cf.write('//    folder into hex arrays and update this file and the corresponding header.\n')
        cf.write('// ============================================================================\n')
        cf.write('\n')
        cf.write('#include "system_audio_data.h"\n\n')
        
        total_size = 0
        
        # Write arrays
        for path in mp3_files:
            fname = os.path.splitext(os.path.basename(path))[0]
            safe_name = re.sub(r'[^a-zA-Z0-9_]', '_', fname)
            
            with open(path, 'rb') as f:
                data = f.read()
                
            total_size += len(data)
            
            cf.write(f'const uint8_t sys_audio_{safe_name}[] PROGMEM = {{\n')
            
            # Write hex values in rows of 16
            hex_data = [f"0x{b:02X}" for b in data]
            for i in range(0, len(hex_data), 16):
                row = ', '.join(hex_data[i:i+16])
                cf.write(f'    {row},\n')
                
            cf.write('};\n\n')
            
        # Write lookup table
        cf.write('const SysAudioFile sysAudioFiles[] = {\n')
        for path in mp3_files:
            fname = os.path.splitext(os.path.basename(path))[0]
            safe_name = re.sub(r'[^a-zA-Z0-9_]', '_', fname)
            
            # The firmware requests e.g. "battery.wav". We store "battery".
            # The lookup will ignore extension.
            # But just in case, let's keep it simple: store the basename exactly as it is without extension
            cf.write(f'    {{"{fname}", sys_audio_{safe_name}, sizeof(sys_audio_{safe_name})}},\n')
            
        cf.write('};\n\n')
        cf.write(f'const int numSysAudioFiles = {len(mp3_files)};\n')
        
    print(f"Successfully converted {len(mp3_files)} files.")
    print(f"Total PROGMEM usage: {total_size / 1024:.1f} KB")
    
if __name__ == "__main__":
    convert_audio_files()
