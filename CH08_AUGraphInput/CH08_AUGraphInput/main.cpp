#include <AudioToolbox/AudioToolbox.h>
#include <ApplicationServices/ApplicationServices.h>
#include "CARingBuffer.h"
#include <pthread.h>

//#define PART_II

typedef struct MyAUGraphPlayer
{
	AudioStreamBasicDescription streamFormat; 
	
	AUGraph graph;
	AudioUnit inputUnit;
	AudioUnit outputUnit;
#ifdef PART_II
	AudioUnit speechUnit;
#endif
	
	AudioBufferList *inputBuffer;
	CARingBuffer *ringBuffer;
	
	Float64 firstInputSampleTime;
	Float64 firstOutputSampleTime;
	Float64 inToOutSampleTimeOffset;
	
} MyAUGraphPlayer;

OSStatus InputRenderProc(void *inRefCon,
						 AudioUnitRenderActionFlags *ioActionFlags,
						 const AudioTimeStamp *inTimeStamp,
						 UInt32 inBusNumber,
						 UInt32 inNumberFrames,
						 AudioBufferList * ioData);
OSStatus GraphRenderProc(void *inRefCon,
						 AudioUnitRenderActionFlags *ioActionFlags,
						 const AudioTimeStamp *inTimeStamp,
						 UInt32 inBusNumber,
						 UInt32 inNumberFrames,
						 AudioBufferList * ioData);
void CreateInputUnit (MyAUGraphPlayer *player);
void CreateMyAUGraph(MyAUGraphPlayer *player);

#pragma mark - render proc - 
OSStatus InputRenderProc(void *inRefCon,
						 AudioUnitRenderActionFlags *ioActionFlags,
						 const AudioTimeStamp *inTimeStamp,
						 UInt32 inBusNumber,
						 UInt32 inNumberFrames,
						 AudioBufferList * ioData)
{
	
	//	printf ("InputRenderProc!\n");
	MyAUGraphPlayer *player = (MyAUGraphPlayer*) inRefCon;
	
	// have we ever logged input timing? (for offset calculation)
	if (player->firstInputSampleTime < 0.0) {
		player->firstInputSampleTime = inTimeStamp->mSampleTime;
		if ((player->firstOutputSampleTime > 0.0) &&
			(player->inToOutSampleTimeOffset < 0.0)) {
			player->inToOutSampleTimeOffset = player->firstInputSampleTime - player->firstOutputSampleTime;
		}
	}
	
	// render into our buffer
	OSStatus inputProcErr = noErr;
	inputProcErr = AudioUnitRender(player->inputUnit,
								   ioActionFlags,
								   inTimeStamp,
								   inBusNumber,
								   inNumberFrames,
								   player->inputBuffer);
	// copy from our buffer to ring buffer
	if (! inputProcErr) {
		inputProcErr = player->ringBuffer->Store(player->inputBuffer,
												 inNumberFrames,
												 inTimeStamp->mSampleTime);
		
		//		printf ("stored %d frames at time %f\n", inNumberFrames, inTimeStamp->mSampleTime);
	}
	//	else {
	//		printf ("input renderErr: %d\n", inputProcErr);
	//	}
	//		
	
	return inputProcErr;
}	


OSStatus GraphRenderProc(void *inRefCon,
						 AudioUnitRenderActionFlags *ioActionFlags,
						 const AudioTimeStamp *inTimeStamp,
						 UInt32 inBusNumber,
						 UInt32 inNumberFrames,
						 AudioBufferList * ioData)
{
	
	//	printf ("GraphRenderProc! need %d frames for time %f \n", inNumberFrames, inTimeStamp->mSampleTime);
	
	MyAUGraphPlayer *player = (MyAUGraphPlayer*) inRefCon;
	
	// have we ever logged output timing? (for offset calculation)
	if (player->firstOutputSampleTime < 0.0) {
		player->firstOutputSampleTime = inTimeStamp->mSampleTime;
		if ((player->firstInputSampleTime > 0.0) &&
			(player->inToOutSampleTimeOffset < 0.0)) {
			player->inToOutSampleTimeOffset = player->firstInputSampleTime - player->firstOutputSampleTime;
		}
	}
	
	// copy samples out of ring buffer
	OSStatus outputProcErr = noErr;
	// new CARingBuffer doesn't take bool 4th arg
	outputProcErr = player->ringBuffer->Fetch(ioData,
											  inNumberFrames,
											  inTimeStamp->mSampleTime + player->inToOutSampleTimeOffset);
	
	//	printf ("fetched %d frames at time %f\n", inNumberFrames, inTimeStamp->mSampleTime);
	return outputProcErr;
	
}




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

