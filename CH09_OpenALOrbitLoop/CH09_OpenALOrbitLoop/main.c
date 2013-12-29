#import <AudioToolbox/AudioToolbox.h>
#import <OpenAL/al.h>
#import <OpenAL/alc.h>

// #define LOOP_PATH CFSTR("/Library/Audio/Apple Loops/Apple/iLife Sound Effects/Stingers/Cartoon Boing Boing.caf")
#define LOOP_PATH CFSTR ("/Library/Audio/Apple Loops/Apple/iLife Sound Effects/Transportation/Bicycle Coasting.caf")

#define ORBIT_SPEED 1
#define RUN_TIME 20.0

#pragma mark user-data struct
typedef struct MyLoopPlayer {
	AudioStreamBasicDescription	dataFormat;
	UInt16						*sampleBuffer;
	UInt32						bufferSizeBytes;
	ALuint						sources[1];
} MyLoopPlayer;

void updateSourceLocation (MyLoopPlayer player);
OSStatus loadLoopIntoBuffer(MyLoopPlayer* player);

#pragma mark - utility functions -
// generic error handler - if err is nonzero, prints error message and exits program.
static void CheckError(OSStatus error, const char *operation)
{
	if (error == noErr) return;
	
	char str[20];
	// see if it appears to be a 4-char-code
	*(UInt32 *)(str + 1) = CFSwapInt32HostToBig(error);
	if (isprint(str[1]) && isprint(str[2]) && isprint(str[3]) && isprint(str[4])) {
		str[0] = str[5] = '\'';
		str[6] = '\0';
	} else
		// no, format it as an integer
		sprintf(str, "%d", (int)error);
	
	fprintf(stderr, "Error: %s (%s)\n", operation, str);
	
	exit(1);
}

static void CheckALError (const char *operation) {
	ALenum alErr = alGetError();
	if (alErr == AL_NO_ERROR) return;
	char *errFormat = NULL;
	switch (alErr) {
		case AL_INVALID_NAME: errFormat = "OpenAL Error: %s (AL_INVALID_NAME)"; break;
		case AL_INVALID_VALUE:  errFormat = "OpenAL Error: %s (AL_INVALID_VALUE)"; break;
		case AL_INVALID_ENUM:  errFormat = "OpenAL Error: %s (AL_INVALID_ENUM)"; break;
		case AL_INVALID_OPERATION: errFormat = "OpenAL Error: %s (AL_INVALID_OPERATION)"; break;
		case AL_OUT_OF_MEMORY: errFormat = "OpenAL Error: %s (AL_OUT_OF_MEMORY)"; break;
	}
	fprintf (stderr, errFormat, operation);
	exit(1);
	
}

void updateSourceLocation (MyLoopPlayer player) {
	double theta = fmod (CFAbsoluteTimeGetCurrent() * ORBIT_SPEED, M_PI * 2);
	// printf ("%f\n", theta);
	ALfloat x = 3 * cos (theta);
	ALfloat y = 0.5 * sin (theta);
	ALfloat z = 1.0 * sin (theta);
	printf ("x=%f, y=%f, z=%f\n", x, y, z);
	alSource3f(player.sources[0], AL_POSITION, x, y, z);
}

