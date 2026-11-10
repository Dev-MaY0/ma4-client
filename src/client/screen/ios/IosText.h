#pragma once
// Minimal single-line text editor model shared by the iOS style UI. The game side (TextBox) derives from this
// so that character / key events routed by the client keep working unchanged.

#include <algorithm>
#include <chrono>
#include <string>

namespace ios {

    class TextEdit {
    public:
        TextEdit() = default;
        explicit TextEdit(int maxChars)
            : maxChars(maxChars) {}
        virtual ~TextEdit() = default;

        [[nodiscard]] bool isSelected() const { return selected_; }

        void setSelected(bool b) {
            selected_ = b;
            if (b) startTime_ = std::chrono::steady_clock::now();
        }

        [[nodiscard]] bool shouldBlink() const {
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime_);
            return (diff.count() % 1000) < 500;
        }

        void onChar(wchar_t character) {
            if (character == L'\b') {
                if (!text_.empty() && place_ > 0) {
                    place_ = std::min(place_, static_cast<int>(text_.size()));
                    text_.erase(text_.begin() + (place_ - 1));
                    --place_;
                }
            } else if (character > 31 && static_cast<int>(text_.size()) < maxChars) {
                text_.insert(text_.begin() + std::min(static_cast<int>(text_.size()), place_), character);
                ++place_;
            }
            startTime_ = std::chrono::steady_clock::now();
        }

        void onKeyDown(int key) {
            const int VK_LEFT_ = 0x25, VK_RIGHT_ = 0x27, VK_DELETE_ = 0x2E, VK_HOME_ = 0x24, VK_END_ = 0x23;
            const int size = static_cast<int>(text_.size());
            if (key == VK_LEFT_) {
                place_ = rtl ? std::min(place_ + 1, size) : std::max(place_ - 1, 0);
            } else if (key == VK_RIGHT_) {
                place_ = rtl ? std::max(place_ - 1, 0) : std::min(place_ + 1, size);
            } else if (key == VK_HOME_) {
                place_ = 0;
            } else if (key == VK_END_) {
                place_ = size;
            } else if (key == VK_DELETE_) {
                if (place_ < size) text_.erase(text_.begin() + place_);
            } else {
                return;
            }
            startTime_ = std::chrono::steady_clock::now();
        }

        [[nodiscard]] std::wstring getText() const { return text_; }

        void setText(std::wstring const& str) {
            text_ = str;
            if (place_ > static_cast<int>(str.size())) place_ = static_cast<int>(str.size());
        }

        void reset() {
            text_.clear();
            place_ = 0;
        }

        [[nodiscard]] int getCaretLocation() const { return place_; }
        void setCaretLocation(int loc) { place_ = std::clamp(loc, 0, static_cast<int>(text_.size())); }
        void setMaxChars(int n) { maxChars = n; }

        bool rtl = false;

    protected:
        std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();
        int maxChars = 64;
        int place_ = 0;
        std::wstring text_;
        bool selected_ = false;
    };

} // namespace ios