void CreateInputUnit (MyAUGraphPlayer *player) {
	
	// generate description that will match audio HAL
	AudioComponentDescription inputcd = {0};
	inputcd.componentType = kAudioUnitType_Output;
	inputcd.componentSubType = kAudioUnitSubType_HALOutput;
	inputcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	
	AudioComponent comp = AudioComponentFindNext(NULL, &inputcd);
	if (comp == NULL) {
		printf ("can't get output unit");
		exit (-1);
	}
	
	CheckError(AudioComponentInstanceNew(comp, &player->inputUnit), 
			   "Couldn't open component for inputUnit");
	
	// enable/io
	UInt32 disableFlag = 0;
	UInt32 enableFlag = 1;
	AudioUnitScope outputBus = 0;
	AudioUnitScope inputBus = 1;
	CheckError (AudioUnitSetProperty(player->inputUnit,
									 kAudioOutputUnitProperty_EnableIO,
									 kAudioUnitScope_Input,
									 inputBus,
									 &enableFlag,
									 sizeof(enableFlag)),
				"Couldn't enable input on I/O unit");
	
	CheckError (AudioUnitSetProperty(player->inputUnit,
									 kAudioOutputUnitProperty_EnableIO,
									 kAudioUnitScope_Output,
									 outputBus,
									 &disableFlag,	// well crap, have to disable
									 sizeof(enableFlag)),
				"Couldn't disable output on I/O unit");
	
	// set device (osx only... iphone has only one device)
	AudioDeviceID defaultDevice = kAudioObjectUnknown;
	UInt32 propertySize = sizeof (defaultDevice);
	
	// AudioHardwareGetProperty() is deprecated	
	//	CheckError (AudioHardwareGetProperty(kAudioHardwarePropertyDefaultInputDevice,
	//										 &propertySize,
	//										 &defaultDevice),
	//				"Couldn't get default input device");
	
	// AudioObjectProperty stuff new in 10.6, replaces AudioHardwareGetProperty() call
	// TODO: need to update ch08 to explain, use this call. need CoreAudio.framework
	AudioObjectPropertyAddress defaultDeviceProperty;
	defaultDeviceProperty.mSelector = kAudioHardwarePropertyDefaultInputDevice;
	defaultDeviceProperty.mScope = kAudioObjectPropertyScopeGlobal;
	defaultDeviceProperty.mElement = kAudioObjectPropertyElementMaster;
	
	CheckError (AudioObjectGetPropertyData(kAudioObjectSystemObject,
										   &defaultDeviceProperty,
										   0,
										   NULL,
										   &propertySize,
										   &defaultDevice),
				"Couldn't get default input device");
	
	// set this defaultDevice as the input's property
	// kAudioUnitErr_InvalidPropertyValue if output is enabled on inputUnit
	CheckError(AudioUnitSetProperty(player->inputUnit,
									kAudioOutputUnitProperty_CurrentDevice,
									kAudioUnitScope_Global,
									outputBus,
									&defaultDevice,
									sizeof(defaultDevice)),
			   "Couldn't set default device on I/O unit");
	
	// use the stream format coming out of the AUHAL (should be de-interleaved)
	propertySize = sizeof (AudioStreamBasicDescription);
	CheckError(AudioUnitGetProperty(player->inputUnit,
									kAudioUnitProperty_StreamFormat,
									kAudioUnitScope_Output,
									inputBus,
									&player->streamFormat,
									&propertySize),
			   "Couldn't get ASBD from input unit");
	
	// 9/6/10 - check the input device's stream format
	AudioStreamBasicDescription deviceFormat;
	CheckError(AudioUnitGetProperty(player->inputUnit,
									kAudioUnitProperty_StreamFormat,
									kAudioUnitScope_Input,
									inputBus,
									&deviceFormat,
									&propertySize),
			   "Couldn't get ASBD from input unit");
	
	printf ("Device rate %f, graph rate %f\n",
			deviceFormat.mSampleRate,
			player->streamFormat.mSampleRate);
	player->streamFormat.mSampleRate = deviceFormat.mSampleRate;
	
	propertySize = sizeof (AudioStreamBasicDescription);
	CheckError(AudioUnitSetProperty(player->inputUnit,
									kAudioUnitProperty_StreamFormat,
									kAudioUnitScope_Output,
									inputBus,
									&player->streamFormat,
									propertySize),
			   "Couldn't set ASBD on input unit");
	
	/* allocate some buffers to hold samples between input and output callbacks
	 (this part largely copied from CAPlayThrough) */
	//Get the size of the IO buffer(s)
	UInt32 bufferSizeFrames = 0;
	propertySize = sizeof(UInt32);
	CheckError (AudioUnitGetProperty(player->inputUnit,
									 kAudioDevicePropertyBufferFrameSize,
									 kAudioUnitScope_Global,
									 0,
									 &bufferSizeFrames,
									 &propertySize),
				"Couldn't get buffer frame size from input unit");
	UInt32 bufferSizeBytes = bufferSizeFrames * sizeof(Float32);
	
	if (player->streamFormat.mFormatFlags & kAudioFormatFlagIsNonInterleaved) {
		printf ("format is non-interleaved\n");
		// allocate an AudioBufferList plus enough space for array of AudioBuffers
		UInt32 propsize = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) * player->streamFormat.mChannelsPerFrame);
		
		//malloc buffer lists
		player->inputBuffer = (AudioBufferList *)malloc(propsize);
		player->inputBuffer->mNumberBuffers = player->streamFormat.mChannelsPerFrame;
		
		//pre-malloc buffers for AudioBufferLists
		for(UInt32 i =0; i< player->inputBuffer->mNumberBuffers ; i++) {
			player->inputBuffer->mBuffers[i].mNumberChannels = 1;
			player->inputBuffer->mBuffers[i].mDataByteSize = bufferSizeBytes;
			player->inputBuffer->mBuffers[i].mData = malloc(bufferSizeBytes);
		}
	} else {
		printf ("format is interleaved\n");
		// allocate an AudioBufferList plus enough space for array of AudioBuffers
		UInt32 propsize = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) * 1);
		
		//malloc buffer lists
		player->inputBuffer = (AudioBufferList *)malloc(propsize);
		player->inputBuffer->mNumberBuffers = 1;
		
		//pre-malloc buffers for AudioBufferLists
		player->inputBuffer->mBuffers[0].mNumberChannels = player->streamFormat.mChannelsPerFrame;
		player->inputBuffer->mBuffers[0].mDataByteSize = bufferSizeBytes;
		player->inputBuffer->mBuffers[0].mData = malloc(bufferSizeBytes);
	}
	
	//Alloc ring buffer that will hold data between the two audio devices
	player->ringBuffer = new CARingBuffer();	
	player->ringBuffer->Allocate(player->streamFormat.mChannelsPerFrame,
								 player->streamFormat.mBytesPerFrame,
								 bufferSizeFrames * 3);
	
	// set render proc to supply samples from input unit
	AURenderCallbackStruct callbackStruct;
	callbackStruct.inputProc = InputRenderProc; 
	callbackStruct.inputProcRefCon = player;
	
	CheckError(AudioUnitSetProperty(player->inputUnit, 
									kAudioOutputUnitProperty_SetInputCallback, 
									kAudioUnitScope_Global,
									0,
									&callbackStruct, 
									sizeof(callbackStruct)),
			   "Couldn't set input callback");
	
	CheckError(AudioUnitInitialize(player->inputUnit),
			   "Couldn't initialize input unit");
	
	player->firstInputSampleTime = -1;
	player->inToOutSampleTimeOffset = -1;
	
	printf ("Bottom of CreateInputUnit()\n");
}


