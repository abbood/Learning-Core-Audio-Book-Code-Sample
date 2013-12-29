#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

#define SAMPLE_RATE 44100	// 1
#define DURATION 5.0	// 2
// #define FILENAME_FORMAT @"%0.3f-square.aif"
// #define FILENAME_FORMAT @"%0.3f-saw.aif"
#define FILENAME_FORMAT @"%0.3f-sine.aif"

int main (int argc, const char * argv[]) {
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
	
	if (argc < 2) {
		printf ("Usage: CAToneFileGenerator n\n(where n is tone in Hz)");
		return -1;
	} // 1
	
	double hz = atof(argv[1]);	// 2
	assert (hz > 0);
	NSLog (@"generating %f hz tone", hz);
	
	NSString *fileName = [NSString stringWithFormat:FILENAME_FORMAT, hz];
	NSString *filePath = [[[NSFileManager defaultManager] currentDirectoryPath]
						  stringByAppendingPathComponent: fileName];
	NSURL *fileURL = [NSURL fileURLWithPath: filePath];
	NSLog (@"path: %@", fileURL);
	
	// prepare the format
	AudioStreamBasicDescription asbd;
	memset(&asbd, 0, sizeof(asbd));
	asbd.mSampleRate = SAMPLE_RATE;
	asbd.mFormatID = kAudioFormatLinearPCM;
	asbd.mFormatFlags = kAudioFormatFlagIsBigEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	asbd.mChannelsPerFrame = 1;
	asbd.mFramesPerPacket = 1;
	asbd.mBitsPerChannel = 16;
	asbd.mBytesPerFrame = 2;
	asbd.mBytesPerPacket = 2;
	
	// set up the file
	AudioFileID audioFile;
	OSStatus audioErr = noErr;
	audioErr = AudioFileCreateWithURL((CFURLRef)fileURL,
									  kAudioFileAIFFType,
									  &asbd,
									  kAudioFileFlags_EraseFile,
									  &audioFile);
	assert (audioErr == noErr);
	
	// start writing samples
	long maxSampleCount = SAMPLE_RATE * DURATION;
	long sampleCount = 0;
	UInt32 bytesToWrite = 2;
	double wavelengthInSamples = SAMPLE_RATE / hz;
	NSLog (@"wavelengthInSamples = %f", wavelengthInSamples);
	
	while (sampleCount < maxSampleCount) {
		for (int i=0; i<wavelengthInSamples; i++) {
			/*
			 // square wave
			 SInt16 sample;
			 if (i < wavelengthInSamples/2) {
			 sample = CFSwapInt16HostToBig (SHRT_MAX);
			 } else {
			 sample = CFSwapInt16HostToBig (SHRT_MIN);
			 }
			 audioErr = AudioFileWriteBytes(audioFile,
			 false,
			 sampleCount*2, 
			 &bytesToWrite,
			 &sample);
			 assert (audioErr == noErr);
			 */
			
			/*
			 // saw wave
			 SInt16 sample = CFSwapInt16HostToBig (((i / wavelengthInSamples) * SHRT_MAX *2) -
			 SHRT_MAX);
			 audioErr = AudioFileWriteBytes(audioFile,
			 false,
			 sampleCount*2, 
			 &bytesToWrite,
			 &sample);
			 assert (audioErr == noErr);
			 sampleCount++;
			 */
			// sine wave
			SInt16 sample = CFSwapInt16HostToBig ((SInt16) SHRT_MAX *
												  sin (2 * M_PI * 
													   (i / wavelengthInSamples)));
			
			
			audioErr = AudioFileWriteBytes(audioFile,
										   false,
										   sampleCount*2, 
										   &bytesToWrite,
										   &sample);
			assert (audioErr == noErr);
			sampleCount++;
			
			
		}
	}
	audioErr = AudioFileClose(audioFile);
	assert (audioErr == noErr);
	NSLog (@"wrote %ld samples", sampleCount);
	
	[pool drain];
    return 0;
}
