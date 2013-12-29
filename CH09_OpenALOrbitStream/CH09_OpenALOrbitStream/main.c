#import <AudioToolbox/AudioToolbox.h>
#import <OpenAL/al.h>
#import <OpenAL/alc.h>

#define STREAM_PATH CFSTR ("/Library/Audio/Apple Loops/Apple/iLife Sound Effects/Jingles/Kickflip Long.caf")
//#define STREAM_PATH CFSTR ("/Volumes/Sephiroth/Tunes/Yes/Highlights - The Very Best Of Yes/Long Distance Runaround.m4a")

#define ORBIT_SPEED 1
#define BUFFER_DURATION_SECONDS	1.0
#define BUFFER_COUNT 3
#define RUN_TIME 20.0

typedef struct MyStreamPlayer {
	AudioStreamBasicDescription	dataFormat;
	UInt32						bufferSizeBytes;
	SInt64						fileLengthFrames;
	SInt64						totalFramesRead;
	ALuint						sources[1];
	// ALuint						buffers[BUFFER_COUNT]; // might need this back for static buffers in ch12/13?
	ExtAudioFileRef				extAudioFile;
} MyStreamPlayer;

void updateSourceLocation (MyStreamPlayer player);
OSStatus setUpExtAudioFile (MyStreamPlayer* player);
void refillALBuffers (MyStreamPlayer* player);
void fillALBuffer (MyStreamPlayer* player, ALuint alBuffer);

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
		default: errFormat = "OpenAL Error: %s (unknown error code)"; break;
	}
	fprintf (stderr, errFormat, operation);
	exit(1);
	
}

void updateSourceLocation (MyStreamPlayer player) {
	double theta = fmod (CFAbsoluteTimeGetCurrent() * ORBIT_SPEED, M_PI * 2);
	// printf ("%f\n", theta);
	ALfloat x = 3 * cos (theta);
	ALfloat y = 0.5 * sin (theta);
	ALfloat z = 1.0 * sin (theta);
	printf ("x=%f, y=%f, z=%f\n", x, y, z);
	alSource3f(player.sources[0], AL_POSITION, x, y, z);
}


OSStatus setUpExtAudioFile (MyStreamPlayer* player) {
	CFURLRef streamFileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, 
														   STREAM_PATH,
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
	
	CheckError (ExtAudioFileOpenURL(streamFileURL, &player->extAudioFile),
				"Couldn't open ExtAudioFile for reading");
	
	// tell extAudioFile about our format
	CheckError(ExtAudioFileSetProperty(player->extAudioFile,
									   kExtAudioFileProperty_ClientDataFormat,
									   sizeof (AudioStreamBasicDescription),
									   &player->dataFormat),
			   "Couldn't set client format on ExtAudioFile");
	
	// figure out how big file is
	UInt32 propSize = sizeof (player->fileLengthFrames);
	ExtAudioFileGetProperty(player->extAudioFile,
							kExtAudioFileProperty_FileLengthFrames,
							&propSize,
							&player->fileLengthFrames);
	
	printf ("fileLengthFrames = %lld frames\n", player->fileLengthFrames);
	
	player->bufferSizeBytes = BUFFER_DURATION_SECONDS *
	player->dataFormat.mSampleRate *
	player->dataFormat.mBytesPerFrame;
	
	printf ("bufferSizeBytes = %d\n", player->bufferSizeBytes);
	
	printf ("Bottom of setUpExtAudioFile\n");
	return noErr;
}

void fillALBuffer (MyStreamPlayer* player, ALuint alBuffer) {
	AudioBufferList *bufferList;
	UInt32 ablSize = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) * 1); // 1 channel
	bufferList = malloc (ablSize);
	
	// allocate sample buffer
	UInt16 *sampleBuffer = malloc(sizeof(UInt16) * player->bufferSizeBytes); // 4/18/11 - fix 2
	
	bufferList->mNumberBuffers = 1;
	bufferList->mBuffers[0].mNumberChannels = 1;
	bufferList->mBuffers[0].mDataByteSize = player->bufferSizeBytes;
	bufferList->mBuffers[0].mData = sampleBuffer;
	printf ("allocated %d byte buffer for ABL\n", 
			player->bufferSizeBytes);
	
	// read from ExtAudioFile into sampleBuffer
	// TODO: handle end-of-file wraparound
	UInt32 framesReadIntoBuffer = 0;
	do {
		UInt32 framesRead = player->fileLengthFrames - framesReadIntoBuffer;
		bufferList->mBuffers[0].mData = sampleBuffer + (framesReadIntoBuffer * (sizeof(UInt16))); // 4/18/11 - fix 3
		CheckError(ExtAudioFileRead(player->extAudioFile, 
									&framesRead,
									bufferList),
				   "ExtAudioFileRead failed");
		framesReadIntoBuffer += framesRead;
		player->totalFramesRead += framesRead;
		printf ("read %d frames\n", framesRead);
	} while (framesReadIntoBuffer < (player->bufferSizeBytes / sizeof(UInt16))); // 4/18/11 - fix 4
	
	// copy from sampleBuffer to AL buffer
	alBufferData(alBuffer,
				 AL_FORMAT_MONO16,
				 sampleBuffer,
				 player->bufferSizeBytes,
				 player->dataFormat.mSampleRate);
	
	free (bufferList);
	free (sampleBuffer);
}

