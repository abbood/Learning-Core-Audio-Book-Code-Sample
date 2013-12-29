#include <AudioToolbox/AudioToolbox.h>
#include <ApplicationServices/ApplicationServices.h>

#define PART_II

typedef struct MyAUGraphPlayer
{
	//	AudioStreamBasicDescription streamFormat; // ASBD to use in the graph
	
	AUGraph graph;
	AudioUnit speechAU;
	
} MyAUGraphPlayer;

void CreateMyAUGraph(MyAUGraphPlayer *player);
void PrepareSpeechAU(MyAUGraphPlayer *player);

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

void CreateMyAUGraph(MyAUGraphPlayer *player)
{
	// create a new AUGraph
	CheckError(NewAUGraph(&player->graph),
			   "NewAUGraph failed");
	
	// generate description that will match our output device (speakers)
	AudioComponentDescription outputcd = {0};
	outputcd.componentType = kAudioUnitType_Output;
	outputcd.componentSubType = kAudioUnitSubType_DefaultOutput;
	outputcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	
	// adds a node with above description to the graph
	AUNode outputNode;
	CheckError(AUGraphAddNode(player->graph, &outputcd, &outputNode),
			   "AUGraphAddNode[kAudioUnitSubType_DefaultOutput] failed");
	
	// generate description that will match a generator AU of type: speech synthesizer
	AudioComponentDescription speechcd = {0};
	speechcd.componentType = kAudioUnitType_Generator;
	speechcd.componentSubType = kAudioUnitSubType_SpeechSynthesis;
	speechcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	
	// adds a node with above description to the graph
	AUNode speechNode;
	CheckError(AUGraphAddNode(player->graph, &speechcd, &speechNode),
			   "AUGraphAddNode[kAudioUnitSubType_SpeechSynthesis] failed");
	
	// opening the graph opens all contained audio units but does not allocate any resources yet
	CheckError(AUGraphOpen(player->graph),
			   "AUGraphOpen failed");
	
	// get the reference to the AudioUnit object for the speech synthesis graph node
	CheckError(AUGraphNodeInfo(player->graph, speechNode, NULL, &player->speechAU),
			   "AUGraphNodeInfo failed");
	
	//	// debug - get the asbd
	//	UInt32 propSize = sizeof (AudioStreamBasicDescription);
	//	CheckError(AudioUnitGetProperty(player->speechAU,
	//									kAudioUnitProperty_StreamFormat,
	//									kAudioUnitScope_Output,
	//									0,
	//									&player->streamFormat,
	//									&propSize),
	//			   "Couldn't get ASBD");	
	
#ifdef PART_II
	//
	// FUN! re-route the speech thru a reverb effect before sending to speakers
	//
	// generate description that will match out reverb effect
	AudioComponentDescription reverbcd = {0};
	reverbcd.componentType = kAudioUnitType_Effect;
	reverbcd.componentSubType = kAudioUnitSubType_MatrixReverb;
	reverbcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	
	// adds a node with above description to the graph
	AUNode reverbNode;
	CheckError(AUGraphAddNode(player->graph, &reverbcd, &reverbNode),
			   "AUGraphAddNode[kAudioUnitSubType_MatrixReverb] failed");
	
	// connect the output source of the speech synthesizer AU to the input source of the reverb node
	CheckError(AUGraphConnectNodeInput(player->graph, speechNode, 0, reverbNode, 0),
			   "AUGraphConnectNodeInput");
	
	// connect the output source of the reverb AU to the input source of the output node
	CheckError(AUGraphConnectNodeInput(player->graph, reverbNode, 0, outputNode, 0),
			   "AUGraphConnectNodeInput");
	
	// get the reference to the AudioUnit object for the reverb graph node
	AudioUnit reverbUnit;
	CheckError(AUGraphNodeInfo(player->graph, reverbNode, NULL, &reverbUnit),
			   "AUGraphNodeInfo failed");
	
	/*
	 enum {
	 kReverbRoomType_SmallRoom		= 0,
	 kReverbRoomType_MediumRoom		= 1,
	 kReverbRoomType_LargeRoom		= 2,
	 kReverbRoomType_MediumHall		= 3,
	 kReverbRoomType_LargeHall		= 4,
	 kReverbRoomType_Plate			= 5,
	 kReverbRoomType_MediumChamber	= 6,
	 kReverbRoomType_LargeChamber	= 7,
	 kReverbRoomType_Cathedral		= 8,
	 kReverbRoomType_LargeRoom2		= 9,
	 kReverbRoomType_MediumHall2		= 10,
	 kReverbRoomType_MediumHall3		= 11,
	 kReverbRoomType_LargeHall2		= 12	
	 };
	 
	 */
	
	// now initialize the graph (causes resources to be allocated)
	CheckError(AUGraphInitialize(player->graph),
			   "AUGraphInitialize failed");
	
	
	// set the reverb preset for room size
	//	UInt32 roomType = kReverbRoomType_SmallRoom;
	//	UInt32 roomType = kReverbRoomType_MediumRoom;
	UInt32 roomType = kReverbRoomType_LargeHall;
	//	UInt32 roomType = kReverbRoomType_Cathedral;
	
	CheckError(AudioUnitSetProperty(reverbUnit, kAudioUnitProperty_ReverbRoomType, 
									kAudioUnitScope_Global, 0, &roomType, sizeof(UInt32)),
			   "AudioUnitSetProperty[kAudioUnitProperty_ReverbRoomType] failed");
	
	
#else
	
	// connect the output source of the speech synthesis AU to the input source of the output node
	CheckError(AUGraphConnectNodeInput(player->graph, speechNode, 0, outputNode, 0),
			   "AUGraphConnectNodeInput");
	
	// now initialize the graph (causes resources to be allocated)
	CheckError(AUGraphInitialize(player->graph),
			   "AUGraphInitialize failed");
	
#endif
	CAShow(player->graph);
}

void PrepareSpeechAU(MyAUGraphPlayer *player)
{
	SpeechChannel chan;
	
	UInt32 propsize = sizeof(SpeechChannel);
	CheckError(AudioUnitGetProperty(player->speechAU, kAudioUnitProperty_SpeechChannel,
									kAudioUnitScope_Global, 0, &chan, &propsize),
			   "AudioUnitGetProperty[kAudioUnitProperty_SpeechChannel] failed");
	
	SpeakCFString(chan, CFSTR("hello world"), NULL);
}

#pragma mark main

int	main(int argc, const char *argv[])
{
 	MyAUGraphPlayer player = {0};
		
	// build a basic speech->speakers graph
	CreateMyAUGraph(&player);
	
	// configure the speech synthesizer
	PrepareSpeechAU(&player);
	
	// start playing
	CheckError(AUGraphStart(player.graph), "AUGraphStart failed");
	
	// sleep a while so the speech can play out
	usleep ((int)(10 * 1000. * 1000.));
	
cleanup:
	AUGraphStop (player.graph);
	AUGraphUninitialize (player.graph);
	AUGraphClose(player.graph);
	
	return 0;
}
