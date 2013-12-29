#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

int main (int argc, const char * argv[]) {
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
	
	if (argc < 2) {
		printf ("Usage: CAMetadata /full/path/to/audiofile\n");
		return -1;
	} // 1
	
	NSString *audioFilePath = [[NSString stringWithUTF8String:argv[1]]
                                    stringByExpandingTildeInPath];	// 2
	NSURL *audioURL = [NSURL fileURLWithPath:audioFilePath];	// 3
	NSLog (@"audioURL: %@", audioURL);
	AudioFileID audioFile;	// 4
	OSStatus theErr = noErr;	// 5
	theErr = AudioFileOpenURL((CFURLRef)audioURL, kAudioFileReadPermission, 0, &audioFile); // 6
	assert (theErr == noErr);	// 7
	UInt32 dictionarySize = 0;	// 8
	theErr = AudioFileGetPropertyInfo (audioFile, kAudioFilePropertyInfoDictionary,
									   &dictionarySize, 0); // 9
	assert (theErr == noErr);	// 10
	CFDictionaryRef dictionary;	// 11
	theErr = AudioFileGetProperty (audioFile, kAudioFilePropertyInfoDictionary,
								   &dictionarySize, &dictionary); // 12
	assert (theErr == noErr);	// 13
	NSLog (@"dictionary: %@", dictionary);	// 14
	CFRelease (dictionary);	// 15
	theErr = AudioFileClose (audioFile);	// 16
	assert (theErr == noErr);	// 17
	
    [pool drain];
    return 0;
}
