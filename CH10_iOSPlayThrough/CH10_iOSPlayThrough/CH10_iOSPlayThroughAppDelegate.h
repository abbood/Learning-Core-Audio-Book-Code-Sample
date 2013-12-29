//
//  CH10_iOSPlayThroughAppDelegate.h
//  CH10_iOSPlayThrough
//
//  Created by Chris Adamson on 7/10/11.
//  Copyright 2011 Subsequently and Furthermore, Inc. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <AudioToolbox/AudioToolbox.h>

typedef struct {
	AudioUnit rioUnit;
	AudioStreamBasicDescription asbd;
	float sineFrequency;
	float sinePhase;
} EffectState;


@interface CH10_iOSPlayThroughAppDelegate : UIResponder <UIApplicationDelegate> {
	
}

@property (nonatomic, retain) UIWindow *window;
@property (assign) EffectState effectState;

@end
