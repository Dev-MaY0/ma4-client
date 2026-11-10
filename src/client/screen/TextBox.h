#pragma once
#include "util/Util.h"
#include "util/Dxutil.h"
#include "client/screen/ios/IosText.h"
#ifdef min
#undef min
#undef max
#endif

// Text input model registered with the client so character / key events reach the focused field.
// The editing logic lives in ios::TextEdit; the iOS style UI draws it.
class TextBox : public ios::TextEdit {
public:
    TextBox() = default;
    explicit TextBox(int maxChars)
        : ios::TextEdit(maxChars) {}
};
