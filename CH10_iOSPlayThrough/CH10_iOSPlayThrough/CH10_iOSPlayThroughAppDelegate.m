//
//  CH10_iOSPlayThroughAppDelegate.m
//  CH10_iOSPlayThrough
//
//  Created by Chris Adamson on 7/10/11.
//  Copyright 2011 Subsequently and Furthermore, Inc. All rights reserved.
//

#import "CH10_iOSPlayThroughAppDelegate.h"

@implementation CH10_iOSPlayThroughAppDelegate

@synthesize window = _window;
@synthesize effectState = _effectState;

#pragma mark helpers

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


#pragma mark callbacks
static void MyInterruptionListener (void *inUserData,
							 UInt32 inInterruptionState) {
	
	printf ("Interrupted! inInterruptionState=%ld\n", inInterruptionState);
	CH10_iOSPlayThroughAppDelegate *appDelegate = (CH10_iOSPlayThroughAppDelegate*)inUserData;
	switch (inInterruptionState) {
		case kAudioSessionBeginInterruption:
			break;
		case kAudioSessionEndInterruption:
			// TODO: doesn't work!
			CheckError(AudioSessionSetActive(true), 
					   "Couldn't set audio session active");
			CheckError(AudioUnitInitialize(appDelegate.effectState.rioUnit),
					   "Couldn't initialize RIO unit");
			CheckError (AudioOutputUnitStart (appDelegate.effectState.rioUnit),
						"Couldn't start RIO unit");
			break;
		default:
			break;
	};
}

static OSStatus InputModulatingRenderCallback (
								   void *							inRefCon,
								   AudioUnitRenderActionFlags *	ioActionFlags,
								   const AudioTimeStamp *			inTimeStamp,
								   UInt32							inBusNumber,
								   UInt32							inNumberFrames,
								   AudioBufferList *				ioData) {
	EffectState *effectState = (EffectState*) inRefCon;

	// just copy samples
	UInt32 bus1 = 1;
	CheckError(AudioUnitRender(effectState->rioUnit,
								ioActionFlags,
								inTimeStamp,
								bus1,
								inNumberFrames,
								ioData),
			   "Couldn't render from RemoteIO unit");
	
	// walk the samples
	AudioSampleType sample = 0;
	UInt32 bytesPerChannel = effectState->asbd.mBytesPerFrame/effectState->asbd.mChannelsPerFrame;
	for (int bufCount=0; bufCount<ioData->mNumberBuffers; bufCount++) {
		AudioBuffer buf = ioData->mBuffers[bufCount];
		int currentFrame = 0;
		while ( currentFrame < inNumberFrames ) {
			// copy sample to buffer, across all channels
			for (int currentChannel=0; currentChannel<buf.mNumberChannels; currentChannel++) {
				memcpy(&sample,
					   buf.mData + (currentFrame * effectState->asbd.mBytesPerFrame) +
							(currentChannel * bytesPerChannel),
					   sizeof(AudioSampleType));
				
				float theta = effectState->sinePhase * M_PI * 2;
				
				sample = (sin(theta) * sample);

				memcpy(buf.mData + (currentFrame * effectState->asbd.mBytesPerFrame) +
							(currentChannel * bytesPerChannel),
					   &sample,
					   sizeof(AudioSampleType));
				
				effectState->sinePhase += 1.0 / (effectState->asbd.mSampleRate / effectState->sineFrequency);
				if (effectState->sinePhase > 1.0) {
					effectState->sinePhase -= 1.0;
				}
			}	
			currentFrame++;
		}
	}
	return noErr;
}





#pragma mark app lifecycle

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    // set up audio session
    CheckError(AudioSessionInitialize(NULL,
                                      kCFRunLoopDefaultMode,
                                      MyInterruptionListener,
                                      self),
               "couldn't initialize audio session");
    
	UInt32 category = kAudioSessionCategory_PlayAndRecord;
    CheckError(AudioSessionSetProperty(kAudioSessionProperty_AudioCategory,
                                       sizeof(category),
                                       &category),
               "Couldn't set category on audio session");
    
	// is audio input available?
	UInt32 ui32PropertySize = sizeof (UInt32);
	UInt32 inputAvailable;
	CheckError(AudioSessionGetProperty(kAudioSessionProperty_AudioInputAvailable,
							&ui32PropertySize,
							&inputAvailable),
			   "Couldn't get current audio input available prop");
	if (! inputAvailable) {
		UIAlertView *noInputAlert =
		[[UIAlertView alloc] initWithTitle:@"No audio input"
								   message:@"No audio input device is currently attached"
								  delegate:nil
						 cancelButtonTitle:@"OK"
						 otherButtonTitles:nil];
		[noInputAlert show];
		[noInputAlert release];
		// TODO: do we have to die? couldn't we tolerate an incoming connection
		// TODO: need another example to show audio routes?
		return YES;
	}

	// inspect the hardware input rate
	Float64 hardwareSampleRate;
	UInt32 propSize = sizeof (hardwareSampleRate);
	CheckError(AudioSessionGetProperty(kAudioSessionProperty_CurrentHardwareSampleRate,
									   &propSize,
									   &hardwareSampleRate),
			   "Couldn't get hardwareSampleRate");
	NSLog (@"hardwareSampleRate = %f", hardwareSampleRate);
	
