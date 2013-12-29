#include <AudioToolbox/AudioToolbox.h>

#define kPlaybackFileLocation	CFSTR("/Insert/Path/To/Audio/File.xxx")
//#define kPlaybackFileLocation	CFSTR("/Users/cadamson/Library/Developer/Xcode/DerivedData/CH04_Recorder-dvninfofohfiwcgyndnhzarhsipp/Build/Products/Debug/output.caf")
//#define kPlaybackFileLocation	CFSTR("/Users/cadamson/audiofile.m4a")
//#define kPlaybackFileLocation	CFSTR("/Volumes/Sephiroth/Tunes/The Tubes/Tubes World Tour 2001/Wild Women of Wongo.m4p")
//#define kPlaybackFileLocation	CFSTR("/Volumes/Sephiroth/Tunes//菅野よう子/ESCAFLOWNE - ORIGINAL MOVIE SOUNDTRACK/We're flying.m4a")

void CalculateBytesForTime (AudioFileID inAudioFile, AudioStreamBasicDescription inDesc, Float64 inSeconds, UInt32 *outBufferSize, UInt32 *outNumPackets);


#define kNumberPlaybackBuffers	3
typedef struct MyPlayer {
	// AudioQueueRef				queue; // the audio queue object
	// AudioStreamBasicDescription dataFormat; // file's data stream description
	AudioFileID					playbackFile; // reference to your output file
	SInt64						packetPosition; // current packet index in output file
	UInt32						numPacketsToRead; // number of packets to read from file
	AudioStreamPacketDescription *packetDescs; // array of packet descriptions for read buffer
	// AudioQueueBufferRef			buffers[kNumberPlaybackBuffers];
	Boolean						isDone; // playback has completed
} MyPlayer;


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

// we only use time here as a guideline
// we're really trying to get somewhere between 16K and 64K buffers, but not allocate too much if we don't need it
void CalculateBytesForTime (AudioFileID inAudioFile, AudioStreamBasicDescription inDesc, Float64 inSeconds, UInt32 *outBufferSize, UInt32 *outNumPackets)
{
	
	// we need to calculate how many packets we read at a time, and how big a buffer we need.
	// we base this on the size of the packets in the file and an approximate duration for each buffer.
	//
	// first check to see what the max size of a packet is, if it is bigger than our default
	// allocation size, that needs to become larger
	UInt32 maxPacketSize;
	UInt32 propSize = sizeof(maxPacketSize);
	CheckError(AudioFileGetProperty(inAudioFile, kAudioFilePropertyPacketSizeUpperBound,
									&propSize, &maxPacketSize), "couldn't get file's max packet size");
	
	static const int maxBufferSize = 0x10000; // limit size to 64K
	static const int minBufferSize = 0x4000; // limit size to 16K
	
	if (inDesc.mFramesPerPacket) {
		Float64 numPacketsForTime = inDesc.mSampleRate / inDesc.mFramesPerPacket * inSeconds;
		*outBufferSize = numPacketsForTime * maxPacketSize;
	} else {
		// if frames per packet is zero, then the codec has no predictable packet == time
		// so we can't tailor this (we don't know how many Packets represent a time period
		// we'll just return a default buffer size
		*outBufferSize = maxBufferSize > maxPacketSize ? maxBufferSize : maxPacketSize;
	}
	
	// we're going to limit our size to our default
	if (*outBufferSize > maxBufferSize && *outBufferSize > maxPacketSize)
		*outBufferSize = maxBufferSize;
	else {
		// also make sure we're not too small - we don't want to go the disk for too small chunks
		if (*outBufferSize < minBufferSize)
			*outBufferSize = minBufferSize;
	}
	*outNumPackets = *outBufferSize / maxPacketSize;
}


// many encoded formats require a 'magic cookie'. if the file has a cookie we get it
// and configure the queue with it
static void MyCopyEncoderCookieToQueue(AudioFileID theFile, AudioQueueRef queue ) {
	UInt32 propertySize;
	OSStatus result = AudioFileGetPropertyInfo (theFile, kAudioFilePropertyMagicCookieData, &propertySize, NULL);
	if (result == noErr && propertySize > 0)
	{
		Byte* magicCookie = (UInt8*)malloc(sizeof(UInt8) * propertySize);	
		CheckError(AudioFileGetProperty (theFile, kAudioFilePropertyMagicCookieData, &propertySize, magicCookie), "get cookie from file failed");
		CheckError(AudioQueueSetProperty(queue, kAudioQueueProperty_MagicCookie, magicCookie, propertySize), "set cookie on queue failed");
		free(magicCookie);
	}
}


#pragma mark - audio queue -

