#include <AudioToolbox/AudioToolbox.h>

#ifndef MAC_OS_X_VERSION_10_7
// CoreServices defines eofErr, replaced in 10.7 by kAudioFileEndOfFileError
#include <CoreServices/CoreServices.h>
#endif


#define kInputFileLocation	CFSTR("/Insert/Path/To/Audio/File.xxx")
// #define kInputFileLocation	CFSTR("/Users/kevin/Desktop/tmp_storage/audio_tests/cdsd_scratch.aiff")
// #define kInputFileLocation	CFSTR("/Volumes/Sephiroth/Tunes//菅野よう子/ESCAFLOWNE - ORIGINAL MOVIE SOUNDTRACK/We're flying.m4a")

typedef struct MyAudioConverterSettings
{
	AudioStreamBasicDescription inputFormat; // input file's data stream description
	AudioStreamBasicDescription outputFormat; // output file's data stream description
	
	AudioFileID					inputFile; // reference to your input file
	AudioFileID					outputFile; // reference to your output file
	
	UInt64						inputFilePacketIndex; // current packet index in input file
	UInt64						inputFilePacketCount; // total number of packts in input file
	UInt32						inputFilePacketMaxSize; // maximum size a packet in the input file can be
	AudioStreamPacketDescription *inputFilePacketDescriptions; // array of packet descriptions for read buffer
	
	// KEVIN: sourceBuffer is never used outside of the callback. why couldn't it be a local there?
	void *sourceBuffer;
	
} MyAudioConverterSettings;


OSStatus MyAudioConverterCallback(AudioConverterRef inAudioConverter,
								  UInt32 *ioDataPacketCount,
								  AudioBufferList *ioData,
								  AudioStreamPacketDescription **outDataPacketDescription,
								  void *inUserData);
void Convert(MyAudioConverterSettings *mySettings);


#pragma mark - utility functions -

// generic error handler - if result is nonzero, prints error message and exits program.
static void CheckResult(OSStatus result, const char *operation)
{
	if (result == noErr) return;
	
	char errorString[20];
	// see if it appears to be a 4-char-code
	*(UInt32 *)(errorString + 1) = CFSwapInt32HostToBig(result);
	if (isprint(errorString[1]) && isprint(errorString[2]) && isprint(errorString[3]) && isprint(errorString[4])) {
		errorString[0] = errorString[5] = '\'';
		errorString[6] = '\0';
	} else
		// no, format it as an integer
		sprintf(errorString, "%d", (int)result);
	
	fprintf(stderr, "Error: %s (%s)\n", operation, errorString);
	
	exit(1);
}


#pragma mark - audio converter -

