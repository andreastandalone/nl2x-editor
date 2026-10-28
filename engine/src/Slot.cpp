#include "nl2x/Slot.h"

namespace nl2x {

char slotLetter(Slot slot)
{
    return static_cast<char>('A' + slotIndex(slot));
}

} // namespace nl2x