//	CheckError(AudioSessionSetActive(true),
//			   "Couldn't set AudioSession active");
		
	// describe unit
	AudioComponentDescription audioCompDesc;
	audioCompDesc.componentType = kAudioUnitType_Output;
	audioCompDesc.componentSubType = kAudioUnitSubType_RemoteIO;
	audioCompDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
	audioCompDesc.componentFlags = 0;
	audioCompDesc.componentFlagsMask = 0;
	
	// get rio unit from audio component manager
	AudioComponent rioComponent = AudioComponentFindNext(NULL, &audioCompDesc);
	CheckError(AudioComponentInstanceNew(rioComponent, &_effectState.rioUnit),
			   "Couldn't get RIO unit instance");
	
	// set up the rio unit for playback
	UInt32 oneFlag = 1;
	AudioUnitElement bus0 = 0;
	CheckError(AudioUnitSetProperty (_effectState.rioUnit,
						  kAudioOutputUnitProperty_EnableIO,
						  kAudioUnitScope_Output,
						  bus0,
						  &oneFlag,
						  sizeof(oneFlag)),
			   "Couldn't enable RIO output");
	
	// enable rio input
	AudioUnitElement bus1 = 1;
	CheckError(AudioUnitSetProperty(_effectState.rioUnit,
									kAudioOutputUnitProperty_EnableIO,
									kAudioUnitScope_Input,
									bus1,
									&oneFlag,
									sizeof(oneFlag)),
			   "Couldn't enable RIO input");
		
	// setup an asbd in the iphone canonical format
	AudioStreamBasicDescription myASBD;
	memset (&myASBD, 0, sizeof (myASBD));
	myASBD.mSampleRate = hardwareSampleRate;
	myASBD.mFormatID = kAudioFormatLinearPCM;
	myASBD.mFormatFlags = kAudioFormatFlagsCanonical;
	myASBD.mBytesPerPacket = 4;
	myASBD.mFramesPerPacket = 1;
	myASBD.mBytesPerFrame = 4;
	myASBD.mChannelsPerFrame = 2;
	myASBD.mBitsPerChannel = 16;
	
	/*
	 // set format for output (bus 0) on rio's input scope
	 */
	CheckError(AudioUnitSetProperty (_effectState.rioUnit,
						  kAudioUnitProperty_StreamFormat,
						  kAudioUnitScope_Input,
						  bus0,
						  &myASBD,
						  sizeof (myASBD)),
			   "Couldn't set ASBD for RIO on input scope / bus 0");
	
	
	// set asbd for mic input
	CheckError(AudioUnitSetProperty (_effectState.rioUnit,
						  kAudioUnitProperty_StreamFormat,
						  kAudioUnitScope_Output,
						  bus1,
						  &myASBD,
						  sizeof (myASBD)),
			   "Couldn't set ASBD for RIO on output scope / bus 1");
	
	// more info on ring modulator and dalek voices at:
	// http://homepage.powerup.com.au/~spratleo/Tech/Dalek_Voice_Primer.html
	_effectState.asbd = myASBD;
	_effectState.sineFrequency = 30;
	_effectState.sinePhase = 0;
	
	// set callback method
	AURenderCallbackStruct callbackStruct;
	callbackStruct.inputProc = InputModulatingRenderCallback; // callback function
	callbackStruct.inputProcRefCon = &_effectState;
	
	CheckError(AudioUnitSetProperty(_effectState.rioUnit, 
						 kAudioUnitProperty_SetRenderCallback,
						 kAudioUnitScope_Global,
						 bus0,
						 &callbackStruct,
						 sizeof (callbackStruct)),
			   "Couldn't set RIO render callback on bus 0");
	

	// initialize and start remoteio unit
	CheckError(AudioUnitInitialize(_effectState.rioUnit),
			   "Couldn't initialize RIO unit");
	CheckError (AudioOutputUnitStart (_effectState.rioUnit),
				"Couldn't start RIO unit");
	
	
	printf("RIO started!\n");
	
	// Override point for customization after application launch.
    [self.window makeKeyAndVisible];
    return YES;
	
}



// CHRIS NOTE TO SELF: NO CHANGES TO XCODE TEMPLATE BELOW THIS POINT


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