OSStatus MyAudioConverterCallback(AudioConverterRef inAudioConverter,
								  UInt32 *ioDataPacketCount,
								  AudioBufferList *ioData,
								  AudioStreamPacketDescription **outDataPacketDescription,
								  void *inUserData)
{
	MyAudioConverterSettings *audioConverterSettings = (MyAudioConverterSettings *)inUserData;
	
	// initialize in case of failure
    ioData->mBuffers[0].mData = NULL;			
    ioData->mBuffers[0].mDataByteSize = 0;
	
    // if there are not enough packets to satisfy request, then read what's left
    if (audioConverterSettings->inputFilePacketIndex + *ioDataPacketCount > audioConverterSettings->inputFilePacketCount)
        *ioDataPacketCount = audioConverterSettings->inputFilePacketCount - audioConverterSettings->inputFilePacketIndex;
	
	if(*ioDataPacketCount == 0)
        return noErr;
    
    if (audioConverterSettings->sourceBuffer != NULL)
    {
        free(audioConverterSettings->sourceBuffer);
        audioConverterSettings->sourceBuffer = NULL;
    }
    
    audioConverterSettings->sourceBuffer = (void *)calloc(1, *ioDataPacketCount * audioConverterSettings->inputFilePacketMaxSize);        
	
    UInt32 outByteCount = 0;
	OSStatus result = AudioFileReadPackets(audioConverterSettings->inputFile, 
										   true, 
										   &outByteCount, 
										   audioConverterSettings->inputFilePacketDescriptions, 
										   audioConverterSettings->inputFilePacketIndex,
										   ioDataPacketCount, 
										   audioConverterSettings->sourceBuffer);
    
	// it's not an error if we just read the remainder of the file
#ifdef MAC_OS_X_VERSION_10_7
    if (result == kAudioFileEndOfFileError && *ioDataPacketCount) result = noErr;
#else
    if (result == eofErr && *ioDataPacketCount) result = noErr;
#endif
    else if (result != noErr) return result;
	
    audioConverterSettings->inputFilePacketIndex += *ioDataPacketCount;
    
	// KEVIN: in "// initialize in case of failure", we assumed there was only 1
	// buffer (since we set it up ourselves in Convert()). so why be careful to
	// iterate over potentially multiple buffers here?
	/*
	 UInt32 bufferIndex;
	 for (bufferIndex = 0; bufferIndex < ioData->mNumberBuffers; bufferIndex++)
	 {
	 ioData->mBuffers[bufferIndex].mData = audioConverterSettings->sourceBuffer;
	 ioData->mBuffers[bufferIndex].mDataByteSize = outByteCount;
	 }
	 */
	// chris' hacky asssume-one-buffer equivalent
	ioData->mBuffers[0].mData = audioConverterSettings->sourceBuffer;
	ioData->mBuffers[0].mDataByteSize = outByteCount;
	
    if (outDataPacketDescription)
        *outDataPacketDescription = audioConverterSettings->inputFilePacketDescriptions;
	
    return result;	
}

void Convert(MyAudioConverterSettings *mySettings)
{
	// create audioConverter object
	AudioConverterRef	audioConverter;
    CheckResult (AudioConverterNew(&mySettings->inputFormat, &mySettings->outputFormat, &audioConverter),
				 "AudioConveterNew failed");
	
	// allocate packet descriptions if the input file is VBR
	UInt32 packetsPerBuffer = 0;
	UInt32 outputBufferSize = 32 * 1024; // 32 KB is a good starting point
	UInt32 sizePerPacket = mySettings->inputFormat.mBytesPerPacket;	
	if (sizePerPacket == 0)
	{
		UInt32 size = sizeof(sizePerPacket);
        CheckResult(AudioConverterGetProperty(audioConverter, kAudioConverterPropertyMaximumOutputPacketSize, &size, &sizePerPacket),
					"Couldn't get kAudioConverterPropertyMaximumOutputPacketSize");
		
        // make sure the buffer is large enough to hold at least one packet
		if (sizePerPacket > outputBufferSize)
			outputBufferSize = sizePerPacket;
		
		packetsPerBuffer = outputBufferSize / sizePerPacket;
		mySettings->inputFilePacketDescriptions = (AudioStreamPacketDescription*)malloc(sizeof(AudioStreamPacketDescription) * packetsPerBuffer);
		
	}
	else
	{
		packetsPerBuffer = outputBufferSize / sizePerPacket;
	}
	
	// allocate destination buffer
	UInt8 *outputBuffer = (UInt8 *)malloc(sizeof(UInt8) * outputBufferSize); // CHRIS: not sizeof(UInt8*). check book text!
	
	UInt32 outputFilePacketPosition = 0; //in bytes
	while(1)
	{
		// wrap the destination buffer in an AudioBufferList
		AudioBufferList convertedData;
		convertedData.mNumberBuffers = 1;
		convertedData.mBuffers[0].mNumberChannels = mySettings->inputFormat.mChannelsPerFrame;
		convertedData.mBuffers[0].mDataByteSize = outputBufferSize;
		convertedData.mBuffers[0].mData = outputBuffer;
		
		// now call the audioConverter to transcode the data. This function will call
		// the callback function as many times as required to fulfill the request.
		UInt32 ioOutputDataPackets = packetsPerBuffer;
		OSStatus error = AudioConverterFillComplexBuffer(audioConverter, 
														 MyAudioConverterCallback, 
														 mySettings, 
														 &ioOutputDataPackets, 
														 &convertedData, 
														 (mySettings->inputFilePacketDescriptions ? mySettings->inputFilePacketDescriptions : nil));
		if (error || !ioOutputDataPackets)
		{
			//		fprintf(stderr, "err: %ld, packets: %ld\n", err, ioOutputDataPackets);
			break;	// this is our termination condition
		}
		
		// write the converted data to the output file
		// KEVIN: QUESTION: 3rd arg seems like it should be a byte count, not packets. why does this work?
		CheckResult (AudioFileWritePackets(mySettings->outputFile,
										   FALSE,
										   ioOutputDataPackets,
										   NULL,
										   outputFilePacketPosition / mySettings->outputFormat.mBytesPerPacket, 
										   &ioOutputDataPackets,
										   convertedData.mBuffers[0].mData),
					 "Couldn't write packets to file");
		
		// advance the output file write location
		outputFilePacketPosition += (ioOutputDataPackets * mySettings->outputFormat.mBytesPerPacket);
	}
	
	AudioConverterDispose(audioConverter);
}

