#include <AudioToolbox/AudioToolbox.h>
#include <unistd.h> // for usleep()

#define kInputFileLocation CFSTR ("/Users/abdullahbakhach/Documents/abbasWuHussein.mp3")
// #define kInputFileLocation	CFSTR("/Volumes/Galactica/Music/Dubee - its the crest.mp3")
// #define kInputFileLocation CFSTR("/Volumes/Sephiroth/Tunes/Amazon MP3/Metric/Fantasies/06 - Gimme Sympathy.mp3")

typedef struct MyAUGraphPlayer
{
	AudioStreamBasicDescription inputFormat; // input file's data stream description
	AudioFileID					inputFile; // reference to your input file
	
	AUGraph graph;
	AudioUnit fileAU;
	
} MyAUGraphPlayer;

void CreateMyAUGraph(MyAUGraphPlayer *player);
double PrepareFileAU(MyAUGraphPlayer *player);

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


#pragma mark - audio converter -

void CreateMyAUGraph(MyAUGraphPlayer *player)
{
	// create a new AUGraph
	CheckError(NewAUGraph(&player->graph),
			   "NewAUGraph failed");
	
	// generate description that will match out output device (speakers)
	AudioComponentDescription outputcd = {0};
	outputcd.componentType = kAudioUnitType_Output;
	outputcd.componentSubType = kAudioUnitSubType_DefaultOutput;
	outputcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	
	// adds a node with above description to the graph
	AUNode outputNode;
	CheckError(AUGraphAddNode(player->graph, &outputcd, &outputNode),
			   "AUGraphAddNode[kAudioUnitSubType_DefaultOutput] failed");
	
	// generate description that will match a generator AU of type: audio file player
	AudioComponentDescription fileplayercd = {0};
	fileplayercd.componentType = kAudioUnitType_Generator;
	fileplayercd.componentSubType = kAudioUnitSubType_AudioFilePlayer;
	fileplayercd.componentManufacturer = kAudioUnitManufacturer_Apple;
	
	// adds a node with above description to the graph
	AUNode fileNode;
	CheckError(AUGraphAddNode(player->graph, &fileplayercd, &fileNode),
			   "AUGraphAddNode[kAudioUnitSubType_AudioFilePlayer] failed");
	
	// opening the graph opens all contained audio units but does not allocate any resources yet
	CheckError(AUGraphOpen(player->graph),
			   "AUGraphOpen failed");
	
	// get the reference to the AudioUnit object for the file player graph node
	CheckError(AUGraphNodeInfo(player->graph, fileNode, NULL, &player->fileAU),
			   "AUGraphNodeInfo failed");
	
	// connect the output source of the file player AU to the input source of the output node
	CheckError(AUGraphConnectNodeInput(player->graph, fileNode, 0, outputNode, 0),
			   "AUGraphConnectNodeInput");
	
	// now initialize the graph (causes resources to be allocated)
	CheckError(AUGraphInitialize(player->graph),
			   "AUGraphInitialize failed");
}

double PrepareFileAU(MyAUGraphPlayer *player)
{
	
	// tell the file player unit to load the file we want to play
	CheckError(AudioUnitSetProperty(player->fileAU, kAudioUnitProperty_ScheduledFileIDs, 
									kAudioUnitScope_Global, 0, &player->inputFile, sizeof(player->inputFile)),
			   "AudioUnitSetProperty[kAudioUnitProperty_ScheduledFileIDs] failed");
	
	UInt64 nPackets;
	UInt32 propsize = sizeof(nPackets);
	CheckError(AudioFileGetProperty(player->inputFile, kAudioFilePropertyAudioDataPacketCount,
									&propsize, &nPackets),
			   "AudioFileGetProperty[kAudioFilePropertyAudioDataPacketCount] failed");
	
	// tell the file player AU to play the entire file
	ScheduledAudioFileRegion rgn;
	memset (&rgn.mTimeStamp, 0, sizeof(rgn.mTimeStamp));
	rgn.mTimeStamp.mFlags = kAudioTimeStampSampleTimeValid;
	rgn.mTimeStamp.mSampleTime = 0;
	rgn.mCompletionProc = NULL;
	rgn.mCompletionProcUserData = NULL;
	rgn.mAudioFile = player->inputFile;
	rgn.mLoopCount = 1;
	rgn.mStartFrame = 0;
	rgn.mFramesToPlay = nPackets * player->inputFormat.mFramesPerPacket;
	
	CheckError(AudioUnitSetProperty(player->fileAU, kAudioUnitProperty_ScheduledFileRegion, 
									kAudioUnitScope_Global, 0,&rgn, sizeof(rgn)),
			   "AudioUnitSetProperty[kAudioUnitProperty_ScheduledFileRegion] failed");
	
	// prime the file player AU with default values
	UInt32 defaultVal = 0;
	CheckError(AudioUnitSetProperty(player->fileAU, kAudioUnitProperty_ScheduledFilePrime, 
									kAudioUnitScope_Global, 0, &defaultVal, sizeof(defaultVal)),
			   "AudioUnitSetProperty[kAudioUnitProperty_ScheduledFilePrime] failed");
	
	// tell the file player AU when to start playing (-1 sample time means next render cycle)
	AudioTimeStamp startTime;
	memset (&startTime, 0, sizeof(startTime));
	startTime.mFlags = kAudioTimeStampSampleTimeValid;
	startTime.mSampleTime = -1;
	CheckError(AudioUnitSetProperty(player->fileAU, kAudioUnitProperty_ScheduleStartTimeStamp, 
									kAudioUnitScope_Global, 0, &startTime, sizeof(startTime)),
			   "AudioUnitSetProperty[kAudioUnitProperty_ScheduleStartTimeStamp]");
	
	// file duration
	return (nPackets * player->inputFormat.mFramesPerPacket) / player->inputFormat.mSampleRate;
}

#pragma mark - main - 
int	main(int argc, const char *argv[])
{
	CFURLRef inputFileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, kInputFileLocation, kCFURLPOSIXPathStyle, false);
 	MyAUGraphPlayer player = {0};
	
	// open the input audio file
	CheckError(AudioFileOpenURL(inputFileURL, kAudioFileReadPermission, 0, &player.inputFile),
			   "AudioFileOpenURL failed");
	CFRelease(inputFileURL);
	
	// get the audio data format from the file
	UInt32 propSize = sizeof(player.inputFormat);
	CheckError(AudioFileGetProperty(player.inputFile, kAudioFilePropertyDataFormat,
									&propSize, &player.inputFormat),
			   "couldn't get file's data format");
	
	// build a basic fileplayer->speakers graph
	CreateMyAUGraph(&player);
	
	// configure the file player
	Float64 fileDuration = PrepareFileAU(&player);
	
	// start playing
	CheckError(AUGraphStart(player.graph),
			   "AUGraphStart failed");
	
	// sleep until the file is finished
	usleep ((int)(fileDuration * 1000.0 * 1000.0));
	
cleanup:
	AUGraphStop (player.graph);
	AUGraphUninitialize (player.graph);
	AUGraphClose(player.graph);
	AudioFileClose(player.inputFile);
	
	return 0;
}
