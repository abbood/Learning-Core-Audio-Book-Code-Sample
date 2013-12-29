#include <AudioToolbox/AudioToolbox.h>

#define kNumberRecordBuffers	3


typedef struct MyRecorder {
	AudioFileID					recordFile; // reference to your output file
	SInt64						recordPacket; // current packet index in output file
	Boolean						running; // recording state
} MyRecorder;


OSStatus MyGetDefaultInputDeviceSampleRate(Float64 *outSampleRate);


#pragma mark - utility functions -

// generic error handler - if error is nonzero, prints error message and exits program.
    static void CheckError(OSStatus error, const char *operation)
    {
        if (error == noErr) return;
        
        char errorString[20];
        // see if it appears to be a 4-char-code
        *(UInt32 *)(errorString + 1) = CFSwapInt32HostToBig(error);
        if (isprint(errorString[1]) && isprint(errorString[2]) && isprint(errorString[3]) && isprint(errorString[4])) {
            errorString[0] = errorString[5] = '\'';
            errorString[6] = '\0';
        } else
            // no, format it as an integer
            sprintf(errorString, "%d", (int)error);
        
        fprintf(stderr, "Error: %s (%s)\n", operation, errorString);
        
        exit(1);
    }

// get sample rate of the default input device
OSStatus MyGetDefaultInputDeviceSampleRate(Float64 *outSampleRate)
{
	OSStatus error;
	AudioDeviceID deviceID = 0;
	
	// get the default input device
	AudioObjectPropertyAddress propertyAddress;
	UInt32 propertySize;
	propertyAddress.mSelector = kAudioHardwarePropertyDefaultInputDevice;
	propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
	propertyAddress.mElement = 0;
	propertySize = sizeof(AudioDeviceID);
	error = AudioHardwareServiceGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &propertySize, &deviceID);
	if (error) return error;
	
	// get its sample rate
	propertyAddress.mSelector = kAudioDevicePropertyNominalSampleRate;
	propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
	propertyAddress.mElement = 0;
	propertySize = sizeof(Float64);
	error = AudioHardwareServiceGetPropertyData(deviceID, &propertyAddress, 0, NULL, &propertySize, outSampleRate);
	
	return error;
}


// Determine the size, in bytes, of a buffer necessary to represent the supplied number
// of seconds of audio data.
static int MyComputeRecordBufferSize(const AudioStreamBasicDescription *format, AudioQueueRef queue, float seconds)
{
	int packets, frames, bytes;
	
	frames = (int)ceil(seconds * format->mSampleRate);
	
	if (format->mBytesPerFrame > 0)						// 1
		bytes = frames * format->mBytesPerFrame;
	else
	{
		UInt32 maxPacketSize;
		if (format->mBytesPerPacket > 0)				// 2
			maxPacketSize = format->mBytesPerPacket;
		else
		{
			// get the largest single packet size possible
			UInt32 propertySize = sizeof(maxPacketSize);	// 3
			CheckError(AudioQueueGetProperty(queue, kAudioConverterPropertyMaximumOutputPacketSize, &maxPacketSize,
											 &propertySize), "couldn't get queue's maximum output packet size");
		}
		if (format->mFramesPerPacket > 0)
			packets = frames / format->mFramesPerPacket;	 // 4
		else
			// worst-case scenario: 1 frame in a packet
			packets = frames;							// 5
		
		if (packets == 0)		// sanity check
			packets = 1;
		bytes = packets * maxPacketSize;				// 6
	}
	return bytes;
}

// Copy a queue's encoder's magic cookie to an audio file.
static void MyCopyEncoderCookieToFile(AudioQueueRef queue, AudioFileID theFile)
{
	UInt32 propertySize;
	
	// get the magic cookie, if any, from the queue's converter		
	OSStatus result = AudioQueueGetPropertySize(queue,
												kAudioConverterCompressionMagicCookie, &propertySize);
	
	if (result == noErr && propertySize > 0)
	{
		// there is valid cookie data to be fetched;  get it
		Byte *magicCookie = (Byte *)malloc(propertySize);
		CheckError(AudioQueueGetProperty(queue, kAudioQueueProperty_MagicCookie, magicCookie,
										 &propertySize), "get audio queue's magic cookie");
		
		// now set the magic cookie on the output file
		CheckError(AudioFileSetProperty(theFile, kAudioFilePropertyMagicCookieData, propertySize, magicCookie),
				   "set audio file's magic cookie");
		free(magicCookie);
	}
}

#pragma mark - audio queue -