void CreateMyAUGraph(MyAUGraphPlayer *player)
{
	
	// create a new AUGraph
	CheckError(NewAUGraph(&player->graph),
			   "NewAUGraph failed");
	
	// generate description that will match default output
	//	ComponentDescription outputcd = {0};
	//	outputcd.componentType = kAudioUnitType_Output;
	//	outputcd.componentSubType = kAudioUnitSubType_DefaultOutput;
	//	outputcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	//	
	//	Component comp = FindNextComponent(NULL, &outputcd);
	//	if (comp == NULL) {
	//		printf ("can't get output unit"); exit (-1);
	//	}
	
	AudioComponentDescription outputcd = {0};
	outputcd.componentType = kAudioUnitType_Output;
	outputcd.componentSubType = kAudioUnitSubType_DefaultOutput;
	outputcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	
	AudioComponent comp = AudioComponentFindNext(NULL, &outputcd);
	if (comp == NULL) {
		printf ("can't get output unit"); exit (-1);
	}
	
	
	// adds a node with above description to the graph
	AUNode outputNode;
	CheckError(AUGraphAddNode(player->graph, &outputcd, &outputNode),
			   "AUGraphAddNode[kAudioUnitSubType_DefaultOutput] failed");
	
#ifdef PART_II
	
	// add a mixer to the graph,
	AudioComponentDescription mixercd = {0};
	mixercd.componentType = kAudioUnitType_Mixer;
	mixercd.componentSubType = kAudioUnitSubType_StereoMixer; // doesn't work: kAudioUnitSubType_MatrixMixer
	mixercd.componentManufacturer = kAudioUnitManufacturer_Apple;
	AUNode mixerNode;
	CheckError(AUGraphAddNode(player->graph, &mixercd, &mixerNode),
			   "AUGraphAddNode[kAudioUnitSubType_StereoMixer] failed");
	
	// adds a node with above description to the graph
	AudioComponentDescription speechcd = {0};
	speechcd.componentType = kAudioUnitType_Generator;
	speechcd.componentSubType = kAudioUnitSubType_SpeechSynthesis;
	speechcd.componentManufacturer = kAudioUnitManufacturer_Apple;
	AUNode speechNode;
	CheckError(AUGraphAddNode(player->graph, &speechcd, &speechNode),
			   "AUGraphAddNode[kAudioUnitSubType_AudioFilePlayer] failed");
	
	// opening the graph opens all contained audio units but does not allocate any resources yet
	CheckError(AUGraphOpen(player->graph),
			   "AUGraphOpen failed");
	
	// get the reference to the AudioUnit objects for the various nodes
	CheckError(AUGraphNodeInfo(player->graph, outputNode, NULL, &player->outputUnit),
			   "AUGraphNodeInfo failed");
	CheckError(AUGraphNodeInfo(player->graph, speechNode, NULL, &player->speechUnit),
			   "AUGraphNodeInfo failed");
	AudioUnit mixerUnit;
	CheckError(AUGraphNodeInfo(player->graph, mixerNode, NULL, &mixerUnit),
			   "AUGraphNodeInfo failed");
	
	// set ASBDs here
	UInt32 propertySize = sizeof (AudioStreamBasicDescription);
	CheckError(AudioUnitSetProperty(player->outputUnit,
									kAudioUnitProperty_StreamFormat,
									kAudioUnitScope_Input,
									0,
									&player->streamFormat,
									propertySize),
			   "Couldn't set stream format on output unit");
	
	// problem: badComponentInstance (-2147450879)
	CheckError(AudioUnitSetProperty(mixerUnit,
									kAudioUnitProperty_StreamFormat,
									kAudioUnitScope_Input,
									0,
									&player->streamFormat,
									propertySize),
			   "Couldn't set stream format on mixer unit bus 0");
	CheckError(AudioUnitSetProperty(mixerUnit,
									kAudioUnitProperty_StreamFormat,
									kAudioUnitScope_Input,
									1,
									&player->streamFormat,
									propertySize),
			   "Couldn't set stream format on mixer unit bus 1");
	
	
	// connections
	// mixer output scope / bus 0 to outputUnit input scope / bus 0
	// mixer input scope / bus 0 to render callback (from ringbuffer, which in turn is from inputUnit)
	// mixer input scope / bus 1 to speech unit output scope / bus 0
	
	CheckError(AUGraphConnectNodeInput(player->graph, mixerNode, 0, outputNode, 0),
			   "Couldn't connect mixer output(0) to outputNode (0)");
	CheckError(AUGraphConnectNodeInput(player->graph, speechNode, 0, mixerNode, 1),
			   "Couldn't connect speech synth unit output (0) to mixer input (1)");
	AURenderCallbackStruct callbackStruct;
	callbackStruct.inputProc = GraphRenderProc; 
	callbackStruct.inputProcRefCon = player;
	CheckError(AudioUnitSetProperty(mixerUnit,
									kAudioUnitProperty_SetRenderCallback,
									kAudioUnitScope_Global,
									0,
									&callbackStruct,
									sizeof(callbackStruct)),
			   "Couldn't set render callback on mixer unit");
	
	
#else	
	
	// opening the graph opens all contained audio units but does not allocate any resources yet
	CheckError(AUGraphOpen(player->graph),
			   "AUGraphOpen failed");
	
	// get the reference to the AudioUnit object for the output graph node
	CheckError(AUGraphNodeInfo(player->graph, outputNode, NULL, &player->outputUnit),
			   "AUGraphNodeInfo failed");
	
	// set the stream format on the output unit's input scope
	UInt32 propertySize = sizeof (AudioStreamBasicDescription);
	CheckError(AudioUnitSetProperty(player->outputUnit,
									kAudioUnitProperty_StreamFormat,
									kAudioUnitScope_Input,
									0,
									&player->streamFormat,
									propertySize),
			   "Couldn't set stream format on output unit");
	
	AURenderCallbackStruct callbackStruct;
	callbackStruct.inputProc = GraphRenderProc; 
	callbackStruct.inputProcRefCon = player;
	
	CheckError(AudioUnitSetProperty(player->outputUnit,
									kAudioUnitProperty_SetRenderCallback,
									kAudioUnitScope_Global,
									0,
									&callbackStruct,
									sizeof(callbackStruct)),
			   "Couldn't set render callback on output unit");
	
#endif
	
	
	// now initialize the graph (causes resources to be allocated)
	CheckError(AUGraphInitialize(player->graph),
			   "AUGraphInitialize failed");
	
	player->firstOutputSampleTime = -1;
	
	printf ("Bottom of CreateSimpleAUGraph()\n");
}

