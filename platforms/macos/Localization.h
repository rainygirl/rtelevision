// Bridge between the core string table and Cocoa.
#pragma once

#import <Foundation/Foundation.h>

#include "core/Strings.h"

// Picks the language from the user's preferred language list, e.g. "ko-KR".
static inline void TVSetUpLanguage() {
    NSString* preferred = [[NSLocale preferredLanguages] firstObject];
    tv::setLanguage(preferred ? tv::languageFromTag(preferred.UTF8String)
                              : tv::detectLanguage());
}

static inline NSString* TVStr(tv::Str id) {
    return [NSString stringWithUTF8String:tv::str(id)];
}

static inline NSString* TVStr2(tv::Str id, NSString* first, NSString* second) {
    const std::string text = tv::format(id, first.UTF8String, second.UTF8String);
    return [NSString stringWithUTF8String:text.c_str()];
}