// Audio Queue callback function, called when an input buffer has been filled.
static void MyAQInputCallback(void *inUserData, AudioQueueRef inQueue,
							  AudioQueueBufferRef inBuffer,
							  const AudioTimeStamp *inStartTime,
							  UInt32 inNumPackets,
							  const AudioStreamPacketDescription *inPacketDesc)
{
	MyRecorder *recorder = (MyRecorder *)inUserData;
	
	// if inNumPackets is greater then zero, our buffer contains audio data
	// in the format we specified (AAC)
	if (inNumPackets > 0)
	{
		// write packets to file
		CheckError(AudioFileWritePackets(recorder->recordFile, FALSE, inBuffer->mAudioDataByteSize,
										 inPacketDesc, recorder->recordPacket, &inNumPackets, 
										 inBuffer->mAudioData), "AudioFileWritePackets failed");
		// increment packet index
		recorder->recordPacket += inNumPackets;
	}
	
	// if we're not stopping, re-enqueue the buffer so that it gets filled again
	if (recorder->running)
		CheckError(AudioQueueEnqueueBuffer(inQueue, inBuffer,
										   0, NULL), "AudioQueueEnqueueBuffer failed");
}

int	main(int argc, const char *argv[])
{
	MyRecorder recorder = {0};
	AudioStreamBasicDescription recordFormat = {0};
	memset(&recordFormat, 0, sizeof(recordFormat));
	
	// Configure the output data format to be AAC
	recordFormat.mFormatID = kAudioFormatMPEG4AAC;
	recordFormat.mChannelsPerFrame = 2;
	
	// get the sample rate of the default input device
	// we use this to adapt the output data format to match hardware capabilities
	MyGetDefaultInputDeviceSampleRate(&recordFormat.mSampleRate);
	
	// ProTip: Use the AudioFormat API to trivialize ASBD creation.
	//         input: atleast the mFormatID, however, at this point we already have
	//                mSampleRate, mFormatID, and mChannelsPerFrame
	//         output: the remainder of the ASBD will be filled out as much as possible
	//                 given the information known about the format
	UInt32 propSize = sizeof(recordFormat);
	CheckError(AudioFormatGetProperty(kAudioFormatProperty_FormatInfo, 0, NULL,
									  &propSize, &recordFormat), "AudioFormatGetProperty failed");
	
	// create a input (recording) queue
	AudioQueueRef queue = {0};
	CheckError(AudioQueueNewInput(&recordFormat, // ASBD
								  MyAQInputCallback, // Callback
								  &recorder, // user data
								  NULL, // run loop
								  NULL, // run loop mode
								  0, // flags (always 0)
								  // &recorder.queue), // output: reference to AudioQueue object
								  &queue),
			   "AudioQueueNewInput failed");
	
	// since the queue is now initilized, we ask it's Audio Converter object
	// for the ASBD it has configured itself with. The file may require a more
	// specific stream description than was necessary to create the audio queue.
	//
	// for example: certain fields in an ASBD cannot possibly be known until it's
	// codec is instantiated (in this case, by the AudioQueue's Audio Converter object)
	UInt32 size = sizeof(recordFormat);
	CheckError(AudioQueueGetProperty(queue, kAudioConverterCurrentOutputStreamDescription,
									 &recordFormat, &size), "couldn't get queue's format");
	
	// create the audio file
	CFURLRef myFileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, CFSTR("./output.caf"), kCFURLPOSIXPathStyle, false);
	CFShow (myFileURL);
	CheckError(AudioFileCreateWithURL(myFileURL, kAudioFileCAFType, &recordFormat,
									  kAudioFileFlags_EraseFile, &recorder.recordFile), "AudioFileCreateWithURL failed");
	CFRelease(myFileURL);
	
	// many encoded formats require a 'magic cookie'. we set the cookie first
	// to give the file object as much info as we can about the data it will be receiving
	MyCopyEncoderCookieToFile(queue, recorder.recordFile);
	
	// allocate and enqueue buffers
	int bufferByteSize = MyComputeRecordBufferSize(&recordFormat, queue, 0.5);	// enough bytes for half a second
	int bufferIndex;
    for (bufferIndex = 0; bufferIndex < kNumberRecordBuffers; ++bufferIndex)
	{
		AudioQueueBufferRef buffer;
		CheckError(AudioQueueAllocateBuffer(queue, bufferByteSize, &buffer),
				   "AudioQueueAllocateBuffer failed");
		CheckError(AudioQueueEnqueueBuffer(queue, buffer, 0, NULL),
				   "AudioQueueEnqueueBuffer failed");
	}
	
	// start the queue. this function return immedatly and begins
	// invoking the callback, as needed, asynchronously.
	recorder.running = TRUE;
	CheckError(AudioQueueStart(queue, NULL), "AudioQueueStart failed");
	
	// and wait
	printf("Recording, press <return> to stop:\n");
	getchar();
	
	// end recording
	printf("* recording done *\n");
	recorder.running = FALSE;
	CheckError(AudioQueueStop(queue, TRUE), "AudioQueueStop failed");
	
	// a codec may update its magic cookie at the end of an encoding session
	// so reapply it to the file now
	MyCopyEncoderCookieToFile(queue, recorder.recordFile);
	
cleanup:
	AudioQueueDispose(queue, TRUE);
	AudioFileClose(recorder.recordFile);
	
	return 0;
}