#ifdef PART_II
void PrepareSpeechAU(MyAUGraphPlayer *player)
{
	SpeechChannel chan;
	
	UInt32 propsize = sizeof(SpeechChannel);
	CheckError(AudioUnitGetProperty(player->speechUnit, kAudioUnitProperty_SpeechChannel,
									kAudioUnitScope_Global, 0, &chan, &propsize), "AudioFileGetProperty[kAudioUnitProperty_SpeechChannel] failed");
	
    SpeakCFString(chan,
                  CFSTR("Please purchase as many copies of our\
						Core Audio book as you possibly can"),
                  NULL);
}
#endif


int main (int argc, const char * argv[]) {
	
 	MyAUGraphPlayer player = {0};
	
	// create the input unit
	CreateInputUnit(&player);
	
	// build a graph with output unit
	CreateMyAUGraph(&player);
	
#ifdef PART_II
	// configure the speech synthesizer
	PrepareSpeechAU(&player);
	
#endif
	
	// start playing
	CheckError (AudioOutputUnitStart(player.inputUnit), "AudioOutputUnitStart failed");
	CheckError(AUGraphStart(player.graph), "AUGraphStart failed");
	
	// and wait
	printf("Capturing, press <return> to stop:\n");
	getchar();
	
cleanup:
	AUGraphStop (player.graph);
	AUGraphUninitialize (player.graph);
	AUGraphClose(player.graph);
	
	
}
