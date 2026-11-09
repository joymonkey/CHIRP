import os
import glob
import re
import shutil
import subprocess
import sys
import time

def convert_audio_files(force_reencode=False):
    base_dir = os.path.dirname(os.path.abspath(__file__))
    wav_dir = os.path.join(base_dir, '0_system_wav')
    mp3_dir = os.path.join(base_dir, '0_system_mp3')
    out_dir = os.path.abspath(os.path.join(base_dir, '..', 'src'))
    
    os.makedirs(mp3_dir, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)
    
    # 1. Check if source WAVs exist and convert to MP3 via ffmpeg
    wav_files = sorted(glob.glob(os.path.join(wav_dir, '*.wav')))
    ffmpeg_bin = shutil.which('ffmpeg')
    
    if wav_files:
        if not ffmpeg_bin:
            print("Warning: FFmpeg not found on PATH. Skipping WAV -> MP3 encoding.")
        else:
            print(f"Found {len(wav_files)} source WAV files.")
            print("Encoding WAVs to 16 kbps mono MP3 with automated silence trimming...")
            start_time = time.time()
            encoded_count = 0
            
            silence_filter = (
                "silenceremove=start_periods=1:start_duration=0.02:start_threshold=-45dB,"
                "areverse,"
                "silenceremove=start_periods=1:start_duration=0.02:start_threshold=-45dB,"
                "areverse"
            )
            
            for wav_path in wav_files:
                base_name = os.path.splitext(os.path.basename(wav_path))[0]
                mp3_path = os.path.join(mp3_dir, f"{base_name}.mp3")
                
                # Encode if mp3 doesn't exist, wav is newer, or force_reencode is True
                needs_encode = (
                    force_reencode or 
                    not os.path.exists(mp3_path) or 
                    os.path.getmtime(wav_path) > os.path.getmtime(mp3_path)
                )
                
                if needs_encode:
                    cmd = [
                        ffmpeg_bin,
                        '-y',
                        '-loglevel', 'error',
                        '-i', wav_path,
                        '-af', silence_filter,
                        '-b:a', '16k',
                        '-ac', '1',
                        '-ar', '22050',
                        mp3_path
                    ]
                    res = subprocess.run(cmd)
                    if res.returncode != 0:
                        print(f"Error encoding {wav_path}")
                    else:
                        encoded_count += 1
                        
            elapsed = time.time() - start_time
            print(f"Encoded/updated {encoded_count} MP3 files in {elapsed:.1f}s.")
            
    # 2. Gather MP3 files
    mp3_files = sorted(glob.glob(os.path.join(mp3_dir, '*.mp3')))
    if not mp3_files:
        print(f"Error: No .mp3 files found in {mp3_dir}")
        return
        
    print(f"Found {len(mp3_files)} MP3 files. Generating C++ firmware source...")
    
    h_path = os.path.join(out_dir, 'system_audio_data.h')
    cpp_path = os.path.join(out_dir, 'system_audio_data.cpp')
    
    # 3. Generate system_audio_data.h
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
            safe_name = re.sub(r'[^a-zA-Z0-9_]', '_', fname)
            hf.write(f'extern const uint8_t sys_audio_{safe_name}[];\n')
            
        hf.write('\n#endif // SYSTEM_AUDIO_DATA_H\n')
        
    # 4. Generate system_audio_data.cpp
    with open(cpp_path, 'w') as cf:
        cf.write('// Auto-generated file. Do not edit manually.\n')
        cf.write('//\n')
        cf.write('// ============================================================================\n')
        cf.write('// HOW TO ADD NEW VOICE CHUNKS OR SOUNDS TO THIS FILE:\n')
        cf.write('// ============================================================================\n')
        cf.write('// 1. Drop your master .wav file into "Sounds/0_system_wav/".\n')
        cf.write('// 2. Run "python Sounds/convert_system_audio.py".\n')
        cf.write('//    - The script will automatically trim leading/trailing silence via FFmpeg,\n')
        cf.write('//      encode to 16 kbps mono MP3, and regenerate this file and the header.\n')
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
            
            cf.write(f'    {{"{fname}", sys_audio_{safe_name}, sizeof(sys_audio_{safe_name})}},\n')
            
        cf.write('};\n\n')
        cf.write(f'const int numSysAudioFiles = {len(mp3_files)};\n')
        
    print(f"Successfully converted {len(mp3_files)} files.")
    print(f"Total PROGMEM flash usage: {total_size} bytes ({total_size / 1024:.1f} KB)")
    
if __name__ == "__main__":
    force = '--force' in sys.argv
    convert_audio_files(force_reencode=force)
