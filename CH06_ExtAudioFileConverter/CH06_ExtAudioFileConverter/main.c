#include <AudioToolbox/AudioToolbox.h>

#define kInputFileLocation	CFSTR("/Insert/Path/To/Audio/File.xxx")
// #define kInputFileLocation	CFSTR("/Users/kevin/Desktop/tmp_storage/audio_tests/cdsd_scratch.aiff")
// #define kInputFileLocation	CFSTR("/Volumes/Sephiroth/Tunes//菅野よう子/ESCAFLOWNE - ORIGINAL MOVIE SOUNDTRACK/We're flying.m4a")

// ml: I'm playing with the name of these structs because it's confusing to have a struct called audioConverter and an audioConverter called converter. I'm not married to it, and will see how it flows with the text and as the bigger picture becomes clear. This is generally true with the names of things.

typedef struct MyAudioConverterSettings
{
	AudioStreamBasicDescription outputFormat; // output file's data stream description
	
	ExtAudioFileRef					inputFile; // reference to your input file
	AudioFileID					outputFile; // reference to your output file
		
} MyAudioConverterSettings;

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
void Convert(MyAudioConverterSettings *mySettings)
{	
	
	UInt32 outputBufferSize = 32 * 1024; // 32 KB is a good starting point
	UInt32 sizePerPacket = mySettings->outputFormat.mBytesPerPacket;	
	UInt32 packetsPerBuffer = outputBufferSize / sizePerPacket;
	
	// allocate destination buffer
	UInt8 *outputBuffer = (UInt8 *)malloc(sizeof(UInt8) * outputBufferSize);
	
	UInt32 outputFilePacketPosition = 0; //in bytes
	while(1)
	{
		// wrap the destination buffer in an AudioBufferList
		AudioBufferList convertedData;
		convertedData.mNumberBuffers = 1;
		convertedData.mBuffers[0].mNumberChannels = mySettings->outputFormat.mChannelsPerFrame;
		convertedData.mBuffers[0].mDataByteSize = outputBufferSize;
		convertedData.mBuffers[0].mData = outputBuffer;
		
		UInt32 frameCount = packetsPerBuffer;
		
		// read from the extaudiofile
		CheckResult(ExtAudioFileRead(mySettings->inputFile,
									 &frameCount,
									 &convertedData),
					"Couldn't read from input file");
		
		if (frameCount == 0) {
			printf ("done reading from file");
			return;
		}
		
		// write the converted data to the output file
		CheckResult (AudioFileWritePackets(mySettings->outputFile,
										   FALSE,
										   frameCount,
										   NULL,
										   outputFilePacketPosition / mySettings->outputFormat.mBytesPerPacket, 
										   &frameCount,
										   convertedData.mBuffers[0].mData),
					 "Couldn't write packets to file");
		
		// advance the output file write location
		outputFilePacketPosition += (frameCount * mySettings->outputFormat.mBytesPerPacket);
	}
	
	// AudioConverterDispose(audioConverter);
}

int	main(int argc, const char *argv[])
{
 	MyAudioConverterSettings audioConverterSettings = {0};
	
	// open the input with ExtAudioFile
	CFURLRef inputFileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, kInputFileLocation, kCFURLPOSIXPathStyle, false);
	CheckResult(ExtAudioFileOpenURL(inputFileURL, 
									&audioConverterSettings.inputFile),
				"ExtAudioFileOpenURL failed");
	
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
	CFURLRef outputFileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, CFSTR("output.aif"), kCFURLPOSIXPathStyle, false);
	CheckResult (AudioFileCreateWithURL(outputFileURL, kAudioFileAIFFType, &audioConverterSettings.outputFormat, kAudioFileFlags_EraseFile, &audioConverterSettings.outputFile),
				 "AudioFileCreateWithURL failed");
    CFRelease(outputFileURL);
	
	// set the PCM format as the client format on the input ext audio file
	CheckResult(ExtAudioFileSetProperty(audioConverterSettings.inputFile,
										kExtAudioFileProperty_ClientDataFormat,
										sizeof (AudioStreamBasicDescription),
										&audioConverterSettings.outputFormat),
				"Couldn't set client data format on input ext file");
	
	fprintf(stdout, "Converting...\n");
	Convert(&audioConverterSettings);
	
cleanup:
	// AudioFileClose(audioConverterSettings.inputFile);
	ExtAudioFileDispose(audioConverterSettings.inputFile);
	AudioFileClose(audioConverterSettings.outputFile);
	return 0;
}