OSStatus loadLoopIntoBuffer(MyLoopPlayer* player) {
	CFURLRef loopFileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, 
														 LOOP_PATH,
														 kCFURLPOSIXPathStyle,
														 false);
	
	// describe the client format - AL needs mono
	memset(&player->dataFormat, 0, sizeof(player->dataFormat));
	player->dataFormat.mFormatID = kAudioFormatLinearPCM;
	player->dataFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	player->dataFormat.mSampleRate = 44100.0;
	player->dataFormat.mChannelsPerFrame = 1;
	player->dataFormat.mFramesPerPacket = 1;
	player->dataFormat.mBitsPerChannel = 16;
	player->dataFormat.mBytesPerFrame = 2;
	player->dataFormat.mBytesPerPacket = 2;
	
	ExtAudioFileRef extAudioFile;
	CheckError (ExtAudioFileOpenURL(loopFileURL, &extAudioFile),
				"Couldn't open ExtAudioFile for reading");
	
	// tell extAudioFile about our format
	CheckError(ExtAudioFileSetProperty(extAudioFile,
									   kExtAudioFileProperty_ClientDataFormat,
									   sizeof (AudioStreamBasicDescription),
									   &player->dataFormat),
			   "Couldn't set client format on ExtAudioFile");
	
	// figure out how big a buffer we need
	SInt64 fileLengthFrames;
	UInt32 propSize = sizeof (fileLengthFrames);
	ExtAudioFileGetProperty(extAudioFile,
							kExtAudioFileProperty_FileLengthFrames,
							&propSize,
							&fileLengthFrames);
	
	printf ("plan on reading %lld frames\n", fileLengthFrames);
	player->bufferSizeBytes = fileLengthFrames * player->dataFormat.mBytesPerFrame;
	
	AudioBufferList *buffers;
	UInt32 ablSize = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) * 1); // 1 channel
	buffers = malloc (ablSize);
	
	// allocate sample buffer
	player->sampleBuffer =  malloc(sizeof(UInt16) * player->bufferSizeBytes); // 4/18/11 - fix 1
	
	buffers->mNumberBuffers = 1;
	buffers->mBuffers[0].mNumberChannels = 1;
	buffers->mBuffers[0].mDataByteSize = player->bufferSizeBytes;
	buffers->mBuffers[0].mData = player->sampleBuffer;
	
	printf ("created AudioBufferList\n");
	
	// loop reading into the ABL until buffer is full
	UInt32 totalFramesRead = 0;
	do {
		UInt32 framesRead = fileLengthFrames - totalFramesRead;
		buffers->mBuffers[0].mData = player->sampleBuffer + (totalFramesRead * (sizeof(UInt16)));
		CheckError(ExtAudioFileRead(extAudioFile, 
									&framesRead,
									buffers),
				   "ExtAudioFileRead failed");
		totalFramesRead += framesRead;
		printf ("read %d frames\n", framesRead);
	} while (totalFramesRead < fileLengthFrames);
	
	// can free the ABL; still have samples in sampleBuffer
	free(buffers);
	return noErr;
}

#pragma mark main

int main (int argc, const char * argv[]) {
	MyLoopPlayer player;
	
	// convert to an OpenAL-friendly format and read into memory
	CheckError(loadLoopIntoBuffer(&player),
			   "Couldn't load loop into buffer") ;
	
	// set up OpenAL buffer
	ALCdevice* alDevice = alcOpenDevice(NULL);
	CheckALError ("Couldn't open AL device"); // default device
	ALCcontext* alContext = alcCreateContext(alDevice, 0);
	CheckALError ("Couldn't open AL context");
	alcMakeContextCurrent (alContext);
	CheckALError ("Couldn't make AL context current");
	ALuint buffers[1];
	alGenBuffers(1, buffers);
	CheckALError ("Couldn't generate buffers");
	alBufferData(*buffers,
				 AL_FORMAT_MONO16,
				 player.sampleBuffer,
				 player.bufferSizeBytes,
				 player.dataFormat.mSampleRate);
	
	// AL copies the samples, so we can free them now
	free(player.sampleBuffer);
	
	// set up OpenAL source
	alGenSources(1, player.sources);
	CheckALError ("Couldn't generate sources");
	alSourcei(player.sources[0], AL_LOOPING, AL_TRUE);
	CheckALError ("Couldn't set source looping property");
	alSourcef(player.sources[0], AL_GAIN, AL_MAX_GAIN);
	CheckALError("Couldn't set source gain");
	updateSourceLocation(player);
	CheckALError ("Couldn't set initial source position");
	
	// connect buffer to source
	alSourcei(player.sources[0], AL_BUFFER, buffers[0]);
	CheckALError ("Couldn't connect buffer to source");
	
	// set up listener
	alListener3f (AL_POSITION, 0.0, 0.0, 0.0);
	CheckALError("Couldn't set listner position");
	
	//	ALfloat listenerOrientation[6]; // 3 vectors: forward x,y,z components, then up x,y,z
	//	listenerOrientation[2] = -1.0;
	//	listenerOrientation[0] = listenerOrientation [1] = 0.0;
	//	listenerOrientation[3] = listenerOrientation [4] =  listenerOrientation[5] = 0.0;
	//	alListenerfv (AL_ORIENTATION, listenerOrientation);
	
	// start playing
	// alSourcePlayv (1, player.sources);
	alSourcePlay(player.sources[0]);
	CheckALError ("Couldn't play");
	
	// and wait
	printf("Playing...\n");
	time_t startTime = time(NULL);
	do
	{
		// get next theta
		updateSourceLocation(player);
		CheckALError ("Couldn't set looping source position");
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
	} while (difftime(time(NULL), startTime) < RUN_TIME);
	
	// cleanup:
	alSourceStop(player.sources[0]);
	alDeleteSources(1, player.sources);
	alDeleteBuffers(1, buffers);
	alcDestroyContext(alContext);
	alcCloseDevice(alDevice);
	printf ("Bottom of main\n");
}