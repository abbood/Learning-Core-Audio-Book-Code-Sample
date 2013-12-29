//
//  CH11_MIDIWifiSourceViewController.m
//  CH11_MIDIWifiSource
//
//  Created by Chris Adamson on 9/10/11.
//  Copyright 2011 Subsequently and Furthermore, Inc. All rights reserved.
//

#import "CH11_MIDIWifiSourceViewController.h"
#import <CoreMIDI/CoreMIDI.h>

#define DESTINATION_ADDRESS @"192.168.2.108"

@interface CH11_MIDIWifiSourceViewController()
- (void) connectToHost;
- (void) sendStatus:(Byte)status data1:(Byte)data1 data2:(Byte)data2;
- (void) sendNoteOnEvent:(Byte) note velocity:(Byte)velocity;
- (void) sendNoteOffEvent:(Byte)key velocity:(Byte)velocity;
@property (assign) MIDINetworkSession *midiSession;
@property (assign) MIDIEndpointRef destinationEndpoint;
@property (assign) MIDIPortRef outputPort;
@end

@implementation CH11_MIDIWifiSourceViewController

@synthesize midiSession;
@synthesize destinationEndpoint;
@synthesize outputPort;


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


#pragma mark - View lifecycle

/*
// Implement viewDidLoad to do additional setup after loading the view, typically from a nib.
*/
- (void)viewDidLoad
{
    [super viewDidLoad];
	[self connectToHost];
}


- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)interfaceOrientation
{
    // Return YES for supported orientations
    return (interfaceOrientation == UIInterfaceOrientationPortrait);
}

#pragma midi stuff
-(void) connectToHost {
	MIDINetworkHost *host = [MIDINetworkHost hostWithName:@"MyMIDIWifi" address:DESTINATION_ADDRESS port:5004];
	if(!host)
		return;
	
	MIDINetworkConnection *connection = [MIDINetworkConnection connectionWithHost:host];
	if(!connection)
		return;
	
	self.midiSession = [MIDINetworkSession defaultSession];
	if (self.midiSession) {
		NSLog (@"Got MIDI session");
		[self.midiSession addConnection:connection];
		self.midiSession.enabled = YES;
		self.destinationEndpoint = [self.midiSession destinationEndpoint];
		
		MIDIClientRef client = NULL;
		MIDIPortRef outport = NULL;
		CheckError (MIDIClientCreate(CFSTR("MyMIDIWifi Client"), NULL, NULL, &client),
					"Couldn't create MIDI client");
		CheckError (MIDIOutputPortCreate(client, CFSTR("MyMIDIWifi Output port"), &outport),
					"Couldn't create output port");
		self.outputPort = outport;
		NSLog (@"Got output port");
	}
}

-(void) sendStatus:(Byte)status data1:(Byte)data1 data2:(Byte)data2 {
	MIDIPacketList packetList;
	
	packetList.numPackets = 1;
	packetList.packet[0].length = 3;
	packetList.packet[0].data[0] = status;
	packetList.packet[0].data[1] = data1;
	packetList.packet[0].data[2] = data2;
	packetList.packet[0].timeStamp = 0;
	
	CheckError (MIDISend(self.outputPort, self.destinationEndpoint, &packetList),
				"Couldn't send MIDI packet list");
}

-(void) sendNoteOnEvent:(Byte)key velocity:(Byte)velocity {
	[self sendStatus:0x90 data1:key & 0x7F data2:velocity & 0x7F];

}

-(void) sendNoteOffEvent:(Byte)key velocity:(Byte)velocity {
	[self sendStatus:0x80 data1:key & 0x7F data2:velocity & 0x7F];
}


#pragma mark event handlers
-(IBAction) handleKeyDown:(id)sender {
	NSInteger note = [sender tag];
	[self sendNoteOnEvent:(Byte) note velocity:127];
}

-(IBAction) handleKeyUp:(id)sender {
	NSInteger note = [sender tag];
	[self sendNoteOffEvent:(Byte) note velocity:127];
	
}



@end
