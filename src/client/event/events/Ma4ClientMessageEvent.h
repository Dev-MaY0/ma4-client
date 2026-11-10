#pragma once
class Ma4ClientMessageEvent : public Event {
public:
    static const uint32_t hash = TOHASH(Ma4ClientMessageEvent);

    Ma4ClientMessageEvent(std::string const& msg)
        : message(msg) {}

    [[nodiscard]] std::string getMessage() { return message; }

private:
    std::string message;
};
