#import "PixelviewSparkle.hpp"

#import <Sparkle/Sparkle.h>
#import <QAction>

@interface PixelviewUpdateObserver : NSObject
@property(nonatomic, strong) SPUStandardUpdaterController *updaterController;
@property(nonatomic, assign) QAction *action;
@property(nonatomic, assign) BOOL observing;
- (instancetype)initWithAction:(QAction *)action;
@end

@implementation PixelviewUpdateObserver

- (instancetype)initWithAction:(QAction *)action
{
	self = [super init];
	if (self) {
		_action = action;
		_updaterController = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
									 updaterDelegate:nil
								  userDriverDelegate:nil];
		[_updaterController.updater addObserver:self
						       forKeyPath:NSStringFromSelector(@selector(canCheckForUpdates))
							  options:(NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew)
							  context:nil];
		_observing = YES;
	}
	return self;
}

- (void)observeValueForKeyPath:(NSString *)keyPath
		      ofObject:(id)object
			change:(NSDictionary<NSKeyValueChangeKey, id> *)change
		       context:(void *)context
{
	if ([keyPath isEqualToString:NSStringFromSelector(@selector(canCheckForUpdates))]) {
		_action->setEnabled(_updaterController.updater.canCheckForUpdates);
	} else {
		[super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
	}
}

- (void)dealloc
{
	if (_observing) {
		[_updaterController.updater removeObserver:self
						 forKeyPath:NSStringFromSelector(@selector(canCheckForUpdates))];
	}
}

@end

PixelviewSparkle::PixelviewSparkle(QAction *checkForUpdatesAction)
{
	@autoreleasepool {
		observer = [[PixelviewUpdateObserver alloc] initWithAction:checkForUpdatesAction];
	}
}

PixelviewSparkle::~PixelviewSparkle()
{
	@autoreleasepool {
		observer = nil;
	}
}

void PixelviewSparkle::checkForUpdates(bool manualCheck)
{
	@autoreleasepool {
		if (manualCheck) {
			[observer.updaterController checkForUpdates:nil];
		} else {
			[observer.updaterController.updater checkForUpdatesInBackground];
		}
	}
}
