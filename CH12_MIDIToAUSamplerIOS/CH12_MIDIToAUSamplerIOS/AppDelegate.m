//
//  AppDelegate.m
//  CH12_MIDIToAUSamplerIOS
//
//  Created by Chris Adamson on 1/2/12.
//  Copyright (c) 2012 Subsequently and Furthermore, Inc. All rights reserved.
//

#import "AppDelegate.h"
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


@interface AppDelegate() 
@property 	MyMIDIPlayer player;
@end

@implementation AppDelegate

@synthesize window = _window;
@synthesize player = _player;

- (void)dealloc
{
	[_window release];
    [super dealloc];
}


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
	NSLog (@"MyMIDIReadProc\n");
	
	MIDIPacket *packet = (MIDIPacket *)pktlist->packet;	
	for (int i=0; i < pktlist->numPackets; i++) {
		NSLog (@"i=%d", i);
		Byte midiStatus = packet->data[0];
		Byte midiCommand = midiStatus >> 4;
		// is it a note-on or note-off
		if ((midiCommand == 0x09) ||
			(midiCommand == 0x08)) {
			Byte note = packet->data[1] & 0x7F;
			Byte velocity = packet->data[2] & 0x7F;
			NSLog(@"midiCommand=%d. Note=%d, Velocity=%d\n", midiCommand, note, velocity);
			
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
	printf("MIDI Notify, messageId=%ld,", message->messageID);
}



#pragma mark - augraph
void setupAUGraph(MyMIDIPlayer *player) {
	
	CheckError(NewAUGraph(&player->graph),
			   "Couldn't open AU Graph");
	
	// generate description that will match our output device (speakers)
	AudioComponentDescription outputcd = {0};
	outputcd.componentType = kAudioUnitType_Output;
	outputcd.componentSubType = kAudioUnitSubType_RemoteIO;
	outputcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	
	// adds a node with above description to the graph
	AUNode outputNode;
	CheckError(AUGraphAddNode(player->graph, &outputcd, &outputNode),
			   "AUGraphAddNode[kAudioUnitSubType_DefaultOutput] failed");
	
	
	AudioComponentDescription instrumentcd = {0};
	instrumentcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	instrumentcd.componentType = kAudioUnitType_MusicDevice;
	instrumentcd.componentSubType = kAudioUnitSubType_Sampler;  // changed!
	
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
	
	
	// configure the AUSampler

	// TODO: path to .aupreset file
	NSString *presetPath = [[NSBundle mainBundle] pathForResource:@"ch12-aupreset"
														   ofType:@"aupreset"];
	const char* presetPathC = [presetPath cStringUsingEncoding:NSUTF8StringEncoding];
	NSLog (@"presetPathC: %s", presetPathC);
	
	CFURLRef presetURL = CFURLCreateFromFileSystemRepresentation(
																 kCFAllocatorDefault,
																 presetPathC,
																 [presetPath length],
																 false);
	
	// load preset file into a CFDataRef
	CFDataRef presetData = NULL;
	SInt32 errorCode = noErr;
	Boolean gotPresetData =
	CFURLCreateDataAndPropertiesFromResource(kCFAllocatorSystemDefault,
											 presetURL,
											 &presetData,
											 NULL,
											 NULL,
											 &errorCode);
	CheckError(errorCode, "couldn't load .aupreset data");
	CheckError(!gotPresetData, "couldn't load .aupreset data");
	
	// convert this into a property list
	CFPropertyListFormat presetPlistFormat = {0};
	CFErrorRef presetPlistError = NULL;
	CFPropertyListRef presetPlist = CFPropertyListCreateWithData(kCFAllocatorSystemDefault,
																 presetData,
																 kCFPropertyListImmutable,
																 &presetPlistFormat, 
																 &presetPlistError);
	if (presetPlistError) {
		printf ("Couldn't create plist object for .aupreset");
		return;
	}
	
	// set this plist as the kAudioUnitProperty_ClassInfo on _auSampler
	if (presetPlist) {
		CheckError(AudioUnitSetProperty(player->instrumentUnit,
										kAudioUnitProperty_ClassInfo,
										kAudioUnitScope_Global,
										0,
										&presetPlist, 
										sizeof(presetPlist)),
				   "Couldn't set aupreset plist as sampler's class info");
	}
	
	NSLog (@"AUGraph ready");
	CAShow(player->graph);

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
	
	NSLog (@"MIDI ready");

}


- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    self.window = [[[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]] autorelease];
	
	setupAUGraph(&_player);
	setupMIDI(&_player);
    
	CheckError (AUGraphStart(_player.graph),
				"couldn't start graph");

	
	self.window.backgroundColor = [UIColor whiteColor];
    [self.window makeKeyAndVisible];
	NSLog(@"MIDI app running\n");
    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application
{
	/*
	 Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
	 Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
	 */
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
	/*
	 Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later. 
	 If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
	 */
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
	/*
	 Called as part of the transition from the background to the inactive state; here you can undo many of the changes made on entering the background.
	 */
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
	/*
	 Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
	 */
}

- (void)applicationWillTerminate:(UIApplication *)application
{
	/*
	 Called when the application is about to terminate.
	 Save data if appropriate.
	 See also applicationDidEnterBackground:.
	 */
}

@end