static void MyAQOutputCallback(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inCompleteAQBuffer) 
{
	MyPlayer *aqp = (MyPlayer*)inUserData;
	if (aqp->isDone) return;
	
	// read audio data from file into supplied buffer
	UInt32 numBytes;
	UInt32 nPackets = aqp->numPacketsToRead;	
	CheckError(AudioFileReadPackets(aqp->playbackFile,
									false,
									&numBytes,
									aqp->packetDescs,
									aqp->packetPosition,
									&nPackets,
									inCompleteAQBuffer->mAudioData),
			   "AudioFileReadPackets failed");
	
	// enqueue buffer into the Audio Queue
	// if nPackets == 0 it means we are EOF (all data has been read from file)
	if (nPackets > 0)
	{
		inCompleteAQBuffer->mAudioDataByteSize = numBytes;		
		AudioQueueEnqueueBuffer(inAQ,
								inCompleteAQBuffer,
								(aqp->packetDescs ? nPackets : 0),
								aqp->packetDescs);
		aqp->packetPosition += nPackets;
	}
	else
	{
		CheckError(AudioQueueStop(inAQ, false), "AudioQueueStop failed");
		aqp->isDone = true;
	}
}

int	main(int argc, const char *argv[])
{
	MyPlayer player = {0};
	
	CFURLRef myFileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, kPlaybackFileLocation, kCFURLPOSIXPathStyle, false);
	
	// open the audio file
	//	CheckError(AudioFileOpenURL(myFileURL, fsRdPerm, 0, &player.playbackFile), "AudioFileOpenURL failed");
	CheckError(AudioFileOpenURL(myFileURL, kAudioFileReadPermission, 0, &player.playbackFile), "AudioFileOpenURL failed");
	CFRelease(myFileURL);
	
	// get the audio data format from the file
	AudioStreamBasicDescription dataFormat;
	UInt32 propSize = sizeof(dataFormat);
	CheckError(AudioFileGetProperty(player.playbackFile, kAudioFilePropertyDataFormat,
									&propSize, &dataFormat), "couldn't get file's data format");
	
	// create a output (playback) queue
	AudioQueueRef queue;
	CheckError(AudioQueueNewOutput(&dataFormat, // ASBD
								   MyAQOutputCallback, // Callback
								   &player, // user data
								   NULL, // run loop
								   NULL, // run loop mode
								   0, // flags (always 0)
								   &queue), // output: reference to AudioQueue object
			   "AudioQueueNewOutput failed");
	
	
	// adjust buffer size to represent about a half second (0.5) of audio based on this format
 	UInt32 bufferByteSize;
	CalculateBytesForTime(player.playbackFile, dataFormat,  0.5, &bufferByteSize, &player.numPacketsToRead);
	
	// check if we are dealing with a VBR file. ASBDs for VBR files always have 
	// mBytesPerPacket and mFramesPerPacket as 0 since they can fluctuate at any time.
	// If we are dealing with a VBR file, we allocate memory to hold the packet descriptions
	bool isFormatVBR = (dataFormat.mBytesPerPacket == 0 || dataFormat.mFramesPerPacket == 0);
	if (isFormatVBR)
		player.packetDescs = (AudioStreamPacketDescription*)malloc(sizeof(AudioStreamPacketDescription) * player.numPacketsToRead);
	else
		player.packetDescs = NULL; // we don't provide packet descriptions for constant bit rate formats (like linear PCM)
	
	// get magic cookie from file and set on queue
	MyCopyEncoderCookieToQueue(player.playbackFile, queue);
	
	// allocate the buffers and prime the queue with some data before starting
	AudioQueueBufferRef	buffers[kNumberPlaybackBuffers];
	player.isDone = false;
	player.packetPosition = 0;
	int i;
	for (i = 0; i < kNumberPlaybackBuffers; ++i)
	{
		CheckError(AudioQueueAllocateBuffer(queue, bufferByteSize, &buffers[i]), "AudioQueueAllocateBuffer failed");
		
		// manually invoke callback to fill buffers with data
		MyAQOutputCallback(&player, queue, buffers[i]);
		
		// EOF (the entire file's contents fit in the buffers)
		if (player.isDone)
			break;
	}	
	
	
	//CheckError(AudioQueueAddPropertyListener(aqp.queue, kAudioQueueProperty_IsRunning, MyAQPropertyListenerCallback, &aqp), "AudioQueueAddPropertyListener(kAudioQueueProperty_IsRunning) failed");
	
	// start the queue. this function returns immedatly and begins
	// invoking the callback, as needed, asynchronously.
	CheckError(AudioQueueStart(queue, NULL), "AudioQueueStart failed");
	
	// and wait
	printf("Playing...\n");
	do
	{
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
	} while (!player.isDone /*|| gIsRunning*/);
	
	// isDone represents the state of the Audio File enqueuing. This does not mean the
	// Audio Queue is actually done playing yet. Since we have 3 half-second buffers in-flight
	// run for continue to run for a short additional time so they can be processed
	CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, false);
	
	// end playback
	player.isDone = true;
	CheckError(AudioQueueStop(queue, TRUE), "AudioQueueStop failed");
	
cleanup:
	AudioQueueDispose(queue, TRUE);
	AudioFileClose(player.playbackFile);
	
	return 0;
}
