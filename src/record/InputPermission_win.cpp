// Windows build of InputPermission: there is no libinput, so click and key
// capture are never available and the recorder records without them.
#include "InputPermission.h"

InputPermission::Status InputPermission::probe()
{
    return NoPermission;
}