int	main(int argc, const char *argv[])
{
 	MyAudioConverterSettings audioConverterSettings = {0};
	
	// open the input audio file
	CFURLRef inputFileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, kInputFileLocation, kCFURLPOSIXPathStyle, false);
    CheckResult (AudioFileOpenURL(inputFileURL, kAudioFileReadPermission , 0, &audioConverterSettings.inputFile),
				 "AudioFileOpenURL failed");
	CFRelease(inputFileURL);
	
	// get the audio data format from the file
	UInt32 propSize = sizeof(audioConverterSettings.inputFormat);
    CheckResult (AudioFileGetProperty(audioConverterSettings.inputFile, kAudioFilePropertyDataFormat, &propSize, &audioConverterSettings.inputFormat),
				 "couldn't get file's data format");
	
	// get the total number of packets in the file
	propSize = sizeof(audioConverterSettings.inputFilePacketCount);
    CheckResult (AudioFileGetProperty(audioConverterSettings.inputFile, kAudioFilePropertyAudioDataPacketCount, &propSize, &audioConverterSettings.inputFilePacketCount),
				 "couldn't get file's packet count");
	
	// get size of the largest possible packet
	propSize = sizeof(audioConverterSettings.inputFilePacketMaxSize);
    CheckResult(AudioFileGetProperty(audioConverterSettings.inputFile, kAudioFilePropertyMaximumPacketSize, &propSize, &audioConverterSettings.inputFilePacketMaxSize),
				"couldn't get file's max packet size");
	
	// define the ouput format. AudioConverter requires that one of the data formats be LPCM
    audioConverterSettings.outputFormat.mSampleRate = 44100.0;
	audioConverterSettings.outputFormat.mFormatID = kAudioFormatLinearPCM;
    audioConverterSettings.outputFormat.mFormatFlags = kAudioFormatFlagIsBigEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	audioConverterSettings.outputFormat.mBytesPerPacket = 4;
	audioConverterSettings.outputFormat.mFramesPerPacket = 1;
	audioConverterSettings.outputFormat.mBytesPerFrame = 4;
	audioConverterSettings.outputFormat.mChannelsPerFrame = 2;
	audioConverterSettings.outputFormat.mBitsPerChannel = 16;
	
	// create output file
	// KEVIN: TODO: this fails if file exists. isn't there an overwrite flag we can use?
	CFURLRef outputFileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, CFSTR("output.aif"), kCFURLPOSIXPathStyle, false);
	CheckResult (AudioFileCreateWithURL(outputFileURL, kAudioFileAIFFType, &audioConverterSettings.outputFormat, kAudioFileFlags_EraseFile, &audioConverterSettings.outputFile),
				 "AudioFileCreateWithURL failed");
    CFRelease(outputFileURL);
	
	fprintf(stdout, "Converting...\n");
	Convert(&audioConverterSettings);
	
cleanup:
	AudioFileClose(audioConverterSettings.inputFile);
	AudioFileClose(audioConverterSettings.outputFile);
	printf("Done\r");
	return 0;
}