void refillALBuffers (MyStreamPlayer* player) {
	ALint processed;
	alGetSourcei (player->sources[0], AL_BUFFERS_PROCESSED, &processed);
	CheckALError ("couldn't get al_buffers_processed");
	
	while (processed > 0) {
		ALuint freeBuffer;
		alSourceUnqueueBuffers(player->sources[0], 1, &freeBuffer);
		CheckALError("couldn't unqueue buffer");
		printf ("refilling buffer %d\n", freeBuffer);
		fillALBuffer(player, freeBuffer);
		alSourceQueueBuffers(player->sources[0], 1, &freeBuffer);
		CheckALError ("couldn't queue refilled buffer");
		printf ("re-queued buffer %d\n", freeBuffer);
		processed--;
	}
	
}

#pragma mark main

int main (int argc, const char * argv[]) {
	MyStreamPlayer player;
	
	// prepare the ExtAudioFile for reading
	CheckError(setUpExtAudioFile(&player),
			   "Couldn't open ExtAudioFile") ;
	
	// set up OpenAL buffers
	ALCdevice* alDevice = alcOpenDevice(NULL);
	CheckALError ("Couldn't open AL device"); // default device
	ALCcontext* alContext = alcCreateContext(alDevice, 0);
	CheckALError ("Couldn't open AL context");
	alcMakeContextCurrent (alContext);
	CheckALError ("Couldn't make AL context current");
	ALuint buffers[BUFFER_COUNT];
	alGenBuffers(BUFFER_COUNT, buffers);
	CheckALError ("Couldn't generate buffers");
	
	for (int i=0; i<BUFFER_COUNT; i++) {
		fillALBuffer(&player, buffers[i]);
	}
	
	// set up streaming source
	alGenSources(1, player.sources);
	CheckALError ("Couldn't generate sources");
	alSourcef(player.sources[0], AL_GAIN, AL_MAX_GAIN);
	CheckALError("Couldn't set source gain");
	updateSourceLocation(player);
	CheckALError ("Couldn't set initial source position");
	
	// queue up the buffers on the source
	alSourceQueueBuffers(player.sources[0],
						 BUFFER_COUNT,
						 buffers);
	CheckALError("Couldn't queue buffers on source");
	
	// set up listener
	alListener3f (AL_POSITION, 0.0, 0.0, 0.0);
	CheckALError("Couldn't set listner position");
	
	// start playing
	alSourcePlayv (1, player.sources);
	CheckALError ("Couldn't play");
	
	// and wait
	printf("Playing...\n");
	time_t startTime = time(NULL);
	do
	{
		// get next theta
		updateSourceLocation(player);
		CheckALError ("Couldn't set source position");
		
		// refill buffers if needed
		refillALBuffers (&player);
		
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
	} while (difftime(time(NULL), startTime) < RUN_TIME);
	
	// cleanup:
	alSourceStop(player.sources[0]);
	alDeleteSources(1, player.sources);
	alDeleteBuffers(BUFFER_COUNT, buffers);
	alcDestroyContext(alContext);
	alcCloseDevice(alDevice);
	printf ("Bottom of main\n");
}

// from chris' openal streaming article... just leaving as notes so I can move between computers
// and have this in svn/dropbox

/*
 // set up and use function pointer to alBufferDataStaticProcPtr
 ALvoid  alBufferDataStaticProc(const ALint bid, ALenum format, ALvoid* data, ALsizei size, ALsizei freq)
 {
 static	alBufferDataStaticProcPtr	proc = NULL;
 
 if (proc == NULL) {
 proc = (alBufferDataStaticProcPtr) alGetProcAddress((const ALCchar*) "alBufferDataStatic");
 }
 
 if (proc)
 proc(bid, format, data, size, freq);
 
 return;
 }
 */