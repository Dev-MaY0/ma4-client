#include "pch.h"
#include "Eventing.h"
#include "client/Ma4.h"

Eventing& Eventing::get() {
    return Ma4::getEventing();
}
