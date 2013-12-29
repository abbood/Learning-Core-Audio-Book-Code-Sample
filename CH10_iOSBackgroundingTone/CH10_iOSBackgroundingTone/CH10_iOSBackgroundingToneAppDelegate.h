//
//  CH10_iOSBackgroundingToneAppDelegate.h
//  CH10_iOSBackgroundingTone
//
//  Created by Chris Adamson on 4/22/11.
//  Copyright 2011 Subsequently and Furthermore, Inc. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <AudioToolbox/AudioToolbox.h>

@interface CH10_iOSBackgroundingToneAppDelegate : NSObject <UIApplicationDelegate>

@property (nonatomic, retain) IBOutlet UIWindow *window;

@property (nonatomic, assign) AudioStreamBasicDescription streamFormat;
@property (nonatomic, assign) UInt32 bufferSize;
@property (nonatomic, assign) double currentFrequency;
@property (nonatomic, assign) double startingFrameCount;
@property (nonatomic, assign) AudioQueueRef	audioQueue;

-(OSStatus) fillBuffer: (AudioQueueBufferRef) buffer;

@end
