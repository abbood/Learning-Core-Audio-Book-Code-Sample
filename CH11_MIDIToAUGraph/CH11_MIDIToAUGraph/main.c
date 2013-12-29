//
//  main.c
//  CH11_MIDIToAUGraph
//
//  Created by Chris Adamson on 9/6/11.
//  Copyright 2011 Subsequently and Furthermore, Inc. All rights reserved.
//

#include <CoreFoundation/CoreFoundation.h>
#import <CoreMIDI/CoreMIDI.h>
#import <AudioToolbox/AudioToolbox.h>

#pragma mark - state struct
typedef struct MyMIDIPlayer {
	AUGraph		graph;
	AudioUnit	instrumentUnit;
} MyMIDIPlayer;

#pragma mark - forward declarations
void setupMIDI(MyMIDIPlayer *player);
void setupAUGraph(MyMIDIPlayer *player);
static void	MyMIDIReadProc(const MIDIPacketList *pktlist, void *refCon, void *connRefCon);
void MyMIDINotifyProc (const MIDINotification  *message, void *refCon);

#pragma mark utility functions
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

#pragma mark - callbacks
static void	MyMIDIReadProc(const MIDIPacketList *pktlist, void *refCon, void *connRefCon) {
	MyMIDIPlayer *player = (MyMIDIPlayer*) refCon;

	MIDIPacket *packet = (MIDIPacket *)pktlist->packet;	
	for (int i=0; i < pktlist->numPackets; i++) {
		Byte midiStatus = packet->data[0];
		Byte midiCommand = midiStatus >> 4;
		// is it a note-on or note-off
		if ((midiCommand == 0x09) ||
			(midiCommand == 0x08)) {
			Byte note = packet->data[1] & 0x7F;
			Byte velocity = packet->data[2] & 0x7F;
			printf("midiCommand=%d. Note=%d, Velocity=%d\n", midiCommand, note, velocity);
			
			// send to augraph
			CheckError(MusicDeviceMIDIEvent (player->instrumentUnit,
											 midiStatus,
											 note,
											 velocity,
											 0), 
					   "Couldn't send MIDI event");
			
		}
		packet = MIDIPacketNext(packet);
	}
}


void MyMIDINotifyProc (const MIDINotification  *message, void *refCon) {
	printf("MIDI Notify, messageId=%d,", message->messageID);
}



#pragma mark - augraph
void setupAUGraph(MyMIDIPlayer *player) {
	
	CheckError(NewAUGraph(&player->graph),
			   "Couldn't open AU Graph");

	// generate description that will match our output device (speakers)
	AudioComponentDescription outputcd = {0};
	outputcd.componentType = kAudioUnitType_Output;
	outputcd.componentSubType = kAudioUnitSubType_DefaultOutput;
	outputcd.componentManufacturer = kAudioUnitManufacturer_Apple;

	// adds a node with above description to the graph
	AUNode outputNode;
	CheckError(AUGraphAddNode(player->graph, &outputcd, &outputNode),
			   "AUGraphAddNode[kAudioUnitSubType_DefaultOutput] failed");

	
	AudioComponentDescription instrumentcd = {0};
	instrumentcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	instrumentcd.componentType = kAudioUnitType_MusicDevice;
	instrumentcd.componentSubType = kAudioUnitSubType_DLSSynth;

	AUNode instrumentNode;
	CheckError(AUGraphAddNode(player->graph, &instrumentcd, &instrumentNode),
			   "AUGraphAddNode[kAudioUnitSubType_DLSSynth] failed");
	
	// opening the graph opens all contained audio units but does not allocate any resources yet
	CheckError(AUGraphOpen(player->graph),
			   "AUGraphOpen failed");
	
	// get the reference to the AudioUnit object for the instrument graph node
	CheckError(AUGraphNodeInfo(player->graph, instrumentNode, NULL, &player->instrumentUnit),
			   "AUGraphNodeInfo failed");

	// connect the output source of the instrument AU to the input source of the output node
	CheckError(AUGraphConnectNodeInput(player->graph, instrumentNode, 0, outputNode, 0),
			   "AUGraphConnectNodeInput");

	// now initialize the graph (causes resources to be allocated)
	CheckError(AUGraphInitialize(player->graph),
			   "AUGraphInitialize failed");

	
}

#pragma mark - midi
void setupMIDI(MyMIDIPlayer *player) {
	
	MIDIClientRef client;
	CheckError (MIDIClientCreate(CFSTR("Core MIDI to System Sounds Demo"), MyMIDINotifyProc, player, &client),
				"Couldn't create MIDI client");
	
	MIDIPortRef inPort;
	CheckError (MIDIInputPortCreate(client, CFSTR("Input port"), MyMIDIReadProc, player, &inPort),
				"Couldn't create MIDI input port");
	
	unsigned long sourceCount = MIDIGetNumberOfSources();
	printf ("%ld sources\n", sourceCount);
	for (int i = 0; i < sourceCount; ++i) {
		MIDIEndpointRef src = MIDIGetSource(i);
		CFStringRef endpointName = NULL;
		CheckError(MIDIObjectGetStringProperty(src, kMIDIPropertyName, &endpointName),
				   "Couldn't get endpoint name");
		char endpointNameC[255];
		CFStringGetCString(endpointName, endpointNameC, 255, kCFStringEncodingUTF8);
		printf("  source %d: %s\n", i, endpointNameC);
		CheckError (MIDIPortConnectSource(inPort, src, NULL),
					"Couldn't connect MIDI port");
	}
	

}

#pragma mark - main
int main (int argc, const char * argv[])
{
	
	MyMIDIPlayer player;
	
	setupAUGraph(&player);
	setupMIDI(&player);
    
	CheckError (AUGraphStart(player.graph),
				"couldn't start graph");
	
	CFRunLoopRun();
	// run until aborted with control-C
	
	return 0;
}


