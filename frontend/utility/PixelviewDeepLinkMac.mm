// Pixelview modification: Universal Links on the existing Qt/OBS app delegate.
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include "PixelviewDeepLinkInbox.hpp"

namespace {
using Restoration = void (^)(NSArray<id<NSUserActivityRestoring>> *);
Class originalClass(id object)
{
    return class_getSuperclass(object_getClass(object));
}
BOOL continueActivity(id self, SEL selector, NSApplication *app, NSUserActivity *activity, Restoration restoration)
{
    if ([activity.activityType isEqualToString:NSUserActivityTypeBrowsingWeb]) {
        NSString *url = activity.webpageURL.absoluteString;
        if ([url hasPrefix:@"https://play.pixelview.io/"] && QCoreApplication::instance()) {
            // No activity object or raw URL is retained. Invalid own-origin links get a fixed UI message.
            pixelview::deepLinkInbox().submit(QString::fromUtf8(url.UTF8String));
            if (restoration) restoration(@[]);
            return YES;
        }
    }
    Method previous = class_getInstanceMethod(originalClass(self), selector);
    if (previous) return reinterpret_cast<BOOL (*)(id, SEL, NSApplication *, NSUserActivity *, Restoration)>(method_getImplementation(previous))(self, selector, app, activity, restoration);
    return NO;
}
BOOL willContinue(id self, SEL selector, NSApplication *app, NSString *type)
{
    if ([type isEqualToString:NSUserActivityTypeBrowsingWeb]) return YES;
    Method previous = class_getInstanceMethod(originalClass(self), selector);
    if (previous) return reinterpret_cast<BOOL (*)(id, SEL, NSApplication *, NSString *)>(method_getImplementation(previous))(self, selector, app, type);
    return NO;
}
}

void pixelview::installMacDeepLinks()
{
    // Main-thread AppKit calls only. Do not replace/retain the Qt delegate or its termination,
    // Dock, AppleEvent URL/file-open and OBS/CEF behavior. Only this instance gains methods.
    id delegate = NSApp.delegate;
    if (!delegate) return;
    Class base = object_getClass(delegate);
    NSString *name = NSStringFromClass(base);
    if ([name hasPrefix:@"PixelviewLinkDelegate_"]) return;
    NSString *subclassName = [@"PixelviewLinkDelegate_" stringByAppendingString:name];
    Class subclass = NSClassFromString(subclassName);
    if (!subclass) {
        subclass = objc_allocateClassPair(base, subclassName.UTF8String, 0);
        if (!subclass) return;
        class_addMethod(subclass, @selector(application:continueUserActivity:restorationHandler:), reinterpret_cast<IMP>(continueActivity), "B@:@@@?");
        class_addMethod(subclass, @selector(application:willContinueUserActivityWithType:), reinterpret_cast<IMP>(willContinue), "B@:@@");
        objc_registerClassPair(subclass);
    }
    object_setClass(delegate, subclass);
    // AppKit caches optional delegate selectors in setDelegate:. Refresh that cache
    // after adding activity support; the OBS hook is idempotent on this subclass.
    NSApp.delegate = delegate;
}
