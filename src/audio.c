/*********************************************************************************************
*
*   raylib.audio
*
*   Basic functions to manage Audio: InitAudioDevice, LoadAudioFiles, PlayAudioFiles
*    
*   Uses external lib:    
*       OpenAL - Audio device management lib
*       stb_vorbis - Ogg audio files loading
*       
*   Copyright (c) 2013 Ramon Santamaria (Ray San - raysan@raysanweb.com)
*    
*   This software is provided "as-is", without any express or implied warranty. In no event 
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial 
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you 
*     wrote the original software. If you use this software in a product, an acknowledgment 
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#include "raylib.h"

#include <AL/al.h>           // OpenAL basic header
#include <AL/alc.h>          // OpenAL context header (like OpenGL, OpenAL requires a context to work)

#include <stdlib.h>          // Declares malloc() and free() for memory management
#include <string.h>          // Required for strcmp()
#include <stdio.h>           // Used for .WAV loading

#include "utils.h"           // rRES data decompression utility function

#include "stb_vorbis.h"      // OGG loading functions

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
#define MUSIC_STREAM_BUFFERS        2
#define MUSIC_BUFFER_SIZE      4096*8   //4096*32

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------

// Music type (file streaming from memory)
// NOTE: Anything longer than ~10 seconds should be streamed...
typedef struct Music {
    stb_vorbis *stream;
    
	ALuint buffers[MUSIC_STREAM_BUFFERS];
	ALuint source;
	ALenum format;
 
    int channels;
    int sampleRate;
	int totalSamplesLeft;
	bool loop;
    
} Music;

// Wave file data
typedef struct Wave {
    void *data;                 // Buffer data pointer
    unsigned int dataSize;      // Data size in bytes
    unsigned int sampleRate;
    short bitsPerSample;
    short channels;  
} Wave;

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
bool musicEnabled = false;
static Music currentMusic;      // Current music loaded
                                // NOTE: Only one music file playing at a time

//----------------------------------------------------------------------------------
// Module specific Functions Declaration
//----------------------------------------------------------------------------------
static Wave LoadWAV(const char *fileName);
static Wave LoadOGG(char *fileName);
static void UnloadWave(Wave wave);

static bool BufferMusicStream(ALuint buffer);   // Fill music buffers with data
static void EmptyMusicStream();                 // Empty music buffers
extern void UpdateMusicStream();                // Updates buffers (refill) for music streaming

//----------------------------------------------------------------------------------
// Module Functions Definition - Audio Device initialization and Closing
//----------------------------------------------------------------------------------

// Initialize audio device and context
void InitAudioDevice()
{
    // Open and initialize a device with default settings
    ALCdevice *device = alcOpenDevice(NULL);
    
    if(!device) TraceLog(ERROR, "Could not open audio device");

    ALCcontext *context = alcCreateContext(device, NULL);
    
    if(context == NULL || alcMakeContextCurrent(context) == ALC_FALSE)
    {
        if(context != NULL) alcDestroyContext(context);
        
        alcCloseDevice(device);
        
        TraceLog(ERROR, "Could not setup audio context");
    }

    TraceLog(INFO, "Audio device and context initialized: %s\n", alcGetString(device, ALC_DEVICE_SPECIFIER));
    
    // Listener definition (just for 2D)
    alListener3f(AL_POSITION, 0, 0, 0);
    alListener3f(AL_VELOCITY, 0, 0, 0);
    alListener3f(AL_ORIENTATION, 0, 0, -1);
}

// Close the audio device for the current context, and destroys the context
void CloseAudioDevice()
{
    StopMusicStream();      // Stop music streaming and close current stream

    ALCdevice *device;
    ALCcontext *context = alcGetCurrentContext();
    
    if (context == NULL) TraceLog(WARNING, "Could not get current audio context for closing");

    device = alcGetContextsDevice(context);

    alcMakeContextCurrent(NULL);
    alcDestroyContext(context);
    alcCloseDevice(device);
}

//----------------------------------------------------------------------------------
// Module Functions Definition - Sounds loading and playing (.WAV)
//----------------------------------------------------------------------------------

// Load sound to memory
Sound LoadSound(char *fileName)
{
    Sound sound;
    Wave wave;
    
    // NOTE: The entire file is loaded to memory to play it all at once (no-streaming)
    
    // Audio file loading
    // NOTE: Buffer space is allocated inside function, Wave must be freed
    
    if (strcmp(GetExtension(fileName),"wav") == 0) wave = LoadWAV(fileName);
    else if (strcmp(GetExtension(fileName),"ogg") == 0) wave = LoadOGG(fileName);
    else TraceLog(WARNING, "[%s] Sound extension not recognized, it can't be loaded", fileName);
    
    if (wave.data != NULL)
    {
        ALenum format = 0;
        // The OpenAL format is worked out by looking at the number of channels and the bits per sample
        if (wave.channels == 1) 
        {
            if (wave.bitsPerSample == 8 ) format = AL_FORMAT_MONO8;
            else if (wave.bitsPerSample == 16) format = AL_FORMAT_MONO16;
        } 
        else if (wave.channels == 2) 
        {
            if (wave.bitsPerSample == 8 ) format = AL_FORMAT_STEREO8;
            else if (wave.bitsPerSample == 16) format = AL_FORMAT_STEREO16;
        }
        
        // Create an audio source
        ALuint source;
        alGenSources(1, &source);            // Generate pointer to audio source

        alSourcef(source, AL_PITCH, 1);    
        alSourcef(source, AL_GAIN, 1);
        alSource3f(source, AL_POSITION, 0, 0, 0);
        alSource3f(source, AL_VELOCITY, 0, 0, 0);
        alSourcei(source, AL_LOOPING, AL_FALSE);
        
        // Convert loaded data to OpenAL buffer
        //----------------------------------------
        ALuint buffer;
        alGenBuffers(1, &buffer);            // Generate pointer to buffer

        // Upload sound data to buffer
        alBufferData(buffer, format, wave.data, wave.dataSize, wave.sampleRate);

        // Attach sound buffer to source
        alSourcei(source, AL_BUFFER, buffer);
        
        // Unallocate WAV data
        UnloadWave(wave);
        
        TraceLog(INFO, "[%s] Sound file loaded successfully", fileName);  
        TraceLog(INFO, "[%s] Sample rate: %i - Channels: %i", fileName, wave.sampleRate, wave.channels);
        
        sound.source = source;
        sound.buffer = buffer;
    }
    
    return sound;
}

// Load sound to memory from rRES file (raylib Resource)
Sound LoadSoundFromRES(const char *rresName, int resId)
{
    // NOTE: rresName could be directly a char array with all the data!!! --> TODO
    Sound sound;
    bool found = false;

    char id[4];             // rRES file identifier
    unsigned char version;  // rRES file version and subversion
    char useless;           // rRES header reserved data
    short numRes;
    
    ResInfoHeader infoHeader;
    
    FILE *rresFile = fopen(rresName, "rb");

    if (!rresFile) TraceLog(WARNING, "[%s] Could not open raylib resource file", rresName);
    else
    {
        // Read rres file (basic file check - id)
        fread(&id[0], sizeof(char), 1, rresFile);
        fread(&id[1], sizeof(char), 1, rresFile);
        fread(&id[2], sizeof(char), 1, rresFile);
        fread(&id[3], sizeof(char), 1, rresFile);
        fread(&version, sizeof(char), 1, rresFile);
        fread(&useless, sizeof(char), 1, rresFile);
        
        if ((id[0] != 'r') && (id[1] != 'R') && (id[2] != 'E') &&(id[3] != 'S'))
        {
            TraceLog(WARNING, "[%s] This is not a valid raylib resource file", rresName);
        }
        else
        {
            // Read number of resources embedded
            fread(&numRes, sizeof(short), 1, rresFile);
            
            for (int i = 0; i < numRes; i++)
            {
                fread(&infoHeader, sizeof(ResInfoHeader), 1, rresFile);
                
                if (infoHeader.id == resId)
                {
                    found = true;

                    // Check data is of valid SOUND type
                    if (infoHeader.type == 1)   // SOUND data type
                    {
                        // TODO: Check data compression type
                        // NOTE: We suppose compression type 2 (DEFLATE - default)
                        
                        // Reading SOUND parameters
                        Wave wave;
                        short sampleRate, bps;
                        char channels, reserved;
                    
                        fread(&sampleRate, sizeof(short), 1, rresFile); // Sample rate (frequency)
                        fread(&bps, sizeof(short), 1, rresFile);        // Bits per sample
                        fread(&channels, 1, 1, rresFile);               // Channels (1 - mono, 2 - stereo)
                        fread(&reserved, 1, 1, rresFile);               // <reserved>
                                
                        wave.sampleRate = sampleRate;
                        wave.dataSize = infoHeader.srcSize;
                        wave.bitsPerSample = bps;
                        wave.channels = (short)channels;
                        
                        unsigned char *data = malloc(infoHeader.size);

                        fread(data, infoHeader.size, 1, rresFile);
                        
                        wave.data = DecompressData(data, infoHeader.size, infoHeader.srcSize);
                        
                        free(data);
                        
                        // Convert wave to Sound (OpenAL)
                        ALenum format = 0;
                        
                        // The OpenAL format is worked out by looking at the number of channels and the bits per sample
                        if (wave.channels == 1) 
                        {
                            if (wave.bitsPerSample == 8 ) format = AL_FORMAT_MONO8;
                            else if (wave.bitsPerSample == 16) format = AL_FORMAT_MONO16;
                        } 
                        else if (wave.channels == 2) 
                        {
                            if (wave.bitsPerSample == 8 ) format = AL_FORMAT_STEREO8;
                            else if (wave.bitsPerSample == 16) format = AL_FORMAT_STEREO16;
                        }
                        
                        
                        // Create an audio source
                        ALuint source;
                        alGenSources(1, &source);            // Generate pointer to audio source

                        alSourcef(source, AL_PITCH, 1);    
                        alSourcef(source, AL_GAIN, 1);
                        alSource3f(source, AL_POSITION, 0, 0, 0);
                        alSource3f(source, AL_VELOCITY, 0, 0, 0);
                        alSourcei(source, AL_LOOPING, AL_FALSE);
                        
                        // Convert loaded data to OpenAL buffer
                        //----------------------------------------
                        ALuint buffer;
                        alGenBuffers(1, &buffer);            // Generate pointer to buffer

                        // Upload sound data to buffer
                        alBufferData(buffer, format, (void*)wave.data, wave.dataSize, wave.sampleRate);

                        // Attach sound buffer to source
                        alSourcei(source, AL_BUFFER, buffer);
                        
                        // Unallocate WAV data
                        UnloadWave(wave);

                        TraceLog(INFO, "[%s] Sound loaded successfully from resource, sample rate: %i", rresName, (int)sampleRate);
                        
                        sound.source = source;
                        sound.buffer = buffer;
                    }
                    else
                    {
                        TraceLog(WARNING, "[%s] Required resource do not seem to be a valid SOUND resource", rresName);
                    }
                }
                else
                {
                    // Depending on type, skip the right amount of parameters
                    switch (infoHeader.type)
                    {
                        case 0: fseek(rresFile, 6, SEEK_CUR); break;   // IMAGE: Jump 6 bytes of parameters
                        case 1: fseek(rresFile, 6, SEEK_CUR); break;   // SOUND: Jump 6 bytes of parameters
                        case 2: fseek(rresFile, 5, SEEK_CUR); break;   // MODEL: Jump 5 bytes of parameters (TODO: Review)
                        case 3: break;   // TEXT: No parameters
                        case 4: break;   // RAW: No parameters
                        default: break;
                    }
                    
                    // Jump DATA to read next infoHeader
                    fseek(rresFile, infoHeader.size, SEEK_CUR);
                }    
            }
        }
        
        fclose(rresFile);
    }
    
    if (!found) TraceLog(WARNING, "[%s] Required resource id [%i] could not be found in the raylib resource file", rresName, resId);
    
    return sound;
}

// Unload sound
void UnloadSound(Sound sound)
{
    alDeleteSources(1, &sound.source);
    alDeleteBuffers(1, &sound.buffer);
}

// Play a sound
void PlaySound(Sound sound)
{
    alSourcePlay(sound.source);        // Play the sound
    
    TraceLog(INFO, "Playing sound");

    // Find the current position of the sound being played
    // NOTE: Only work when the entire file is in a single buffer
    //int byteOffset;
    //alGetSourcei(sound.source, AL_BYTE_OFFSET, &byteOffset);
    //
    //int sampleRate;
    //alGetBufferi(sound.buffer, AL_FREQUENCY, &sampleRate);    // AL_CHANNELS, AL_BITS (bps)
    
    //float seconds = (float)byteOffset / sampleRate;      // Number of seconds since the beginning of the sound
    //or
    //float result;
    //alGetSourcef(sound.source, AL_SEC_OFFSET, &result);   // AL_SAMPLE_OFFSET
}

// Pause a sound
void PauseSound(Sound sound)
{
    alSourcePause(sound.source);
}

// Stop reproducing a sound
void StopSound(Sound sound)
{
    alSourceStop(sound.source);
}

// Check if a sound is playing
bool SoundIsPlaying(Sound sound)
{
    bool playing = false;
    ALint state;
    
    alGetSourcei(sound.source, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING) playing = true;
    
    return playing;
}

// Set volume for a sound
void SetSoundVolume(Sound sound, float volume)
{
    alSourcef(sound.source, AL_GAIN, volume);
}

// Set pitch for a sound
void SetSoundPitch(Sound sound, float pitch)
{
    alSourcef(sound.source, AL_PITCH, pitch);
}

//----------------------------------------------------------------------------------
// Module Functions Definition - Music loading and stream playing (.OGG)
//----------------------------------------------------------------------------------

// Start music playing (open stream)
void PlayMusicStream(char *fileName)
{
    if (strcmp(GetExtension(fileName),"ogg") == 0)
    {
        // Stop current music, clean buffers, unload current stream
        StopMusicStream();
    
        // Open audio stream
        currentMusic.stream = stb_vorbis_open_filename(fileName, NULL, NULL);
        
        if (currentMusic.stream == NULL) TraceLog(WARNING, "[%s] Could not open ogg audio file", fileName);
        else
        {
            // Get file info
            stb_vorbis_info info = stb_vorbis_get_info(currentMusic.stream);
            
            currentMusic.channels = info.channels;
            currentMusic.sampleRate = info.sample_rate;
            
            TraceLog(INFO, "[%s] Ogg sample rate: %i", fileName, info.sample_rate);
            TraceLog(INFO, "[%s] Ogg channels: %i", fileName, info.channels);
            TraceLog(INFO, "[%s] Temp memory required: %i", fileName, info.temp_memory_required);
            
            if (info.channels == 2) currentMusic.format = AL_FORMAT_STEREO16;
            else currentMusic.format = AL_FORMAT_MONO16;
            
            currentMusic.loop = true;                  // We loop by default
            musicEnabled = true;
            
            // Create an audio source
            alGenSources(1, &currentMusic.source);     // Generate pointer to audio source

            alSourcef(currentMusic.source, AL_PITCH, 1);    
            alSourcef(currentMusic.source, AL_GAIN, 1);
            alSource3f(currentMusic.source, AL_POSITION, 0, 0, 0);
            alSource3f(currentMusic.source, AL_VELOCITY, 0, 0, 0);
            //alSourcei(currentMusic.source, AL_LOOPING, AL_TRUE);     // ERROR: Buffers do not queue!
            
            // Generate two OpenAL buffers
            alGenBuffers(2, currentMusic.buffers);

            // Fill buffers with music...
            BufferMusicStream(currentMusic.buffers[0]);
            BufferMusicStream(currentMusic.buffers[1]);
            
            // Queue buffers and start playing
            alSourceQueueBuffers(currentMusic.source, 2, currentMusic.buffers);
            alSourcePlay(currentMusic.source);
            
            // NOTE: Regularly, we must check if a buffer has been processed and refill it: MusicStreamUpdate()

            currentMusic.totalSamplesLeft = stb_vorbis_stream_length_in_samples(currentMusic.stream) * currentMusic.channels;
        }
    }
    else TraceLog(WARNING, "[%s] Music extension not recognized, it can't be loaded", fileName);
}

// Stop music playing (close stream)
void StopMusicStream()
{
    if (musicEnabled)
    {
        alSourceStop(currentMusic.source);
        
        EmptyMusicStream();     // Empty music buffers
        
        alDeleteSources(1, &currentMusic.source);
        alDeleteBuffers(2, currentMusic.buffers);
        
        stb_vorbis_close(currentMusic.stream);
    }
    
    musicEnabled = false;
}

// Pause music playing
void PauseMusicStream()
{
    // TODO: Record music is paused or check if music available!
    alSourcePause(currentMusic.source);
}

// Check if music is playing
bool MusicIsPlaying()
{
    ALenum state;
    
    alGetSourcei(currentMusic.source, AL_SOURCE_STATE, &state);
    
    return (state == AL_PLAYING);
}

// Set volume for music
void SetMusicVolume(float volume)
{
    alSourcef(currentMusic.source, AL_GAIN, volume);
}

// Get current music time length (in seconds)
float GetMusicTimeLength()
{
    float totalSeconds = stb_vorbis_stream_length_in_seconds(currentMusic.stream);
    
    return totalSeconds;
}

// Get current music time played (in seconds)
float GetMusicTimePlayed()
{
    int totalSamples = stb_vorbis_stream_length_in_samples(currentMusic.stream) * currentMusic.channels;
    
    int samplesPlayed = totalSamples - currentMusic.totalSamplesLeft;
    
    float secondsPlayed = (float)samplesPlayed / (currentMusic.sampleRate * currentMusic.channels);
    
    return secondsPlayed;
}

//----------------------------------------------------------------------------------
// Module specific Functions Definition
//----------------------------------------------------------------------------------

// Fill music buffers with new data from music stream
static bool BufferMusicStream(ALuint buffer)
{
	short pcm[MUSIC_BUFFER_SIZE];
    
	int  size = 0;              // Total size of data steamed (in bytes)
	int  streamedBytes = 0;     // Bytes of data obtained in one samples get
    
    bool active = true;         // We can get more data from stream (not finished)
    
    if (musicEnabled)
    {
        while (size < MUSIC_BUFFER_SIZE)
        {
            streamedBytes = stb_vorbis_get_samples_short_interleaved(currentMusic.stream, currentMusic.channels, pcm + size, MUSIC_BUFFER_SIZE - size);
            
            if (streamedBytes > 0) size += (streamedBytes*currentMusic.channels);
            else break;
        }
        
        TraceLog(DEBUG, "Streaming music data to buffer. Bytes streamed: %i", size);
    }
    
	if (size > 0)
    {
        alBufferData(buffer, currentMusic.format, pcm, size*sizeof(short), currentMusic.sampleRate);
        
        currentMusic.totalSamplesLeft -= size;
    }
    else
    {
        active = false;
        TraceLog(WARNING, "No more data obtained from stream");
    }

	return active;
}

// Empty music buffers
static void EmptyMusicStream()
{
    ALuint buffer = 0; 
    int queued = 0;
    
    alGetSourcei(currentMusic.source, AL_BUFFERS_QUEUED, &queued);
    
    while(queued > 0)
    {
        alSourceUnqueueBuffers(currentMusic.source, 1, &buffer);
        
        queued--;
    }
}

// Update (re-fill) music buffers if data already processed
extern void UpdateMusicStream()
{
    ALuint buffer = 0;
    ALint processed = 0;
    bool active = true;
    
    if (musicEnabled)
    {
        // Get the number of already processed buffers (if any)
        alGetSourcei(currentMusic.source, AL_BUFFERS_PROCESSED, &processed);
        
        while (processed > 0)
        {
            // Recover processed buffer for refill
            alSourceUnqueueBuffers(currentMusic.source, 1, &buffer);

            // Refill buffer
            active = BufferMusicStream(buffer);
            
            // If no more data to stream, restart music (if loop)
            if ((!active) && (currentMusic.loop))   
            {
                if (currentMusic.loop)
                {
                    stb_vorbis_seek_start(currentMusic.stream);
                    currentMusic.totalSamplesLeft = stb_vorbis_stream_length_in_samples(currentMusic.stream) * currentMusic.channels;
                    
                    active = BufferMusicStream(buffer);
                }
            }
            
            // Add refilled buffer to queue again... don't let the music stop!
            alSourceQueueBuffers(currentMusic.source, 1, &buffer);
            
            if(alGetError() != AL_NO_ERROR) TraceLog(WARNING, "Ogg playing, error buffering data...");
            
            processed--;
        }
        
        ALenum state;
        alGetSourcei(currentMusic.source, AL_SOURCE_STATE, &state);
        
        if ((state != AL_PLAYING) && active) alSourcePlay(currentMusic.source);
        
        if (!active) StopMusicStream();
    }
}

// Load WAV file into Wave structure
static Wave LoadWAV(const char *fileName)
{
    // Basic WAV headers structs
    typedef struct {
        char chunkID[4];
        long chunkSize;
        char format[4];
    } RiffHeader;

    typedef struct {
        char subChunkID[4];
        long subChunkSize;
        short audioFormat;
        short numChannels;
        long sampleRate;
        long byteRate;
        short blockAlign;
        short bitsPerSample;
    } WaveFormat;

    typedef struct {
        char subChunkID[4];
        long subChunkSize;
    } WaveData;
    
    RiffHeader riffHeader;
    WaveFormat waveFormat;
    WaveData waveData;
    
    Wave wave;
    FILE *wavFile;
    
    wavFile = fopen(fileName, "rb");
    
    if (!wavFile)
    {
        TraceLog(WARNING, "[%s] Could not open WAV file", fileName);
    }
    else
    {
        // Read in the first chunk into the struct
        fread(&riffHeader, sizeof(RiffHeader), 1, wavFile);
     
        // Check for RIFF and WAVE tags
        if (((riffHeader.chunkID[0] != 'R') || (riffHeader.chunkID[1] != 'I') || (riffHeader.chunkID[2] != 'F') || (riffHeader.chunkID[3] != 'F')) ||
            ((riffHeader.format[0] != 'W') || (riffHeader.format[1] != 'A') || (riffHeader.format[2] != 'V') || (riffHeader.format[3] != 'E')))
        {
                TraceLog(WARNING, "[%s] Invalid RIFF or WAVE Header", fileName);
        }
        else
        {
            // Read in the 2nd chunk for the wave info
            fread(&waveFormat, sizeof(WaveFormat), 1, wavFile);
            
            // Check for fmt tag
            if ((waveFormat.subChunkID[0] != 'f') || (waveFormat.subChunkID[1] != 'm') ||
                (waveFormat.subChunkID[2] != 't') || (waveFormat.subChunkID[3] != ' '))
            {
                TraceLog(WARNING, "[%s] Invalid Wave format", fileName);
            }
            else
            {
                // Check for extra parameters;
                if (waveFormat.subChunkSize > 16) fseek(wavFile, sizeof(short), SEEK_CUR);
             
                // Read in the the last byte of data before the sound file
                fread(&waveData, sizeof(WaveData), 1, wavFile);
                
                // Check for data tag
                if ((waveData.subChunkID[0] != 'd') || (waveData.subChunkID[1] != 'a') ||
                    (waveData.subChunkID[2] != 't') || (waveData.subChunkID[3] != 'a'))
                {
                    TraceLog(WARNING, "[%s] Invalid data header", fileName);
                }
                else
                {
                    // Allocate memory for data
                    wave.data = (unsigned char *)malloc(sizeof(unsigned char) * waveData.subChunkSize); 
                 
                    // Read in the sound data into the soundData variable
                    fread(wave.data, waveData.subChunkSize, 1, wavFile);
                    
                    // Now we set the variables that we need later
                    wave.dataSize = waveData.subChunkSize;
                    wave.sampleRate = waveFormat.sampleRate;
                    wave.channels = waveFormat.numChannels;
                    wave.bitsPerSample = waveFormat.bitsPerSample;
                    
                    TraceLog(INFO, "[%s] Wave file loaded successfully", fileName);
                }
            }
        }

        fclose(wavFile);
    }
    
    return wave;
}

// Load OGG file into Wave structure
static Wave LoadOGG(char *fileName)
{
    Wave wave;
    
    stb_vorbis *oggFile = stb_vorbis_open_filename(fileName, NULL, NULL);
    stb_vorbis_info info = stb_vorbis_get_info(oggFile);
    
    wave.sampleRate = info.sample_rate;
    wave.bitsPerSample = 16;
    wave.channels = info.channels;
    
    TraceLog(DEBUG, "[%s] Ogg sample rate: %i", fileName, info.sample_rate);
    TraceLog(DEBUG, "[%s] Ogg channels: %i", fileName, info.channels);

    int totalSamplesLength = (stb_vorbis_stream_length_in_samples(oggFile) * info.channels);
    
    wave.dataSize = totalSamplesLength*sizeof(short);   // Size must be in bytes
    
    TraceLog(DEBUG, "[%s] Samples length: %i", fileName, totalSamplesLength);
    
    float totalSeconds = stb_vorbis_stream_length_in_seconds(oggFile);
    
    TraceLog(DEBUG, "[%s] Total seconds: %f", fileName, totalSeconds);
    
    if (totalSeconds > 10) TraceLog(WARNING, "[%s] Ogg audio lenght is larger than 10 seconds (%f), that's a big file in memory, consider music streaming", fileName, totalSeconds);
    
    int totalSamples = totalSeconds*info.sample_rate*info.channels;
   
    TraceLog(DEBUG, "[%s] Total samples calculated: %i", fileName, totalSamples);
    
    //short *data 
    wave.data = malloc(sizeof(short)*totalSamplesLength);

    int samplesObtained = stb_vorbis_get_samples_short_interleaved(oggFile, info.channels, wave.data, totalSamplesLength);
    
    TraceLog(DEBUG, "[%s] Samples obtained: %i", fileName, samplesObtained);

    stb_vorbis_close(oggFile);
    
    return wave;
}

// Unload Wave data
static void UnloadWave(Wave wave)
{
    free(wave.data);
}