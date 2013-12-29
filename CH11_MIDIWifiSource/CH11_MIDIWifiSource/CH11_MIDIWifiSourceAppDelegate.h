//
//  CH11_MIDIWifiSourceAppDelegate.h
//  CH11_MIDIWifiSource
//
//  Created by Chris Adamson on 9/10/11.
//  Copyright 2011 Subsequently and Furthermore, Inc. All rights reserved.
//

#import <UIKit/UIKit.h>

@class CH11_MIDIWifiSourceViewController;

@interface CH11_MIDIWifiSourceAppDelegate : NSObject <UIApplicationDelegate>

@property (nonatomic, retain) IBOutlet UIWindow *window;

@property (nonatomic, retain) IBOutlet CH11_MIDIWifiSourceViewController *viewController;

@end
